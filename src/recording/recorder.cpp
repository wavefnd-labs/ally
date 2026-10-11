#include <algorithm>
#include <ally/file.hpp>
#include <ally/media/stream.hpp>
#include <ally/recording.hpp>
#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
namespace ally {
struct Recorder::Impl {
    struct Block {
        uint64_t first = 0;
        uint16_t channels = 0, frames = 0;
        std::array<float, 2048> samples{};
    };
    audio::Engine &engine;
    std::unique_ptr<audio::SpscQueue<Block, 1024>> queue = std::make_unique<audio::SpscQueue<Block, 1024>>();
    std::thread thread;
    std::atomic<bool> accepting{false}, stopping{false};
    std::atomic<uint64_t> captured{0}, lost{0};
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
void Recorder::start(std::vector<RecordRoute> routes) {
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
    impl_->captured = impl_->lost = 0;
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
            auto write = [&](const Impl::Block &block) {
                for (size_t i = 0; i < routes.size(); ++i) {
                    const int channel = routes[i].inputChannel;
                    std::array<float, 512> samples{};
                    const size_t channels = channel ? 1 : 2;
                    for (size_t frame = 0; frame < block.frames; ++frame) {
                        for (size_t ch = 0; ch < channels; ++ch) {
                            size_t input = channel ? size_t(channel - 1) : ch;
                            if (input < block.channels) {
                                samples[frame * channels + ch] =
                                    block.samples[frame * block.channels + input];
                            }
                        }
                    }
                    writers[i]->append(std::span(samples).first(size_t(block.frames) * channels));
                }
            };
            Impl::Block block;
            while (true) {
                if (impl_->queue->pop(block)) {
                    while (written < block.first) {
                        Impl::Block silence;
                        silence.frames = uint16_t(std::min<uint64_t>(256, block.first - written));
                        silence.channels = 2;
                        write(silence);
                        written += silence.frames;
                    }
                    write(block);
                    written += block.frames;
                } else if (impl_->stopping.load()) {
                    break;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            // Trailing overflow must preserve capture duration as well.
            while (written < impl_->captured.load()) {
                Impl::Block silence;
                silence.frames = uint16_t(std::min<uint64_t>(256, impl_->captured.load() - written));
                silence.channels = 2;
                write(silence);
                written += silence.frames;
            }
            for (size_t i = 0; i < writers.size(); ++i) {
                writers[i]->finish();
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
        }
    });
    try {
        future.get();
        impl_->accepting = true;
    } catch (...) {
        impl_->stopping = true;
        impl_->thread.join();
        throw;
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
void Recorder::capture(std::span<const float> samples, uint16_t channels) noexcept {
    if (!channels || channels > 8 || samples.size() % channels) {
        return;
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
        block.first = impl_->captured.fetch_add(count);
        block.channels = channels;
        block.frames = count;
        std::copy_n(samples.data() + first * channels, size_t(count) * channels, block.samples.data());
        if (!impl_->queue->push(block)) {
            impl_->lost.fetch_add(count);
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
