#include "hw_rtc.h"
#include <chrono>

namespace HwRtc {
    // Basic RTC instance for standard SPI reads
    class HW_RTC {
    public:
        static uint8_t bcd(int v) {
            return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
        }

        struct RTCTime {
            uint8_t year, month, day, weekday, hour, minute, second;
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

        enum class SPIState { Idle, Command, Reading, Writing };
        SPIState state = SPIState::Idle;
        uint8_t command = 0, data_index = 0, data_buffer[7] = {};

        uint8_t ProcessSPIByte(uint8_t input) {
            switch (state) {
                case SPIState::Idle:
                    command = input;
                    data_index = 0;
                    if (command & 0x01) { // Read
                        auto time = GetCurrentTime();
                        data_buffer[0] = time.year; data_buffer[1] = time.month; data_buffer[2] = time.day;
                        data_buffer[3] = time.weekday; data_buffer[4] = time.hour; data_buffer[5] = time.minute;
                        data_buffer[6] = time.second;
                        state = SPIState::Reading;
                    } else {
                        state = SPIState::Writing;
                    }
                    return 0;
                case SPIState::Reading:
                    if (data_index < 7) return data_buffer[data_index++];
                    state = SPIState::Idle; return 0;
                case SPIState::Writing:
                    if (data_index < 7) data_buffer[data_index++] = input;
                    state = SPIState::Idle; return 0;
                default:
                    state = SPIState::Idle; return 0;
            }
        }
    };

    static HW_RTC g_rtc;

    void Init() {
        g_rtc.state = HW_RTC::SPIState::Idle;
    }

    uint8_t ReadSPI() {
        // Return dummy or last byte. ProcessSPIByte handles it typically.
        return 0;
    }

    void WriteSPI(uint8_t value) {
        g_rtc.ProcessSPIByte(value);
    }
}
