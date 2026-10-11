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
// Native callback -> bounded SPSC -> disk worker. Stop native capture before finish/destruction.
class Recorder final : public audio::CaptureSink {
  public:
    explicit Recorder(audio::Engine &);
    ~Recorder();
    void start(std::vector<RecordRoute> routes);
    std::vector<RecordedFile> finish();
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
