#include <algorithm>
#include <ally/audio/backend.hpp>
#include <ally/audio/engine.hpp>
#include <alsa/asoundlib.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
namespace ally::audio {
class AlsaBackend final : public Backend {
  public:
    ~AlsaBackend() override {
        stop();
    }
    std::string name() const override {
        return "ALSA / " + config_.deviceId + " / 48 kHz";
    }
    std::vector<Device> devices(std::string &error) override {
        std::vector<Device> result{{"default", "System default output"}};
        void **hints = nullptr;
        const int rc = snd_device_name_hint(-1, "pcm", &hints);
        if (rc < 0) {
            error = snd_strerror(rc);
            return result;
        }
        for (auto hint = hints; hint && *hint; ++hint) {
            char *id = snd_device_name_get_hint(*hint, "NAME");
            char *description = snd_device_name_get_hint(*hint, "DESC");
            char *direction = snd_device_name_get_hint(*hint, "IOID");
            if (id && std::string(id) != "default" && (!direction || std::string(direction) != "Input")) {
                std::string label = description ? description : id;
                std::replace(label.begin(), label.end(), '\n', ' ');
                result.push_back({id, label});
            }
            std::free(id);
            std::free(description);
            std::free(direction);
        }
        snd_device_name_free_hint(hints);
        return result;
    }
    bool start(Engine &engine, const BackendConfig &config, std::string &error) override {
        stop();
        if (config.bufferFrames < 32 || config.bufferFrames > 2048 || config.deviceId.empty()) {
            error = "Invalid audio device/buffer configuration";
            return false;
        }
        config_ = config;
        processed_ = underruns_ = maxProcess_ = 0;
        error_ = 0;
        int rc = snd_pcm_open(&pcm_, config.deviceId.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
        if (rc < 0) {
            error = snd_strerror(rc);
            pcm_ = nullptr;
            return false;
        }
        rc = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                                engine.sampleRate(), 0,
                                unsigned(uint64_t(config.bufferFrames) * 4 * 1000000 / engine.sampleRate()));
        if (rc < 0) {
            error = snd_strerror(rc);
            stop();
            return false;
        }
        snd_pcm_uframes_t size = 0, period = 0;
        snd_pcm_get_params(pcm_, &size, &period);
        rate_ = engine.sampleRate();
        period_ = uint32_t(period);
        latency_ = double(size) * 1000 / rate_;
        running_ = true;
        thread_ = std::jthread([this, &engine] {
            std::array<float, 4096> buffer{};
            while (running_.load()) {
                const auto begin = std::chrono::steady_clock::now();
                engine.process(std::span(buffer).first(config_.bufferFrames * 2));
                auto ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now() - begin)
                                       .count());
                maxProcess_.store(std::max(maxProcess_.load(), ns));
                processed_.fetch_add(config_.bufferFrames);
                size_t written = 0;
                while (written < config_.bufferFrames && running_.load()) {
                    auto result =
                        snd_pcm_writei(pcm_, buffer.data() + written * 2, config_.bufferFrames - written);
                    if (result < 0) {
                        if (result == -EPIPE || result == -ESTRPIPE) {
                            underruns_.fetch_add(1);
                        }
                        int recovered = snd_pcm_recover(pcm_, int(result), 1);
                        if (!running_.load() || recovered < 0) {
                            error_.store(recovered);
                            running_ = false;
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
    BackendStats stats() const noexcept override {
        return {
            rate_,         period_,        latency_, processed_.load(), underruns_.load(), maxProcess_.load(),
            error_.load(), running_.load()};
    }
    void stop() noexcept override {
        running_ = false;
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
    BackendConfig config_;
    uint32_t rate_ = 0, period_ = 0;
    double latency_ = 0;
    snd_pcm_t *pcm_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> processed_{0}, underruns_{0}, maxProcess_{0};
    std::atomic<int64_t> error_{0};
    std::jthread thread_;
};
std::unique_ptr<Backend> makeNativeBackend() {
    return std::make_unique<AlsaBackend>();
}
} // namespace ally::audio
