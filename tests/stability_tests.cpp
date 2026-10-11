#include <algorithm>
#include <ally/audio/resample.hpp>
#include <ally/media/stream.hpp>
#include <ally/project.hpp>
#include <ally/recording.hpp>
#include <ally/stream.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <thread>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
void verify(const std::filesystem::path &root) {
    using namespace ally;
    for (uint32_t rate : {8000u, 44100u, 96000u, 384000u, 44101u}) {
        std::vector<float> input(size_t(rate) * (rate == 44101 ? 3 : 1) * 2);
        for (size_t i = 0; i < input.size() / 2; ++i) {
            input[i * 2] = float(
                .3 * std::sin(2 * std::numbers::pi * (rate == 96000 ? 32000 : 1750) * double(i) / rate));
            input[i * 2 + 1] = float(.2 * std::sin(2 * std::numbers::pi * 2150 * double(i) / rate));
        }
        auto path = root / (std::to_string(rate) + ".wav");
        {
            media::WavWriter writer(path, rate, 2);
            for (size_t at = 0; at < input.size(); at += 8192) {
                writer.append(std::span(input).subspan(at, std::min<size_t>(8192, input.size() - at)));
            }
            writer.finish();
        }
        const auto reference = audio::resampleStereo(input, rate, 48000);
        auto stream = openStream(path);
        CHECK(stream->frames() == reference.size() / 2);
        double error = 0;
        // Deliberately non-sequential seeks and cache/chunk boundaries, including both EOF edges.
        for (uint64_t start : {28672u, 0u, 45056u, 4096u, 16384u, 32768u, 40960u, 8192u, 12288u, 20480u,
                               24576u, 36864u, 65536u, 98304u, 143360u}) {
            if (start >= stream->frames()) {
                continue;
            }
            stream->prefetch(start, true);
            for (uint64_t frame = start; frame < std::min(start + 4096, stream->frames()); ++frame) {
                float l = 0, r = 0;
                CHECK(stream->read(frame, l, r));
                error = std::max({error, std::abs(double(l) - reference[size_t(frame) * 2]),
                                  std::abs(double(r) - reference[size_t(frame) * 2 + 1])});
            }
        }
        CHECK(error < 2e-5);
        std::cout << "Seek/chunk/EOF " << rate << " -> 48000 maximum error=" << error << '\n';
    }
    audio::Engine engine;
    Recorder recorder(engine);
    recorder.start({{0, 0, root / "gap.wav"}});
    std::array<float, 512> input{};
    input.fill(.25f);
    recorder.capture(input, 2);
    recorder.gap(777);
    recorder.capture(input, 2);
    const auto take = recorder.finish();
    CHECK(take[0].frames == 1289 && recorder.lostFrames() == 777 && recorder.writtenFrames() == 1289);
    auto pcm = media::readWav(take[0].path);
    for (size_t i = 0; i < pcm.frames(); ++i) {
        CHECK(pcm.samples[i * 2] == (i >= 256 && i < 1033 ? 0 : .25f));
    }
    recorder.start({{0, 0, root / "prefix-gap.wav"}});
    recorder.gap(64);
    recorder.capture(input, 2);
    CHECK(recorder.firstPacketOffsetFrames() == 64);
    CHECK(recorder.finish()[0].frames == 320);
    recorder.start({{0, 0, root / "unknown.wav"}});
    recorder.capture(input, 2);
    // Give the worker time to commit the valid prefix before simulating an unknown native gap.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!recorder.writtenFrames() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(recorder.writtenFrames() == 256);
    recorder.gap(0);
    bool failed = false;
    try {
        recorder.finish();
    } catch (...) {
        failed = true;
    }
    CHECK(failed && recorder.failed() && !std::filesystem::exists(root / "unknown.wav"));
    CHECK(media::recoverRecording(root / "unknown.wav.partial.wav", root / "salvaged.wav") == 256);
    // Disk worker failure is visible before pressing Stop.
    recorder.start({{0, 0, root / "failed.wav"}});
    input[0] = std::numeric_limits<float>::quiet_NaN();
    recorder.capture(input, 2);
    const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!recorder.failed() && std::chrono::steady_clock::now() < failureDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(recorder.failed() && !recorder.error().empty());
    try {
        recorder.finish();
    } catch (...) {
    }
    CHECK(recordingStartFrame(24000, 10000, 10800, 256, 48000, 4, 8, 240, true, 48) == 23680);
    CHECK(recordingStartFrame(0, 10000, 10256, 256, 48000, 4, 8, 240, false, -48) == 48);
    {
        std::ofstream legacy(root / "v6.ally");
        legacy << "ALLY_PROJECT 6\n1 120 1 .8 0 1 0 0\n32 24\n";
        const char *strip = "0 0 0 0 0 4 0 .25 .3 -1 0 0 0 0\n";
        legacy << strip;
        for (int bus = 0; bus < 4; ++bus) {
            legacy << "1 " << strip;
        }
        legacy << "\"Legacy\" .65 0 0 16000 .002 .005 1 1 0\n"
                  "0 1 60 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
        legacy << strip << "1 \"Pattern\" 0\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
        legacy.close();
        auto migrated = Project::load(root / "v6.ally");
        CHECK(migrated.valid() && migrated.automaticRecordLatency && migrated.recordOffsetFrames == 0);
        CHECK(migrated.tracks[0].strip.lookaheadFrames == 0);
    }
    auto project = Project::empty();
    project.recordOffsetFrames = -72;
    project.tracks[0].strip.lookaheadFrames = 240;
    project.timelineBeats = 12000;
    project.save(root / "long.ally");
    CHECK(Project::load(root / "long.ally") == project);
    CHECK(project.compile(48000).clickIntervalFrames > 0);
    project.timelineBeats = 4;
    project.loopBeats = 4;
    project.importWav(take[0].path, 0);
    renderProject(project, root / "pdc-render.wav");
    auto rendered = media::readWav(root / "pdc-render.wav");
    CHECK(rendered.frames() == project.compile(48000).loopFrames);
    CHECK(std::abs(rendered.samples[0] - .25f * .65f * .78f) < 1e-6f);
    std::cout << "Known/unknown input gaps, interrupted take recovery, early worker failure, recording "
                 "timing, long arrangement/v7 and export latency trimming passed\n";
}
void pressure(const std::filesystem::path &root) {
    ally::audio::Engine engine;
    ally::Recorder recorder(engine);
    std::vector<ally::RecordRoute> routes;
    for (size_t i = 0; i < 8; ++i) {
        routes.push_back({i, 0, root / (std::to_string(i) + ".wav")});
    }
    recorder.start(routes);
    std::array<float, 512> input{};
    input.fill(.25f);
    constexpr uint64_t blocks = 10000;
    for (uint64_t i = 0; i < blocks; ++i) {
        recorder.capture(input, 2);
    }
    const auto files = recorder.finish();
    CHECK(files.size() == 8 && recorder.capturedFrames() == blocks * 256);
    CHECK(recorder.lostFrames() > 0);
    for (const auto &file : files) {
        ally::media::WavReader reader(file.path);
        CHECK(reader.info().frames == blocks * 256);
        uint64_t zero = 0;
        for (uint64_t at = 0; at < reader.info().frames; at += 65536) {
            auto pcm = reader.read(at, 65536);
            for (size_t i = 0; i < pcm.frames(); ++i) {
                const auto value = pcm.samples[i * 2];
                CHECK((value == .25f || value == 0) && value == pcm.samples[i * 2 + 1]);
                zero += value == 0;
            }
        }
        CHECK(zero == recorder.lostFrames());
    }
    std::cout << "8-track accelerated overload passed: frames=" << recorder.capturedFrames()
              << " zero-filled lost=" << recorder.lostFrames()
              << " queue_peak=" << recorder.queueHighWaterFrames() << std::endl;
}
void native(const std::filesystem::path &root, int seconds, const std::string &device) {
    using namespace ally;
    CHECK(seconds >= 1 && seconds <= 3600);
    audio::Engine engine;
    Recorder recorder(engine);
    auto input = audio::makeNativeCapture();
    auto output = audio::makeNativeBackend();
    auto plan = std::make_unique<audio::Plan>();
    plan->loopFrames = uint64_t(48000) * (seconds + 2);
    plan->strips[0].lookaheadFrames = 240;
    CHECK(engine.submit(*plan));
    engine.play(true);
    std::string error;
    CHECK(output->start(engine, error));
    recorder.start({{0, 0, root / "stereo.wav"}, {1, 1, root / "left.wav"}, {2, 2, root / "right.wav"}});
    audio::CaptureConfig config;
    config.deviceId = device;
    if (!input->start(recorder, config, error)) {
        throw std::runtime_error(error);
    }
    const auto start = std::chrono::steady_clock::now();
    uint64_t lastFrames = 0;
    for (int second = 0; second < seconds; ++second) {
        std::this_thread::sleep_until(start + std::chrono::seconds(second + 1));
        const auto stats = input->stats();
        CHECK(stats.active && !stats.errorCode && !recorder.failed());
        CHECK(recorder.capturedFrames() > lastFrames);
        lastFrames = recorder.capturedFrames();
        if ((second + 1) % 30 == 0) {
            std::cout << "Native soak " << second + 1 << " s: " << lastFrames
                      << " frames / lost=" << recorder.lostFrames()
                      << " / queue peak=" << recorder.queueHighWaterFrames() << std::endl;
        }
    }
    input->stop();
    const auto files = recorder.finish();
    const auto stats = output->stats();
    output->stop();
    CHECK(files.size() == 3 && recorder.capturedFrames() >= uint64_t(seconds - 1) * 48000);
    CHECK(recorder.lostFrames() == 0 && recorder.writtenFrames() == recorder.capturedFrames());
    for (const auto &file : files) {
        media::WavReader reader(file.path);
        CHECK(reader.info().frames == recorder.capturedFrames());
        CHECK(reader.read(reader.info().frames - 1, 1).frames() == 1);
    }
    std::cout << "Native soak passed: seconds=" << seconds << " capture=" << recorder.capturedFrames()
              << " lost=" << recorder.lostFrames() << " queue_peak=" << recorder.queueHighWaterFrames()
              << " output_xruns=" << stats.underruns << " output_max_ms=" << double(stats.maxProcessNs) / 1e6
              << std::endl;
}
int main(int argc, char **argv) {
    const auto base = argc > 4 && std::string(argv[1]) == "--native" ? std::filesystem::path(argv[4])
                                                                     : std::filesystem::temp_directory_path();
    const auto root = base / ("ally-stability-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        if (argc > 3 && std::string(argv[1]) == "--native") {
            native(root, std::stoi(argv[2]), argv[3]);
        } else if (argc > 1 && std::string(argv[1]) == "--pressure") {
            pressure(root);
        } else {
            verify(root);
        }
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " (fixture " << root << ")\n";
        return 1;
    }
}
