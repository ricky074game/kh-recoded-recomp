#include "hw_audio.h"
#include "miniaudio_backend.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr std::size_t kChannelCount = 16;
constexpr uint32_t kMixFramesPerBatch = 256;
constexpr float kPi = 3.14159265359f;

struct DSChannel {
    uint32_t control = 0;
    uint32_t source_addr = 0;
    uint16_t timer = 0;
    uint16_t repeat_point = 0;
    uint32_t length = 0;

    float pan = 0.0f;      // -1.0 (left) .. 1.0 (right)
    float pitch = 1.0f;    // playback step
    float volume = 0.5f;   // [0, 1]

    bool active = false;
    bool adpcm = false;
    bool looping = true;

    std::vector<int16_t> buffer;
    float cursor = 0.0f;
    int16_t adpcm_predictor = 0;
};

std::array<DSChannel, kChannelCount> g_channels;
std::mutex g_channels_mutex;
float g_master_gain = 1.0f;

std::string Trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::vector<int16_t> BuildToneBuffer(float frequency_hz, std::size_t samples) {
    std::vector<int16_t> tone(samples);
    for (std::size_t i = 0; i < samples; ++i) {
        const float phase = static_cast<float>(i) / static_cast<float>(samples);
        const float value = std::sin(2.0f * kPi * phase * frequency_hz);
        tone[i] = static_cast<int16_t>(value * 2800.0f);
    }
    return tone;
}

void ResetChannelsLocked() {
    for (std::size_t i = 0; i < kChannelCount; ++i) {
        DSChannel& channel = g_channels[i];
        channel = DSChannel{};
        channel.pan = 0.0f;
        channel.pitch = 1.0f;
        channel.volume = (i == 0) ? 0.7f : 0.3f;
        channel.looping = true;
        channel.adpcm = (i % 2) == 1;
        channel.buffer = BuildToneBuffer(0.75f + static_cast<float>(i), 128);
        channel.active = (i == 0);
    }
}

float ClampPitch(float pitch) {
    return std::clamp(pitch, 0.125f, 8.0f);
}

float ClampPanFromDS(int pan) {
    const int clamped = std::clamp(pan, 0, 127);
    return (static_cast<float>(clamped) / 63.5f) - 1.0f;
}

int16_t NextChannelSample(DSChannel& channel, bool adpcm_pass) {
    if (!channel.active || channel.buffer.empty() || channel.adpcm != adpcm_pass) {
        return 0;
    }

    const std::size_t index = static_cast<std::size_t>(channel.cursor) % channel.buffer.size();
    int16_t sample = channel.buffer[index];

    if (adpcm_pass) {
        const int16_t predicted = static_cast<int16_t>((sample + channel.adpcm_predictor) / 2);
        channel.adpcm_predictor = predicted;
        sample = predicted;
    }

    channel.cursor += ClampPitch(channel.pitch);
    if (channel.cursor >= static_cast<float>(channel.buffer.size())) {
        if (channel.looping) {
            const float loop_start = static_cast<float>(
                std::min<std::size_t>(channel.repeat_point, channel.buffer.size() - 1));
            channel.cursor = loop_start;
        } else {
            channel.active = false;
            channel.cursor = static_cast<float>(channel.buffer.size() - 1);
        }
    }

    return sample;
}

void MixPass(bool adpcm_pass, std::array<int16_t, kMixFramesPerBatch * 2>& mixed_output) {
    mixed_output.fill(0);

    for (uint32_t frame = 0; frame < kMixFramesPerBatch; ++frame) {
        float left = 0.0f;
        float right = 0.0f;

        for (DSChannel& channel : g_channels) {
            const int16_t sample = NextChannelSample(channel, adpcm_pass);
            if (sample == 0) {
                continue;
            }

            const float normalized = static_cast<float>(sample) / 32768.0f;
            const float pan = std::clamp(channel.pan, -1.0f, 1.0f);
            const float left_gain = ((1.0f - pan) * 0.5f) * channel.volume;
            const float right_gain = ((1.0f + pan) * 0.5f) * channel.volume;

            left += normalized * left_gain;
            right += normalized * right_gain;
        }

        left *= g_master_gain;
        right *= g_master_gain;

        left = std::clamp(left, -1.0f, 1.0f);
        right = std::clamp(right, -1.0f, 1.0f);

        mixed_output[frame * 2 + 0] = static_cast<int16_t>(left * 32767.0f);
        mixed_output[frame * 2 + 1] = static_cast<int16_t>(right * 32767.0f);
    }
}
}

