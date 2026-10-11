#include "thread_priority.hpp"
#include <algorithm>
#include <ally/audio/capture.hpp>
#include <alsa/asoundlib.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>
namespace ally::audio {
#ifdef ALLY_PIPEWIRE_DISCOVERY
std::vector<Device> pipewireInputs();
#endif
class AlsaCapture final : public Capture {
    snd_pcm_t *pcm_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_{0}, xruns_{0};
    std::atomic<int64_t> error_{0};
    std::atomic<uint32_t> priority_{0};
    uint32_t rate_ = 0, period_ = 0;
    double latencyMs_ = 0;

  public:
    ~AlsaCapture() override {
        stop();
    }
    std::vector<Device> devices(std::string &error) override {
        std::vector<Device> result{{"default", "System default input"}};
#ifdef ALLY_PIPEWIRE_DISCOVERY
        auto server = pipewireInputs();
        result.insert(result.end(), server.begin(), server.end());
#endif
        void **hints = nullptr;
        int rc = snd_device_name_hint(-1, "pcm", &hints);
        if (rc < 0) {
            error = snd_strerror(rc);
            return result;
        }
        for (auto hint = hints; hint && *hint; ++hint) {
            char *name = snd_device_name_get_hint(*hint, "NAME"),
                 *desc = snd_device_name_get_hint(*hint, "DESC"),
                 *direction = snd_device_name_get_hint(*hint, "IOID");
            if (name && std::string(name) != "default" &&
                (!direction || std::string(direction) != "Output")) {
                std::string label = desc ? desc : name;
                std::replace(label.begin(), label.end(), '\n', ' ');
                result.push_back({name, label});
            }
            std::free(name);
            std::free(desc);
            std::free(direction);
        }
        snd_device_name_free_hint(hints);
        return result;
    }
    bool start(CaptureSink &sink, const CaptureConfig &config, std::string &error) override {
        stop();
        if (config.deviceId.empty() || config.sampleRate != 48000 || config.bufferFrames < 32 ||
            config.bufferFrames > 2048 || !config.channels || config.channels > 8) {
            error = "Invalid capture configuration";
            return false;
        }
        int rc = snd_pcm_open(&pcm_, config.deviceId.c_str(), SND_PCM_STREAM_CAPTURE, 0);
        if (rc < 0) {
            error = snd_strerror(rc);
            pcm_ = nullptr;
            return false;
        }
        rc = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, config.channels,
                                config.sampleRate, 0,
                                unsigned(uint64_t(config.bufferFrames) * 4 * 1000000 / config.sampleRate));
        if (rc < 0) {
            error = snd_strerror(rc);
            stop();
            return false;
        }
        snd_pcm_uframes_t size = 0, period = 0;
        snd_pcm_get_params(pcm_, &size, &period);
        rate_ = config.sampleRate;
        period_ = uint32_t(period);
        latencyMs_ = double(size) * 1000 / rate_;
        frames_ = xruns_ = 0;
        error_ = 0;
        priority_ = 0;
        running_ = true;
        thread_ = std::thread([this, &sink, config] {
            std::array<float, 16384> buffer{};
            priority_ = requestAudioThreadPriority();
            while (running_.load()) {
                auto count = snd_pcm_readi(pcm_, buffer.data(), config.bufferFrames);
                if (count < 0) {
                    if (!running_.load()) {
                        break;
                    }
                    xruns_.fetch_add(1);
                    sink.gap(0); // ALSA recovery cannot report the exact number of discarded frames.
                    int rc = snd_pcm_recover(pcm_, int(count), 1);
                    if (rc < 0) {
                        error_ = rc;
                        running_ = false;
                    }
                    continue;
                }
                if (count > 0 && running_.load()) {
                    sink.capture(std::span(buffer).first(size_t(count) * config.channels), config.channels);
                    frames_.fetch_add(uint64_t(count));
                }
            }
        });
        return true;
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
    BackendStats stats() const noexcept override {
        return {rate_,         period_,         0,          frames_.load(),  xruns_.load(), 0,
                error_.load(), running_.load(), latencyMs_, priority_.load()};
    }
};
std::unique_ptr<Capture> makeNativeCapture() {
    return std::make_unique<AlsaCapture>();
}
} // namespace ally::audio
