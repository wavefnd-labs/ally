#include <ally/audio/backend.hpp>
namespace ally::audio {
class UnsupportedBackend final : public Backend {
  public:
    bool start(Engine &, std::string &error) override {
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
