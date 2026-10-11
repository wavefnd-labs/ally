#include <ally/audio/backend.hpp>
#include <ally/audio/capture.hpp>
#include <ally/audio/engine.hpp>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
struct Sink : ally::audio::CaptureSink {
    std::atomic<uint64_t> frames{0};
    void capture(std::span<const float> pcm, uint16_t channels) noexcept override {
        frames.fetch_add(pcm.size() / channels);
    }
};
int main(int argc, char **argv) {
    auto input = ally::audio::makeNativeCapture();
    Sink sink;
    std::string captureError;
    ally::audio::CaptureConfig invalid;
    invalid.bufferFrames = 1;
    if (input->start(sink, invalid, captureError) || captureError.empty()) {
        throw std::runtime_error("Invalid capture must fail");
    }
    input->stop();
    input->stop();
    if (argc > 1 && std::string(argv[1]) == "--capture-probe") {
        ally::audio::CaptureConfig config;
        if (argc > 2) {
            config.deviceId = argv[2];
        }
        for (auto device : input->devices(captureError)) {
            std::cout << "input " << device.id << ": " << device.name << '\n';
        }
        if (!input->start(sink, config, captureError)) {
            throw std::runtime_error(captureError);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        auto stats = input->stats();
        input->stop();
        if (!sink.frames.load() || stats.errorCode) {
            throw std::runtime_error("Capture probe has no frames");
        }
        std::cout << "Native input frames=" << sink.frames.load() << " xruns=" << stats.underruns << '\n';
        return 0;
    }
    auto backend = ally::audio::makeNativeBackend();
    ally::audio::Engine engine;
    std::string error;
    ally::audio::BackendConfig config;
    config.bufferFrames = 1;
    if (backend->start(engine, config, error) || error.empty() || backend->stats().active) {
        throw std::runtime_error("Invalid configuration must fail without starting an audio owner");
    }
    if (argc > 1) {
        auto devices = backend->devices(error);
        for (const auto &device : devices) {
            std::cout << device.id << ": " << device.name << '\n';
        }
        ally::audio::Plan plan;
        plan.count = 1;
        plan.notes[0] = {0, 24000, 60, 0, .2f};
        if (!engine.submit(plan)) {
            throw std::runtime_error("Probe plan rejected");
        }
        engine.play(true);
        for (uint32_t size : {128u, 512u, 256u}) {
            config.bufferFrames = size;
            if (!backend->start(engine, config, error)) {
                throw std::runtime_error(error);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            auto stats = backend->stats();
            if (!stats.active || !stats.processedFrames || stats.errorCode) {
                throw std::runtime_error("Audio probe did not process frames");
            }
            std::cout << "requested=" << size << " actual=" << stats.bufferFrames
                      << " frames=" << stats.processedFrames << " latency=" << stats.outputLatencyMs
                      << "ms xruns=" << stats.underruns << '\n';
            backend->stop();
        }
    }
    backend->stop();
    backend->stop();
    std::cout << "Native backend invalid configuration and idempotent shutdown passed\n";
}
