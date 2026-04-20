
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "hw_audio.h"
#include "miniaudio_backend.h"

TEST(HWAudioTest, LoadMapParsesValidEntries) {
    AudioManager audio;

    const std::filesystem::path temp_map =
        std::filesystem::temp_directory_path() / "kh_recoded_audio_map_test.txt";

    std::ofstream out(temp_map);
    ASSERT_TRUE(out.is_open());
    out << "1 = bgm/field.ogg\n";
    out << "bad_line_without_delim\n";
    out << "# comment\n";
    out << "2=bgm/boss.ogg\n";
    out << "not_a_number = ignored.ogg\n";
    out.close();

    audio.LoadMap(temp_map.string());
    EXPECT_EQ(audio.GetMappedSequenceCount(), 2u);

    std::error_code ec;
    std::filesystem::remove(temp_map, ec);
}

TEST(HWAudioTest, TriggerAudioDMAProducesBufferedFrames) {
    Backend::Audio::SetupRingBuffer();
    Backend::Audio::ClearBufferedAudio();

    AudioManager audio;
    audio.SetChannelPanning(0, 96);
    audio.SetChannelPitch(0, 1.25f);
    audio.SetMasterVolume(0.75f);
    audio.TriggerAudioDMA();

    EXPECT_GT(Backend::Audio::GetBufferedFrames(), 0u);
}
