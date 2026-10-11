#include <algorithm>
#include <ally/file.hpp>
#include <ally/media/stream.hpp>
#include <ally/recording.hpp>
#include <chrono>
#include <cmath>
#include <future>
#include <mutex>
#include <stdexcept>
namespace ally {
int64_t recordingStartFrame(uint64_t startFrame, uint64_t startClock, uint64_t firstClock,
                            uint32_t packetFrames, uint32_t rate, double inputMs, double outputMs,
                            uint32_t graphLatency, bool automatic, int offsetFrames) {
    if (!rate || !std::isfinite(inputMs) || !std::isfinite(outputMs) || inputMs < 0 || outputMs < 0 ||
        inputMs > 2000 || outputMs > 2000 || startFrame > INT64_MAX || startClock > INT64_MAX ||
        firstClock > INT64_MAX || offsetFrames < -48000 || offsetFrames > 48000) {
        throw std::runtime_error("Invalid recording timing");
    }
    const int64_t elapsed = int64_t(firstClock) - int64_t(startClock);
    if (std::abs(elapsed) > int64_t(rate) * 60) {
        throw std::runtime_error("First recording packet is outside the transport anchor");
    }
    const int64_t correction =
        automatic ? int64_t(std::llround((inputMs + outputMs) * rate / 1000)) + graphLatency : 0;
    return int64_t(startFrame) + elapsed - packetFrames - correction - offsetFrames;
}
struct Recorder::Impl {
    struct Block {
        uint64_t first = 0;
        uint16_t channels = 0, frames = 0;
        std::array<float, 2048> samples{};
    };
    audio::Engine &engine;
    // About 64 MiB, prepared outside capture: 43.7 s of 48 kHz input backlog.
    // 32 independent files can stall the disk worker during filesystem writeback.
    std::unique_ptr<audio::SpscQueue<Block, 8192>> queue = std::make_unique<audio::SpscQueue<Block, 8192>>();
    std::thread thread;
    std::atomic<bool> accepting{false}, stopping{false}, failed{false}, unknownGap{false};
    std::atomic<uint64_t> captured{0}, lost{0}, written{0}, highWater{0}, firstClock{UINT64_MAX};
    std::atomic<uint32_t> firstPacket{0};
    std::atomic<uint64_t> firstOffset{0};
    std::atomic<int> monitorTrack{-1}, monitorChannel{0};
    std::mutex errorMutex;
    std::string failure;
    std::vector<RecordedFile> files;
    explicit Impl(audio::Engine &e) : engine(e) {}
};
Recorder::Recorder(audio::Engine &engine) : impl_(std::make_unique<Impl>(engine)) {}
Recorder::~Recorder() {
    impl_->accepting = false;
    impl_->stopping = true;
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}
void Recorder::monitor(int track, int channel) noexcept {
    impl_->monitorTrack = track;
    impl_->monitorChannel = channel;
    impl_->engine.monitorTrack(track);
}
void Recorder::start(std::vector<RecordRoute> routes, bool captureImmediately) {
    if (impl_->thread.joinable() || routes.empty() || routes.size() > audio::Plan::maxTracks) {
        throw std::runtime_error("Invalid or active recording session");
    }
    if (impl_->engine.sampleRate() != 48000) {
        throw std::runtime_error("Recording requires a 48 kHz engine");
    }
    for (size_t i = 0; i < routes.size(); ++i) {
        const auto &route = routes[i];
        if (route.track >= audio::Plan::maxTracks || route.inputChannel < 0 || route.inputChannel > 8 ||
            route.path.empty() || std::filesystem::exists(route.path)) {
            throw std::runtime_error("Invalid recording route or existing destination");
        }
        for (size_t j = 0; j < i; ++j) {
            if (route.track == routes[j].track ||
                std::filesystem::absolute(route.path).lexically_normal() ==
                    std::filesystem::absolute(routes[j].path).lexically_normal()) {
                throw std::runtime_error("Duplicate recording track or destination");
            }
        }
    }
    impl_->captured = impl_->lost = impl_->written = impl_->highWater = 0;
    impl_->failed = impl_->unknownGap = false;
    impl_->firstClock = UINT64_MAX;
    impl_->firstPacket = 0;
    impl_->firstOffset = 0;
    impl_->stopping = false;
    impl_->failure.clear();
    impl_->files.clear();
    Impl::Block old;
    while (impl_->queue->pop(old)) {
    }
    auto ready = std::make_shared<std::promise<void>>();
    auto future = ready->get_future();
    impl_->thread = std::thread([this, routes = std::move(routes), ready] {
        bool signalled = false;
        try {
            std::vector<std::unique_ptr<media::WavWriter>> writers;
            std::vector<std::filesystem::path> partials;
            for (const auto &route : routes) {
                auto partial = route.path;
                partial += ".partial.wav";
                if (std::filesystem::exists(partial)) {
                    throw std::runtime_error("Recording partial already exists; preserve/recover it first");
                }
                partials.push_back(partial);
                writers.push_back(
                    std::make_unique<media::WavWriter>(partial, 48000, route.inputChannel ? 1 : 2));
            }
            ready->set_value();
            signalled = true;
            uint64_t written = 0;
            constexpr size_t batchFrames = 8192;
            std::vector<std::vector<float>> batches(routes.size());
            for (size_t i = 0; i < routes.size(); ++i) {
                batches[i].resize(batchFrames * (routes[i].inputChannel ? 1 : 2));
            }
            size_t pending = 0;
            auto flush = [&] {
                if (!pending) {
                    return;
                }
                for (size_t i = 0; i < routes.size(); ++i) {
                    writers[i]->append(
                        std::span(batches[i]).first(pending * (routes[i].inputChannel ? 1 : 2)));
                }
                impl_->written = written;
                pending = 0;
            };
            auto write = [&](const Impl::Block &block) {
                for (size_t i = 0; i < routes.size(); ++i) {
                    const int channel = routes[i].inputChannel;
                    auto &samples = batches[i];
                    const size_t channels = channel ? 1 : 2;
                    for (size_t frame = 0; frame < block.frames; ++frame) {
                        for (size_t ch = 0; ch < channels; ++ch) {
                            size_t input = channel ? size_t(channel - 1) : ch;
                            samples[(pending + frame) * channels + ch] =
                                input < block.channels ? block.samples[frame * block.channels + input] : 0;
                        }
                    }
                }
                pending += block.frames;
            };
            auto lastFlush = std::chrono::steady_clock::now();
            Impl::Block block;
            while (true) {
                if (impl_->unknownGap.load()) {
                    flush();
                    throw std::runtime_error(
                        "Native input discontinuity has unknown duration; recover partial take");
                }
                if (pending + 256 > batchFrames) {
                    flush();
                    lastFlush = std::chrono::steady_clock::now();
                }
                if (impl_->queue->pop(block)) {
                    while (written < block.first) {
                        Impl::Block silence;
                        silence.frames = uint16_t(std::min<uint64_t>(256, block.first - written));
                        silence.channels = 2;
                        write(silence);
                        written += silence.frames;
                        if (pending + 256 > batchFrames) {
                            flush();
                        }
                    }
                    write(block);
                    written += block.frames;
                } else if (impl_->stopping.load()) {
                    break;
                } else {
                    if (std::chrono::steady_clock::now() - lastFlush > std::chrono::milliseconds(200)) {
                        flush();
                        lastFlush = std::chrono::steady_clock::now();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            // Trailing overflow must preserve capture duration as well.
            while (written < impl_->captured.load()) {
                if (pending + 256 > batchFrames) {
                    flush();
                }
                Impl::Block silence;
                silence.frames = uint16_t(std::min<uint64_t>(256, impl_->captured.load() - written));
                silence.channels = 2;
                write(silence);
                written += silence.frames;
            }
            flush();
            for (size_t i = 0; i < writers.size(); ++i) {
                writers[i]->finish();
                if (std::filesystem::exists(routes[i].path)) {
                    throw std::runtime_error("Recording destination appeared during take; partial preserved");
                }
                replaceFile(partials[i], routes[i].path);
                impl_->files.push_back({routes[i].track, routes[i].path, written});
            }
        } catch (...) {
            if (!signalled) {
                ready->set_exception(std::current_exception());
            }
            try {
                throw;
            } catch (const std::exception &e) {
                std::lock_guard lock(impl_->errorMutex);
                impl_->failure = e.what();
            }
            impl_->accepting = false;
            impl_->failed = true;
        }
    });
    try {
        future.get();
        impl_->accepting = captureImmediately;
    } catch (...) {
        impl_->stopping = true;
        impl_->thread.join();
        throw;
    }
}
void Recorder::beginCapture() noexcept {
    if (!impl_->failed.load() && !impl_->stopping.load()) {
        impl_->accepting = true;
    }
}
std::vector<RecordedFile> Recorder::finish() {
    impl_->accepting = false;
    impl_->stopping = true;
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    if (!error().empty()) {
        throw std::runtime_error("Recording failed; partial WAV preserved: " + error());
    }
    return impl_->files;
}
void Recorder::gap(uint64_t frames) noexcept {
    if (!impl_->accepting.load()) {
        return;
    }
    if (!frames || frames > uint64_t(48000) * 60) {
        impl_->unknownGap = true;
        impl_->accepting = false;
        impl_->failed = true;
        return;
    }
    frames = std::min(frames, maxCaptureFrames - impl_->captured.load());
    impl_->captured.fetch_add(frames);
    impl_->lost.fetch_add(frames);
}
bool Recorder::failed() const noexcept {
    return impl_->failed.load();
}
uint64_t Recorder::queueHighWaterFrames() const noexcept {
    return impl_->highWater.load();
}
uint64_t Recorder::writtenFrames() const noexcept {
    return impl_->written.load();
}
uint64_t Recorder::firstCaptureClock() const noexcept {
    return impl_->firstClock.load();
}
uint64_t Recorder::firstPacketOffsetFrames() const noexcept {
    return impl_->firstOffset.load();
}
uint32_t Recorder::firstPacketFrames() const noexcept {
    return impl_->firstPacket.load();
}
void Recorder::capture(std::span<const float> samples, uint16_t channels) noexcept {
    if (!channels || channels > 8 || samples.size() % channels) {
        return;
    }
    if (!samples.empty() && impl_->accepting.load() && impl_->firstClock.load() == UINT64_MAX) {
        impl_->firstPacket = uint32_t(samples.size() / channels);
        impl_->firstOffset = impl_->captured.load();
        impl_->firstClock = impl_->engine.timestamp();
    }
    for (size_t first = 0; first < samples.size() / channels; first += 256) {
        const auto count = uint16_t(std::min<size_t>(256, samples.size() / channels - first));
        if (impl_->monitorTrack.load() >= 0) {
            std::array<float, 512> stereo{};
            int mode = impl_->monitorChannel.load();
            for (size_t frame = 0; frame < count; ++frame) {
                for (size_t ch = 0; ch < 2; ++ch) {
                    const auto input = std::min(mode ? size_t(mode - 1) : ch, size_t(channels - 1));
                    stereo[frame * 2 + ch] = samples[(first + frame) * channels + input];
                }
            }
            impl_->engine.monitorInput(std::span(stereo).first(size_t(count) * 2));
        }
        if (!impl_->accepting.load()) {
            continue;
        }
        Impl::Block block;
        const auto recordedCount =
            uint16_t(std::min<uint64_t>(count, maxCaptureFrames - impl_->captured.load()));
        if (!recordedCount) {
            continue;
        }
        block.first = impl_->captured.fetch_add(recordedCount);
        block.channels = channels;
        block.frames = recordedCount;
        std::copy_n(samples.data() + first * channels, size_t(recordedCount) * channels,
                    block.samples.data());
        if (!impl_->queue->push(block)) {
            impl_->lost.fetch_add(recordedCount);
        } else {
            const auto depth = uint64_t(impl_->queue->size()) * 256;
            if (depth > impl_->highWater.load(std::memory_order_relaxed)) {
                impl_->highWater.store(depth, std::memory_order_relaxed);
            }
        }
    }
}
uint64_t Recorder::lostFrames() const noexcept {
    return impl_->lost.load();
}
uint64_t Recorder::capturedFrames() const noexcept {
    return impl_->captured.load();
}
std::string Recorder::error() const {
    std::lock_guard lock(impl_->errorMutex);
    return impl_->failure;
}
} // namespace ally
