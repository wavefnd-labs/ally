#pragma once
#include <ally/media/wav.hpp>
#include <cstdio>
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
// Incremental IEEE float WAV writer. Reserves ds64 space, automatically promotes to RF64.
// Checkpoints flush data before updating sizes and sync to disk every five seconds.
class WavWriter {
  public:
    WavWriter(const std::filesystem::path &, uint32_t rate, uint16_t channels);
    ~WavWriter();
    WavWriter(const WavWriter &) = delete;
    WavWriter &operator=(const WavWriter &) = delete;
    void append(std::span<const float>);
    void finish();
    uint64_t frames() const {
        return frames_;
    }

  private:
    std::FILE *file_ = nullptr;
    std::vector<unsigned char> bytes_;
    uint32_t rate_;
    uint16_t channels_;
    uint64_t frames_ = 0;
    bool finished_ = false;
};
// Salvage complete frames from Ally's own interrupted writer; never modifies the source.
uint64_t recoverRecording(const std::filesystem::path &partial, const std::filesystem::path &destination);
} // namespace ally::media
