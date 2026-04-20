#include "hw_mic.h"

#include <array>
#include <cstddef>
#include <mutex>

namespace HwMic {

namespace {
constexpr std::size_t kMicFifoSize = 256;

std::array<uint8_t, kMicFifoSize> mic_fifo{};
std::size_t fifo_head = 0;
std::size_t fifo_tail = 0;
std::size_t fifo_count = 0;
std::mutex mic_mutex;

bool capture_enabled = false;
uint16_t noise_lfsr = 0xACE1u;

uint8_t NextNoiseSample() {
    const uint16_t bit = static_cast<uint16_t>((noise_lfsr ^ (noise_lfsr >> 1)) & 1u);
    noise_lfsr = static_cast<uint16_t>((noise_lfsr >> 1) | (bit << 15));
    return static_cast<uint8_t>(0x70u + (noise_lfsr & 0x1Fu));
}
}

void Init() {
    std::lock_guard<std::mutex> lock(mic_mutex);
    mic_fifo.fill(0x80);
    fifo_head = 0;
    fifo_tail = 0;
    fifo_count = 0;
    capture_enabled = false;
    noise_lfsr = 0xACE1u;
}

void SetCaptureEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mic_mutex);
    capture_enabled = enabled;
}

void PushSample(uint8_t sample) {
    std::lock_guard<std::mutex> lock(mic_mutex);

    if (fifo_count == kMicFifoSize) {
        // Overwrite oldest sample when producer outruns consumer.
        fifo_head = (fifo_head + 1) % kMicFifoSize;
        --fifo_count;
    }

    mic_fifo[fifo_tail] = sample;
    fifo_tail = (fifo_tail + 1) % kMicFifoSize;
    ++fifo_count;
}

uint8_t ReadData() {
    std::lock_guard<std::mutex> lock(mic_mutex);

    if (!capture_enabled) {
        return 0x80;
    }

    if (fifo_count > 0) {
        const uint8_t sample = mic_fifo[fifo_head];
        fifo_head = (fifo_head + 1) % kMicFifoSize;
        --fifo_count;
        return sample;
    }

    return NextNoiseSample();
}
}
