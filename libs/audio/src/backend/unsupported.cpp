#include <ally/audio/backend.hpp>
namespace ally::audio {
class UnsupportedBackend final : public Backend {
  public:
    std::vector<Device> devices(std::string &error) override {
        error = "Native audio disabled or unsupported";
        return {};
    }
    BackendStats stats() const noexcept override {
        return {};
    }
    bool start(Engine &, const BackendConfig &, std::string &error) override {
        error = "Native audio I/O is not implemented for this platform; offline render is available";
        return false;
    }
    void stop() noexcept override {}
    std::string name() const override {
        return "Unsupported native audio backend";
    }
};
std::unique_ptr<Backend> makeNativeBackend() {
    return std::make_unique<UnsupportedBackend>();
}
} // namespace ally::audio