AudioManager::AudioManager() : engine(nullptr) {
    std::lock_guard<std::mutex> lock(g_channels_mutex);
    ResetChannelsLocked();
}

AudioManager::~AudioManager() {
    Shutdown();
}

bool AudioManager::Initialize() {
    if (engine != nullptr) {
        return true;
    }

    Backend::Audio::SetupRingBuffer();
    Backend::Audio::InitMiniaudio();

    engine = new ma_engine;
    const ma_result result = ma_engine_init(nullptr, engine);
    if (result != MA_SUCCESS) {
        std::cerr << "AudioManager: Failed to initialize Miniaudio engine (" << result << ").\n";
        delete engine;
        engine = nullptr;
        return false;
    }

    std::cout << "AudioManager: initialized.\n";
    return true;
}

void AudioManager::Shutdown() {
    if (engine != nullptr) {
        ma_engine_uninit(engine);
        delete engine;
        engine = nullptr;
    }

    Backend::Audio::ShutdownMiniaudio();
}

void AudioManager::LoadMap(const std::string& map_path) {
    std::ifstream file(map_path);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }

        const std::size_t delim = line.find('=');
        if (delim == std::string::npos) {
            continue;
        }

        const std::string id_str = Trim(line.substr(0, delim));
        const std::string path_str = Trim(line.substr(delim + 1));
        if (id_str.empty() || path_str.empty()) {
            continue;
        }

        try {
            const uint32_t id = static_cast<uint32_t>(std::stoul(id_str));
            seq_to_file[id] = path_str;
        } catch (...) {
            // Ignore malformed IDs.
        }
    }
}

void AudioManager::PlaySequence(uint32_t seq_id) {
    if (engine == nullptr) {
        return;
    }

    const auto it = seq_to_file.find(seq_id);
    if (it == seq_to_file.end()) {
        return;
    }

    (void)ma_engine_play_sound(engine, it->second.c_str(), nullptr);
}

void AudioManager::StopAll() {
    if (engine == nullptr) {
        return;
    }

    (void)ma_engine_stop(engine);
    (void)ma_engine_start(engine);
}

void AudioManager::SetMasterVolume(float volume) {
    const float clamped = std::clamp(volume, 0.0f, 1.0f);
    {
        std::lock_guard<std::mutex> lock(g_channels_mutex);
        g_master_gain = clamped;
    }

    if (engine != nullptr) {
        (void)ma_engine_set_volume(engine, clamped);
    }
}

void AudioManager::SynthesizePCM() {
    std::array<int16_t, kMixFramesPerBatch * 2> mixed{};
    {
        std::lock_guard<std::mutex> lock(g_channels_mutex);
        MixPass(false, mixed);
    }

    Backend::Audio::SetupRingBuffer();
    (void)Backend::Audio::PushInterleavedSamples(mixed.data(), kMixFramesPerBatch);
}

void AudioManager::SynthesizeADPCM() {
    std::array<int16_t, kMixFramesPerBatch * 2> mixed{};
    {
        std::lock_guard<std::mutex> lock(g_channels_mutex);
        MixPass(true, mixed);
    }

    Backend::Audio::SetupRingBuffer();
    (void)Backend::Audio::PushInterleavedSamples(mixed.data(), kMixFramesPerBatch);
}

void AudioManager::SetChannelPanning(int channel, int pan) {
    if (channel < 0 || channel >= static_cast<int>(kChannelCount)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_channels_mutex);
    g_channels[static_cast<std::size_t>(channel)].pan = ClampPanFromDS(pan);
}

void AudioManager::SetChannelPitch(int channel, float pitch) {
    if (channel < 0 || channel >= static_cast<int>(kChannelCount)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_channels_mutex);
    g_channels[static_cast<std::size_t>(channel)].pitch = ClampPitch(pitch);
}

void AudioManager::TriggerAudioDMA() {
    SynthesizePCM();
    SynthesizeADPCM();
}

bool AudioManager::IsInitialized() const {
    return engine != nullptr;
}

std::size_t AudioManager::GetMappedSequenceCount() const {
    return seq_to_file.size();
}