#include <ally/audio/capture.hpp>
namespace ally::audio {
class NoCapture final : public Capture {
  public:
    std::vector<Device> devices(std::string &error) override {
        error = "Native capture disabled in this build";
        return {};
    }
    bool start(CaptureSink &, const CaptureConfig &, std::string &error) override {
        error = "Native capture disabled in this build";
        return false;
    }
    void stop() noexcept override {}
    BackendStats stats() const noexcept override {
        return {};
    }
};
std::unique_ptr<Capture> makeNativeCapture() {
    return std::make_unique<NoCapture>();
}
} // namespace ally::audio
