#pragma once

// ============================================================================
// hw_audio.h — Nintendo DS Audio Emulation (Miniaudio)
//
// Bypasses the ARM7 software mixer and maps DS SDAT Sequence IDs
// directly to high-quality PCM files on the host PC (FLAC/OGG).
// ============================================================================

#include <cstdint>
#include <string>
#include <unordered_map>
#include <cstddef>
// We don't include miniaudio.h in the header to avoid massive compilation times
// Forward declare basic types used internally in hw_audio.cpp.

struct ma_engine;

class AudioManager {
private:
    ma_engine* engine;
    
    // Sequence ID -> PCM File Path
    std::unordered_map<uint32_t, std::string> seq_to_file;

public:
    AudioManager();
    ~AudioManager();

    bool Initialize();
    void Shutdown();

    void LoadMap(const std::string& map_path);
    
    // Play an SDAT Sequence
    void PlaySequence(uint32_t seq_id);
    void StopAll();
    
    void SetMasterVolume(float volume);

    // DS-like channel synthesis hooks.
    void SynthesizePCM();
    void SynthesizeADPCM();
    void SetChannelPanning(int channel, int pan);
    void SetChannelPitch(int channel, float pitch);
    void TriggerAudioDMA();

    // Introspection helpers for tests/debug.
    bool IsInitialized() const;
    std::size_t GetMappedSequenceCount() const;
};
