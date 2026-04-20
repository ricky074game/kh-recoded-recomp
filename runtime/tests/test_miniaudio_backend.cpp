#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "miniaudio_backend.h"

TEST(MiniaudioBackendTest, UnderrunReturnsSilence) {
    Backend::Audio::SetupRingBuffer();
    Backend::Audio::ClearBufferedAudio();

    std::array<int16_t, 64> output{};
    std::fill(output.begin(), output.end(), static_cast<int16_t>(1234));

    Backend::Audio::StreamCallback(nullptr, output.data(), nullptr, 32);
    for (int16_t sample : output) {
        EXPECT_EQ(sample, 0);
    }
}

TEST(MiniaudioBackendTest, PushAndConsumeFrames) {
    Backend::Audio::SetupRingBuffer();
    Backend::Audio::ClearBufferedAudio();

    std::array<int16_t, 128> input{};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<int16_t>(i * 13);
    }

    ASSERT_TRUE(Backend::Audio::PushInterleavedSamples(input.data(), 64));
    EXPECT_GE(Backend::Audio::GetBufferedFrames(), 64u);

    std::array<int16_t, 128> output{};
    Backend::Audio::StreamCallback(nullptr, output.data(), nullptr, 64);

    EXPECT_EQ(output[0], input[0]);
    EXPECT_EQ(output[1], input[1]);
    EXPECT_EQ(output[126], input[126]);
    EXPECT_EQ(output[127], input[127]);
}