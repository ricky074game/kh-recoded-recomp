#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>

class NDS2DEngine;

namespace DSClock {
constexpr double BUS_HZ = 33513982.0;
constexpr double TICKS_PER_US = 33.513982;
}

namespace TimerPrescaler {
constexpr uint16_t DIV_1 = 0;
constexpr uint16_t DIV_64 = 1;
constexpr uint16_t DIV_256 = 2;
constexpr uint16_t DIV_1024 = 3;

inline uint32_t GetDivider(uint16_t prescaler) {
    switch (prescaler & 0x3u) {
        case DIV_1:
            return 1;
        case DIV_64:
            return 64;
        case DIV_256:
            return 256;
        case DIV_1024:
            return 1024;
        default:
            return 1;
    }
}
}

struct HWTimerChannel {
    uint16_t reload = 0;
    uint16_t control = 0;
    uint16_t counter = 0;
    bool running = false;
    bool countup = false;
    bool irq_en = false;
    uint16_t prescaler = 0;
    uint32_t cycle_remainder = 0;

    void WriteReload(uint16_t value) {
        reload = value;
        if (!running) {
            counter = value;
        }
    }

    void WriteControl(uint16_t value) {
        const bool was_running = running;

        control = static_cast<uint16_t>(value & 0x00C7u);
        prescaler = static_cast<uint16_t>(control & 0x3u);
        countup = (control & (1u << 2)) != 0;
        irq_en = (control & (1u << 6)) != 0;
        running = (control & (1u << 7)) != 0;

        if (!was_running && running) {
            counter = reload;
            cycle_remainder = 0;
        }
    }

    uint16_t ReadCounter() const {
        return counter;
    }
};

class HWTimerManager {
public:
    HWTimerChannel channels[4];

    uint16_t ReadTimer(uint32_t offset) const {
        const uint32_t ch = (offset / 4u) & 3u;
        if ((offset & 0x2u) == 0) {
            return channels[ch].ReadCounter();
        }
        return channels[ch].control;
    }

    void WriteTimer(uint32_t offset, uint16_t value) {
        const uint32_t ch = (offset / 4u) & 3u;
        if ((offset & 0x2u) == 0) {
            channels[ch].WriteReload(value);
        } else {
            channels[ch].WriteControl(value);
        }
    }
};

class HWTimer {
public:
    uint16_t scale(int us, int pre) {
        if (pre == 0) {
            return 0;
        }
        const double ticks = static_cast<double>(us) * DSClock::TICKS_PER_US;
        return static_cast<uint16_t>(ticks / pre);
    }
};

class HWRTC {
public:
    static uint8_t bcd(int v) {
        return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
    }

    struct RTCTime {
        uint8_t year;
        uint8_t month;
        uint8_t day;
        uint8_t weekday;
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
    };

    RTCTime GetCurrentTime() const {
        const auto now =
            std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm* t = std::localtime(&now);

        RTCTime rtc{};
        rtc.year = bcd(t->tm_year % 100);
        rtc.month = bcd(t->tm_mon + 1);
        rtc.day = bcd(t->tm_mday);
        rtc.weekday = bcd(t->tm_wday);
        rtc.hour = bcd(t->tm_hour);
        rtc.minute = bcd(t->tm_min);
        rtc.second = bcd(t->tm_sec);
        return rtc;
    }

    enum class SPIState { Idle, Command, Reading, Writing };

    SPIState state = SPIState::Idle;
    uint8_t command = 0;
    uint8_t data_index = 0;
    uint8_t data_buffer[7] = {};

    uint8_t ProcessSPIByte(uint8_t input) {
        switch (state) {
            case SPIState::Idle:
                command = input;
                data_index = 0;
                if ((command & 0x01u) != 0) {
                    const auto time = GetCurrentTime();
                    data_buffer[0] = time.year;
                    data_buffer[1] = time.month;
                    data_buffer[2] = time.day;
                    data_buffer[3] = time.weekday;
                    data_buffer[4] = time.hour;
                    data_buffer[5] = time.minute;
                    data_buffer[6] = time.second;
                    state = SPIState::Reading;
                } else {
                    state = SPIState::Writing;
                }
                return 0;

            case SPIState::Reading:
                if (data_index < 7) {
                    return data_buffer[data_index++];
                }
                state = SPIState::Idle;
                return 0;

            case SPIState::Writing:
                if (data_index < 7) {
                    data_buffer[data_index++] = input;
                }
                state = SPIState::Idle;
                return 0;
        }

        state = SPIState::Idle;
        return 0;
    }
};

class TimerCore {
public:
    HWTimerManager* tmr = nullptr;
    class HWIRQ* irq = nullptr;
    class HWIRQ* irq_arm7 = nullptr;
    NDS2DEngine* engine_a = nullptr;
    NDS2DEngine* engine_b = nullptr;

    void StepTimers(int cycles);
    void StepVideo(int cycles);
    void CheckCascades();
    void FireTimerIRQ(int timer_id);
    void ReloadTimer(int timer_id);
};
