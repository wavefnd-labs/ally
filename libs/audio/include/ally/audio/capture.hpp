#pragma once
#include <ally/audio/backend.hpp>
#include <span>
namespace ally::audio {
struct CaptureConfig {
    std::string deviceId = "default";
    uint32_t sampleRate = 48000, bufferFrames = 256;
    uint16_t channels = 2;
};
class CaptureSink {
  public:
    virtual ~CaptureSink() = default;
    // Exact missing device frames, or zero if the discontinuity length is unknown.
    virtual void gap(uint64_t) noexcept {}
    virtual void capture(std::span<const float> interleaved, uint16_t channels) noexcept = 0;
};
class Capture {
  public:
    virtual ~Capture() = default;
    virtual std::vector<Device> devices(std::string &error) = 0;
    virtual bool start(CaptureSink &, const CaptureConfig &, std::string &error) = 0;
    virtual void stop() noexcept = 0;
    virtual BackendStats stats() const noexcept = 0;
};
std::unique_ptr<Capture> makeNativeCapture();
} // namespace ally::audio
