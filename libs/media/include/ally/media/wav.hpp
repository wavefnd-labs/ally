#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ally::media {
struct Pcm {
    uint32_t sampleRate = 48000;
    uint16_t channels = 2;
    std::vector<float> samples; // Interleaved, finite normalized PCM.
    size_t frames() const {
        return channels ? samples.size() / channels : 0;
    }
};
// Throws std::runtime_error for malformed, unsupported or oversized input. Non-RT API.
Pcm decodeWav(std::span<const std::byte> bytes);
Pcm readWav(const std::filesystem::path &path);
std::vector<std::byte> encodeWav(const Pcm &pcm);
void writeWav(const std::filesystem::path &path, const Pcm &pcm);
} // namespace ally::media
