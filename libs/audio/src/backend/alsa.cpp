#include <ally/audio/backend.hpp>
#include <ally/audio/engine.hpp>
#include <alsa/asoundlib.h>
#include <array>
#include <atomic>
#include <thread>

namespace ally::audio {
class AlsaBackend final : public Backend {
  public:
    ~AlsaBackend() override {
        stop();
    }
    std::string name() const override {
        return "ALSA / stereo float32 / 48 kHz";
    }
    bool start(Engine &engine, std::string &error) override {
        stop();
        int rc = snd_pcm_open(&pcm_, "default", SND_PCM_STREAM_PLAYBACK, 0);
        if (rc < 0) {
            error = snd_strerror(rc);
            pcm_ = nullptr;
            return false;
        }
        rc = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                                engine.sampleRate(), 0, 20000);
        if (rc < 0) {
            error = snd_strerror(rc);
            stop();
            return false;
        }
        running_.store(true);
        thread_ = std::jthread([this, &engine] {
            std::array<float, 512> buffer{};
            while (running_.load()) {
                engine.process(buffer);
                size_t written = 0;
                while (written < 256 && running_.load()) {
                    auto result = snd_pcm_writei(pcm_, buffer.data() + written * 2, 256 - written);
                    if (result < 0) {
                        if (!running_.load() || snd_pcm_recover(pcm_, int(result), 1) < 0) {
                            running_.store(false);
                            break;
                        }
                    } else if (result > 0) {
                        written += size_t(result);
                    }
                }
            }
        });
        return true;
    }
    void stop() noexcept override {
        running_.store(false);
        if (pcm_) {
            snd_pcm_drop(pcm_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        if (pcm_) {
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

  private:
    snd_pcm_t *pcm_ = nullptr;
    std::atomic<bool> running_{false};
    std::jthread thread_;
};
std::unique_ptr<Backend> makeNativeBackend() {
    return std::make_unique<AlsaBackend>();
}
} // namespace ally::audio
