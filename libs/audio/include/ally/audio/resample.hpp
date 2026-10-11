#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace ally::audio {
// Non-RT stereo interleaved conversion. Optional Ally::Resampler target; no external API types.
std::vector<float> resampleStereo(std::span<const float> input, uint32_t inputRate, uint32_t outputRate);
} // namespace ally::audio
