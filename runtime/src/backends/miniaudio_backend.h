#pragma once

#include <cstddef>
#include <cstdint>

namespace Backend::Audio {
    void InitMiniaudio();
    void ShutdownMiniaudio();
    void EnumerateDevices();
    void SetupRingBuffer();
    bool PushInterleavedSamples(const int16_t* samples, uint32_t frameCount);
    uint32_t GetBufferedFrames();
    void ClearBufferedAudio();
    bool IsInitialized();
    void StreamCallback(void* pDevice, void* pOutput, const void* pInput, uint32_t frameCount);
}
