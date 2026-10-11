#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
namespace ally::audio {
// Stateful HQ conversion on a non-real-time worker. Counts are frames, stereo interleaved.
// Submit all input, then repeatedly drain with end=true and empty input until outputFrames is zero.
class StreamingResampler {
  public:
    struct Result {
        size_t inputFrames = 0, outputFrames = 0;
    };
    StreamingResampler(uint32_t inputRate, uint32_t outputRate);
    ~StreamingResampler();
    StreamingResampler(const StreamingResampler &) = delete;
    StreamingResampler &operator=(const StreamingResampler &) = delete;
    Result process(std::span<const float> input, std::span<float> output, bool end = false);
    void reset();
    double bufferedFrames() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Non-RT stereo interleaved conversion. Optional Ally::Resampler target; no external API types.
std::vector<float> resampleStereo(std::span<const float> input, uint32_t inputRate, uint32_t outputRate);
} // namespace ally::audio
