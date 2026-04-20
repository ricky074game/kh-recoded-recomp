#include "miniaudio_backend.h"
#include "miniaudio.h"

#include <atomic>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <mutex>
#include <vector>

namespace Backend::Audio {

namespace {
constexpr ma_uint32 kSampleRate = 32768;
constexpr ma_uint32 kChannels = 2;
constexpr size_t kBytesPerFrame = sizeof(int16_t) * kChannels;
constexpr size_t kDefaultFrameCapacity = kSampleRate * 2;  // 2 seconds.

ma_device device{};
ma_context context{};
ma_rb ring_buffer{};
std::vector<uint8_t> rb_data;

std::mutex lifecycle_mutex;
std::atomic<bool> context_ready{false};
std::atomic<bool> device_ready{false};
std::atomic<bool> ring_buffer_ready{false};

bool SetupRingBufferLocked() {
    if (ring_buffer_ready.load()) {
        return true;
    }

    rb_data.assign(kDefaultFrameCapacity * kBytesPerFrame, 0);
    if (rb_data.empty()) {
        std::cerr << "Miniaudio: Failed to reserve ring buffer storage.\n";
        return false;
    }

    if (ma_rb_init(rb_data.size(), rb_data.data(), nullptr, &ring_buffer) != MA_SUCCESS) {
        std::cerr << "Miniaudio: Failed to initialize ring buffer.\n";
        rb_data.clear();
        return false;
    }

    ring_buffer_ready.store(true);
    return true;
}

void StreamCallbackInternal(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    StreamCallback(pDevice, pOutput, pInput, static_cast<uint32_t>(frameCount));
}
}

void InitMiniaudio() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex);

    if (device_ready.load()) {
        return;
    }

    if (!context_ready.load()) {
        if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
            std::cerr << "Miniaudio: Failed to initialize context.\n";
            return;
        }
        context_ready.store(true);
    }

    if (!SetupRingBufferLocked()) {
        return;
    }

    ma_device_config device_config = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format = ma_format_s16;
    device_config.playback.channels = kChannels;
    device_config.sampleRate = kSampleRate;
    device_config.dataCallback = StreamCallbackInternal;

    if (ma_device_init(&context, &device_config, &device) != MA_SUCCESS) {
        std::cerr << "Miniaudio: Failed to initialize playback device.\n";
        return;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cerr << "Miniaudio: Failed to start playback device.\n";
        ma_device_uninit(&device);
        return;
    }

    device_ready.store(true);
}

void ShutdownMiniaudio() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex);

    if (device_ready.load()) {
        ma_device_uninit(&device);
        device_ready.store(false);
    }

    if (context_ready.load()) {
        ma_context_uninit(&context);
        context_ready.store(false);
    }

    if (ring_buffer_ready.load()) {
        ma_rb_uninit(&ring_buffer);
        ring_buffer_ready.store(false);
        rb_data.clear();
    }
}

void EnumerateDevices() {
    if (!context_ready.load()) {
        return;
    }

    ma_device_info* playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    if (ma_context_get_devices(&context, &playback_infos, &playback_count, nullptr, nullptr) != MA_SUCCESS) {
        return;
    }

    for (ma_uint32 i = 0; i < playback_count; ++i) {
        std::cout << "Playback device " << i << ": " << playback_infos[i].name << "\n";
    }
}

void SetupRingBuffer() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex);
    (void)SetupRingBufferLocked();
}

bool PushInterleavedSamples(const int16_t* samples, uint32_t frameCount) {
    if (samples == nullptr || frameCount == 0 || !ring_buffer_ready.load()) {
        return false;
    }

    size_t bytes_remaining = static_cast<size_t>(frameCount) * kBytesPerFrame;
    const uint8_t* src = reinterpret_cast<const uint8_t*>(samples);

    while (bytes_remaining > 0) {
        size_t bytes_to_write = bytes_remaining;
        void* write_buffer = nullptr;

        if (ma_rb_acquire_write(&ring_buffer, &bytes_to_write, &write_buffer) != MA_SUCCESS ||
            bytes_to_write == 0 || write_buffer == nullptr) {
            break;
        }

        std::memcpy(write_buffer, src, bytes_to_write);
        ma_rb_commit_write(&ring_buffer, bytes_to_write);

        src += bytes_to_write;
        bytes_remaining -= bytes_to_write;
    }

    return bytes_remaining == 0;
}

uint32_t GetBufferedFrames() {
    if (!ring_buffer_ready.load()) {
        return 0;
    }

    return ma_rb_available_read(&ring_buffer) / static_cast<uint32_t>(kBytesPerFrame);
}

void ClearBufferedAudio() {
    if (!ring_buffer_ready.load()) {
        return;
    }

    ma_rb_reset(&ring_buffer);
}

bool IsInitialized() {
    return device_ready.load();
}

void StreamCallback(void* pDevice, void* pOutput, const void* pInput, uint32_t frameCount) {
    const size_t output_size = static_cast<size_t>(frameCount) * kBytesPerFrame;
    uint8_t* out = static_cast<uint8_t*>(pOutput);
    std::memset(out, 0, output_size);

    if (!ring_buffer_ready.load()) {
        (void)pInput;
        (void)pDevice;
        return;
    }

    size_t copied = 0;
    size_t bytes_remaining = output_size;
    while (bytes_remaining > 0) {
        size_t bytes_to_read = bytes_remaining;
        void* read_buffer = nullptr;

        if (ma_rb_acquire_read(&ring_buffer, &bytes_to_read, &read_buffer) != MA_SUCCESS ||
            bytes_to_read == 0 || read_buffer == nullptr) {
            break;
        }

        std::memcpy(out + copied, read_buffer, bytes_to_read);
        ma_rb_commit_read(&ring_buffer, bytes_to_read);

        copied += bytes_to_read;
        bytes_remaining -= bytes_to_read;
    }

    (void)pInput;
    (void)pDevice;
}
}
