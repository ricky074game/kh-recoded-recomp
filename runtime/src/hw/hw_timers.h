#pragma once

// ============================================================================
// hw_timers.h — Nintendo DS Hardware Timer & RTC Emulation
//
// The DS has 4 hardware timers per CPU (0-3). Each can be individually
// configured with a prescaler, count-up (cascade) mode, and IRQ enable.
// The base clock is exactly 33.513982 MHz (Bus clock = 33513982 Hz).
//
// The Real-Time Clock (RTC) sits on the ARM7 SPI bus and provides
// BCD-encoded date/time values to the game.
//
// Reference: GBATEK §DS Timers, §DS Real-Time Clock
//   Timer registers: 0x04000100 - 0x0400010F (4 timers × 4 bytes)
// ============================================================================

#include <cstdint>
#include <chrono>
#include <ctime>

// ---- DS Clock Constants ----
namespace DSClock {
    constexpr double BUS_HZ       = 33513982.0;  // Exact ARM7/timer bus frequency
    constexpr double TICKS_PER_US = 33.513982;    // Ticks per microsecond
}

// ---- Timer Prescaler Dividers ----
// TMCNT_H bits 0-1 select the prescaler.
namespace TimerPrescaler {
    constexpr uint16_t DIV_1    = 0; // F/1   (33.514 MHz)
    constexpr uint16_t DIV_64   = 1; // F/64  (523.657 kHz)
    constexpr uint16_t DIV_256  = 2; // F/256 (130.914 kHz)
    constexpr uint16_t DIV_1024 = 3; // F/1024 (32.729 kHz)

    // Returns the actual divider value.
    inline uint32_t GetDivider(uint16_t prescaler) {
        switch (prescaler & 0x3) {
            case DIV_1:    return 1;
            case DIV_64:   return 64;
            case DIV_256:  return 256;
            case DIV_1024: return 1024;
            default:       return 1;
        }
    }
}

// ---- Single Hardware Timer ----
struct HWTimerChannel {
    uint16_t reload   = 0;     // Reload value (written to counter on overflow or start)
    uint16_t control  = 0;     // TMCNT_H: prescaler, count-up, IRQ, enable
    bool     running  = false;
    bool     countup  = false; // Count-up mode: increments when previous timer overflows
    bool     irq_en   = false;
    uint16_t prescaler = 0;

    // Timestamp when this timer was started (for computing elapsed ticks)
    std::chrono::high_resolution_clock::time_point start_time;
    uint64_t accumulated_ticks = 0; // The actual 16-bit counter value in cycle-accurate mode
    uint64_t cycle_remainder = 0;   // The sub-tick cycle remainder

    void WriteControl(uint16_t value) {
        control   = value;
        prescaler = value & 0x3;
        countup   = (value >> 2) & 1;
        irq_en    = (value >> 6) & 1;
        bool was_running = running;
        running   = (value >> 7) & 1;

        // If transitioning from stopped to running, capture start time
        if (!was_running && running) {
            start_time = std::chrono::high_resolution_clock::now();
            accumulated_ticks = reload; // Initialize counter with reload
            cycle_remainder = 0;
        }
    }

    // Returns the current 16-bit counter value based on executed cycles.
    uint16_t ReadCounter() const {
        if (!running && !countup) {
            return reload;
        }
        return static_cast<uint16_t>(accumulated_ticks & 0xFFFF);
    }
};

// ---- Timer Manager (4 channels) ----
class HWTimerManager {
public:
    HWTimerChannel channels[4];

    HWTimerManager() = default;

    // Read/write a timer register by its IO address offset from 0x04000100.
    uint16_t ReadTimer(uint32_t offset) const {
        uint32_t ch = (offset / 4) & 3;
        if (offset % 4 == 0) {
            return channels[ch].ReadCounter();
        } else {
            return channels[ch].control;
        }
    }

    void WriteTimer(uint32_t offset, uint16_t value) {
        uint32_t ch = (offset / 4) & 3;
        if (offset % 4 == 0) {
            channels[ch].reload = value;
        } else {
            channels[ch].WriteControl(value);
        }
    }
};

// ---- Legacy compatibility wrapper ----
class HWTimer {
public:
    HWTimer() = default;

    uint16_t scale(int us, int pre) {
        if (pre == 0) return 0;
        double ticks = static_cast<double>(us) * DSClock::TICKS_PER_US;
        return static_cast<uint16_t>(ticks / pre);
    }
};

// ---- Real-Time Clock (RTC) ----
// Sits on the ARM7 SPI bus. Provides BCD-encoded date/time.
class HWRTC {
public:
    // BCD-encodes a decimal value (0-99).
    static uint8_t bcd(int v) {
        return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
    }

    // Returns the current host time as a 7-byte BCD time struct.
    // Format: [year(00-99), month(1-12), day(1-31), weekday(0-6),
    //          hour(0-23), minute(0-59), second(0-59)]
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
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        struct tm* t = std::localtime(&now);

        RTCTime rtc;
        rtc.year    = bcd(t->tm_year % 100);
        rtc.month   = bcd(t->tm_mon + 1);
        rtc.day     = bcd(t->tm_mday);
        rtc.weekday = bcd(t->tm_wday);
        rtc.hour    = bcd(t->tm_hour);
        rtc.minute  = bcd(t->tm_min);
        rtc.second  = bcd(t->tm_sec);
        return rtc;
    }

    // SPI protocol state for RTC communication
    enum class SPIState { Idle, Command, Reading, Writing };

    SPIState state         = SPIState::Idle;
    uint8_t  command       = 0;
    uint8_t  data_index    = 0;
    uint8_t  data_buffer[7] = {};

    // Process a byte written to the SPI data register.
    uint8_t ProcessSPIByte(uint8_t input) {
        switch (state) {
            case SPIState::Idle:
                command = input;
                data_index = 0;
                if (command & 0x01) {
                    // Read command — fill buffer with current time
                    auto time = GetCurrentTime();
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

            default:
                state = SPIState::Idle;
                return 0;
        }
    }
};

class TimerCore {
public:
    HWTimerManager* tmr = nullptr;
    class HWIRQ* irq = nullptr;

public:
    void StepTimers(int cycles);
    void CheckCascades();
    void FireTimerIRQ(int timer_id);
    void ReloadTimer(int timer_id);
};
