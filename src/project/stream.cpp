#include <algorithm>
#include <ally/audio/resample.hpp>
#include <ally/media/stream.hpp>
#include <ally/stream.hpp>
#include <chrono>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <thread>

namespace ally {
namespace {
class DiskStream final : public audio::SampleStream {
    static constexpr size_t block = 4096, slots = 64;
    static constexpr uint64_t absent = UINT64_MAX;
    struct Slot {
        // 0 empty, 1 worker writing, 2 ready, 3 RT/non-RT reader pinned.
        std::atomic<int> state{0};
        std::atomic<uint64_t> requested{absent};
        std::atomic<uint64_t> tag{absent};
        std::array<float, block * 2> pcm{};
    };
    media::WavReader reader_;
    std::unique_ptr<std::array<Slot, slots>> cache_;
    std::atomic<bool> stop_{false}, failed_{false};
    std::atomic<uint64_t> misses_{0};
    std::thread worker_;
    uint64_t frames_;
    bool available(uint64_t chunk) {
        auto &slot = (*cache_)[size_t(chunk % slots)];
        int ready = 2;
        if (!slot.state.compare_exchange_strong(ready, 3, std::memory_order_acquire)) {
            return false;
        }
        bool found = slot.tag.load() == chunk;
        slot.state.store(2, std::memory_order_release);
        return found;
    }
    void decode(uint64_t chunk, std::array<float, block * 2> &destination) {
        destination.fill(0);
        const auto rate = reader_.info().sampleRate;
        const uint64_t target = chunk * block;
        if (rate == 48000) {
            auto pcm = reader_.read(target, block);
            for (size_t i = 0; i < pcm.frames(); ++i) {
                for (size_t ch = 0; ch < 2; ++ch) {
                    destination[i * 2 + ch] = std::clamp(
                        pcm.samples[i * pcm.channels + std::min(ch, size_t(pcm.channels - 1))], -1.0f, 1.0f);
                }
            }
            return;
        }
        // Align a seek to the rational rate's phase period. Warm both sides of the HQ FIR;
        // starting a converter at the requested sample would introduce a new edge transient.
        constexpr uint64_t halo = 8192;
        const uint64_t period = 48000 / std::gcd(rate, 48000u);
        const uint64_t origin = (target > halo ? (target - halo) / period : 0) * period;
        uint64_t inputFrame = origin * rate / 48000;
        const uint64_t inputEnd =
            std::min(reader_.info().frames, ((target + block + halo) * rate + 47999) / 48000);
        audio::StreamingResampler converter(rate, 48000);
        std::array<float, block * 2> output{};
        uint64_t emitted = origin;
        auto collect = [&](size_t count) {
            const auto first = std::max(target, emitted);
            const auto end = std::min(target + block, emitted + count);
            for (uint64_t frame = first; frame < end; ++frame) {
                for (size_t ch = 0; ch < 2; ++ch) {
                    destination[size_t(frame - target) * 2 + ch] =
                        std::clamp(output[size_t(frame - emitted) * 2 + ch], -1.0f, 1.0f);
                }
            }
            emitted += count;
        };
        while (inputFrame < inputEnd && emitted < target + block) {
            auto pcm = reader_.read(inputFrame, size_t(std::min<uint64_t>(65536, inputEnd - inputFrame)));
            std::vector<float> stereo(pcm.frames() * 2);
            for (size_t i = 0; i < pcm.frames(); ++i) {
                for (size_t ch = 0; ch < 2; ++ch) {
                    stereo[i * 2 + ch] = std::clamp(
                        pcm.samples[i * pcm.channels + std::min(ch, size_t(pcm.channels - 1))], -1.0f, 1.0f);
                }
            }
            size_t consumed = 0;
            while (consumed < pcm.frames() && emitted < target + block) {
                const auto result = converter.process(std::span(stereo).subspan(consumed * 2), output);
                if (!result.inputFrames && !result.outputFrames) {
                    throw std::runtime_error("Streaming resampler made no progress");
                }
                consumed += result.inputFrames;
                collect(result.outputFrames);
            }
            inputFrame += consumed;
        }
        while (emitted < target + block) {
            const auto result = converter.process({}, output, true);
            if (!result.outputFrames) {
                break;
            }
            collect(result.outputFrames);
        }
    }
    void work() noexcept {
        try {
            while (!stop_.load()) {
                bool progress = false;
                for (auto &slot : *cache_) {
                    const auto chunk = slot.requested.load();
                    if (chunk == absent || chunk * block >= frames_) {
                        continue;
                    }
                    if (slot.tag.load() == chunk) {
                        continue;
                    }
                    int ready = 2;
                    if (slot.state.compare_exchange_strong(ready, 1, std::memory_order_acquire)) {
                        if (slot.tag.load() == chunk) {
                            slot.state.store(2, std::memory_order_release);
                            continue;
                        }
                    } else {
                        ready = 0;
                        if (!slot.state.compare_exchange_strong(ready, 1, std::memory_order_acquire)) {
                            continue;
                        }
                    }
                    decode(chunk, slot.pcm);
                    slot.tag = chunk;
                    slot.state.store(2, std::memory_order_release);
                    progress = true;
                }
                if (!progress) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        } catch (...) {
            failed_ = true;
        }
    }

  public:
    explicit DiskStream(const std::filesystem::path &path)
        : reader_(path), cache_(std::make_unique<std::array<Slot, slots>>()),
          frames_(uint64_t((reader_.info().frames * 48000 + reader_.info().sampleRate / 2) /
                           reader_.info().sampleRate)) {
        if (!frames_ || reader_.info().channels > 2) {
            throw std::runtime_error("Stream playback requires non-empty converted mono/stereo WAV");
        }
        worker_ = std::thread([this] { work(); });
        try {
            prefetch(0, true);
        } catch (...) {
            stop_ = true;
            worker_.join();
            throw;
        }
    }
    ~DiskStream() override {
        stop_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    uint64_t frames() const noexcept override {
        return frames_;
    }
    void prefetch(uint64_t frame, bool wait) override {
        if (frame >= frames_) {
            return;
        }
        const auto chunk = frame / block;
        for (uint64_t i = chunk; i < chunk + 8 && i * block < frames_; ++i) {
            (*cache_)[size_t(i % slots)].requested.store(i);
        }
        if (wait) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!available(chunk)) {
                if (failed_.load() || std::chrono::steady_clock::now() > deadline) {
                    throw std::runtime_error("Disk stream prefetch failed");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    bool read(uint64_t frame, float &left, float &right) noexcept override {
        left = right = 0;
        if (frame >= frames_) {
            return true;
        }
        const auto chunk = frame / block;
        if (!(frame % block)) {
            prefetch(frame, false);
        }
        auto &slot = (*cache_)[size_t(chunk % slots)];
        int ready = 2;
        if (slot.state.compare_exchange_strong(ready, 3, std::memory_order_acquire)) {
            if (slot.tag.load() == chunk) {
                const auto at = size_t(frame % block) * 2;
                left = slot.pcm[at];
                right = slot.pcm[at + 1];
                slot.state.store(2, std::memory_order_release);
                return true;
            }
            slot.state.store(2, std::memory_order_release);
        }
        prefetch(frame, false);
        misses_.fetch_add(1);
        return false;
    }
    uint64_t underruns() const noexcept override {
        return misses_.load();
    }
};
} // namespace
std::shared_ptr<audio::SampleStream> openStream(const std::filesystem::path &path) {
    return std::make_shared<DiskStream>(path);
}
} // namespace ally
