#pragma once
#include <ally/audio/capture.hpp>
#include <ally/audio/engine.hpp>
#include <filesystem>
#include <thread>
namespace ally {
struct RecordRoute {
    size_t track = 0;
    int inputChannel = 0;
    std::filesystem::path path;
};
struct RecordedFile {
    size_t track = 0;
    std::filesystem::path path;
    uint64_t frames = 0;
};
// Frame of the first recorded sample on the arrangement. Positive manual offset moves a take earlier.
// Native latency is an estimate until the device's loopback offset is measured by the user.
int64_t recordingStartFrame(uint64_t startFrame, uint64_t startClock, uint64_t firstClock,
                            uint32_t firstPacketFrames, uint32_t rate, double inputLatencyMs,
                            double outputLatencyMs, uint32_t graphLatency, bool automatic, int offsetFrames);
// Native callback -> bounded SPSC -> disk worker. Stop native capture before finish/destruction.
class Recorder final : public audio::CaptureSink {
  public:
    static constexpr uint64_t maxCaptureFrames = uint64_t(48000) * 21600;
    explicit Recorder(audio::Engine &);
    ~Recorder();
    void start(std::vector<RecordRoute> routes, bool captureImmediately = true);
    void beginCapture() noexcept;
    std::vector<RecordedFile> finish();
    void gap(uint64_t missingFrames) noexcept override;
    bool failed() const noexcept;
    uint64_t queueHighWaterFrames() const noexcept;
    uint64_t writtenFrames() const noexcept;
    uint64_t firstCaptureClock() const noexcept;
    uint32_t firstPacketFrames() const noexcept;
    uint64_t firstPacketOffsetFrames() const noexcept;
    void capture(std::span<const float>, uint16_t channels) noexcept override;
    void monitor(int track, int channel = 0) noexcept;
    uint64_t lostFrames() const noexcept;
    uint64_t capturedFrames() const noexcept;
    std::string error() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ally
