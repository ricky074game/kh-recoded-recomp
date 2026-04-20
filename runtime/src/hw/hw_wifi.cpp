#include "hw_wifi.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace HwWifi {

namespace {
constexpr uint32_t kBaseAddress = 0x04804000;
constexpr uint32_t kEndAddress = 0x04805000;
constexpr std::size_t kRegisterCount = (kEndAddress - kBaseAddress) / 2;

constexpr std::size_t kRegControl = 0x0000 / 2;
constexpr std::size_t kRegStatus = 0x0002 / 2;
constexpr std::size_t kRegBeaconTick = 0x0010 / 2;
constexpr std::size_t kRegIrqStatus = 0x0012 / 2;

std::array<uint16_t, kRegisterCount> mac_registers{};
std::mutex wifi_mutex;
std::atomic<bool> irq_pending{false};

bool IsMappedAddress(uint32_t address) {
    return address >= kBaseAddress && address < kEndAddress && (address % 2u) == 0;
}

std::size_t ToRegisterIndex(uint32_t address) {
    return static_cast<std::size_t>((address - kBaseAddress) / 2);
}
}

void Init() {
    std::lock_guard<std::mutex> lock(wifi_mutex);
    mac_registers.fill(0);

    // Status bit0 marks hardware present; bit1 reflects pending IRQ.
    mac_registers[kRegStatus] = 0x0001;
    mac_registers[kRegControl] = 0x0000;
    mac_registers[kRegBeaconTick] = 0x0000;
    mac_registers[kRegIrqStatus] = 0x0000;
    irq_pending.store(false);
}

void Update() {
    std::lock_guard<std::mutex> lock(wifi_mutex);

    if ((mac_registers[kRegControl] & 0x0001) == 0) {
        return;
    }

    mac_registers[kRegBeaconTick] = static_cast<uint16_t>(mac_registers[kRegBeaconTick] + 1u);

    // Signal a periodic IRQ every 1024 update ticks to emulate beacon activity.
    if ((mac_registers[kRegBeaconTick] & 0x03FFu) == 0) {
        mac_registers[kRegIrqStatus] |= 0x0001;
        mac_registers[kRegStatus] |= 0x0002;
        irq_pending.store(true);
    }
}

uint16_t ReadRegister(uint32_t address) {
    if (!IsMappedAddress(address)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(wifi_mutex);
    return mac_registers[ToRegisterIndex(address)];
}

void WriteRegister(uint32_t address, uint16_t value) {
    if (!IsMappedAddress(address)) {
        return;
    }

    std::lock_guard<std::mutex> lock(wifi_mutex);
    const std::size_t idx = ToRegisterIndex(address);
    mac_registers[idx] = value;

    if (idx == kRegIrqStatus) {
        // Writing 1 to bit0 acknowledges the synthetic beacon IRQ.
        if ((value & 0x0001) != 0) {
            mac_registers[kRegIrqStatus] &= static_cast<uint16_t>(~0x0001u);
            mac_registers[kRegStatus] &= static_cast<uint16_t>(~0x0002u);
            irq_pending.store(false);
        }
    }

    if (idx == kRegControl && (value & 0x0001) == 0) {
        // Disabling the MAC clears pending periodic state.
        mac_registers[kRegIrqStatus] = 0;
        mac_registers[kRegStatus] &= static_cast<uint16_t>(~0x0002u);
        irq_pending.store(false);
    }
}

bool IsIRQPending() {
    return irq_pending.load();
}

void ClearIRQ() {
    std::lock_guard<std::mutex> lock(wifi_mutex);
    mac_registers[kRegIrqStatus] = 0;
    mac_registers[kRegStatus] &= static_cast<uint16_t>(~0x0002u);
    irq_pending.store(false);
}

}
