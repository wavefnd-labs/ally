#include <algorithm>
#include <ally/media/stream.hpp>
#include <ally/stream.hpp>
#include <chrono>
#include <cmath>
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
                    const double ratio = double(reader_.info().sampleRate) / 48000;
                    const auto first = uint64_t(std::floor(double(chunk * block) * ratio));
                    const auto count = size_t(std::ceil(block * ratio)) + 2;
                    auto pcm = reader_.read(first, count);
                    for (size_t i = 0; i < block; ++i) {
                        double position = double(chunk * block + i) * ratio - double(first);
                        auto at = size_t(std::floor(position));
                        for (size_t ch = 0; ch < 2; ++ch) {
                            const auto channel = std::min(ch, size_t(pcm.channels - 1));
                            float a = at < pcm.frames()
                                          ? std::clamp(pcm.samples[at * pcm.channels + channel], -1.0f, 1.0f)
                                          : 0;
                            float b =
                                at + 1 < pcm.frames()
                                    ? std::clamp(pcm.samples[(at + 1) * pcm.channels + channel], -1.0f, 1.0f)
                                    : a;
                            slot.pcm[i * 2 + ch] = std::lerp(a, b, float(position - double(at)));
                        }
                    }
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
          frames_(uint64_t(std::ceil(double(reader_.info().frames) * 48000 / reader_.info().sampleRate))) {
        if (reader_.info().channels > 2) {
            throw std::runtime_error("Stream playback requires mono/stereo WAV");
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
