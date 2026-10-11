#pragma once
#include <ally/media/wav.hpp>
#include <fstream>
#include <memory>

namespace ally::media {
struct WavInfo {
    uint32_t sampleRate = 0;
    uint16_t channels = 0, bits = 0, format = 0;
    uint64_t frames = 0, dataOffset = 0, dataBytes = 0;
};
// Independent bounded parser and seekable decoder. All methods are non-RT.
class WavReader {
  public:
    explicit WavReader(const std::filesystem::path &);
    const WavInfo &info() const {
        return info_;
    }
    Pcm read(uint64_t firstFrame, size_t frames);

  private:
    std::ifstream file_;
    WavInfo info_;
};
// Incremental IEEE float WAV writer, permits long captures without whole-file RAM.
class WavWriter {
  public:
    WavWriter(const std::filesystem::path &, uint32_t rate, uint16_t channels);
    ~WavWriter();
    void append(std::span<const float>);
    void finish();
    uint64_t frames() const {
        return frames_;
    }

  private:
    std::ofstream file_;
    uint32_t rate_;
    uint16_t channels_;
    uint64_t frames_ = 0;
    bool finished_ = false;
};
} // namespace ally::media
