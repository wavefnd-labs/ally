#include <algorithm>
#include <ally/audio/resample.hpp>
#include <ally/media/stream.hpp>
#include <ally/project.hpp>
#include <ally/recording.hpp>
#include <ally/stream.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
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
    for (const uint32_t rate : {44100u, 48000u}) {
        // Exercise both an HQ stream larger than the cache and a 513-block tail
        // that would collide with the loop head in a plain modulo-512 cache.
        const auto path = root / ("loop-cache-" + std::to_string(rate) + ".wav");
        media::WavWriter writer(path, rate, 2);
        std::array<float, 8192> input{};
        input.fill(.125f);
        for (size_t block = 0; block < (rate == 48000 ? 513u : 1024u); ++block) {
            writer.append(input);
        }
        writer.finish();
        auto stream = openStream(path);
        stream->prefetch(512 * 4096, true);
        stream->prefetch(stream->frames() - 1, true);
        float left = 0, right = 0;
        CHECK(stream->read(0, left, right));
        CHECK(std::isfinite(left) && left > .05f && left < .15f && left == right);
        CHECK(stream->underruns() == 0);
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
void accelerated(const std::filesystem::path &root) {
    using namespace ally;
    constexpr uint64_t frames = Recorder::maxCaptureFrames;
    CHECK(std::filesystem::space(root).available > frames * 8 + uint64_t(2) * 1024 * 1024 * 1024);
    const auto started = std::chrono::steady_clock::now();
    audio::Engine engine;
    Recorder recorder(engine);
    recorder.start({{0, 0, root / "stereo.wav"}});
    const auto sample = [](uint64_t frame, size_t channel) {
        return float(int((frame * (channel ? 17 : 13) + channel * 47) % 2048) - 1024) / 2048;
    };
    std::array<float, 512> input{};
    for (uint64_t at = 0; at < frames; at += 256) {
        // Test-only producer backpressure: exercise the real writer without pretending
        // this artificial source is a real-time hardware callback or soak test.
        while (recorder.capturedFrames() - recorder.writtenFrames() > 1024 * 256) {
            CHECK(!recorder.failed());
            CHECK(std::chrono::steady_clock::now() - started < std::chrono::minutes(10));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (size_t i = 0; i < 256; ++i) {
            input[i * 2] = sample(at + i, 0);
            input[i * 2 + 1] = sample(at + i, 1);
        }
        recorder.capture(input, 2);
        if (at % (48000 * 60) == 0) {
            std::cout << "Accelerated data progress: frames=" << at << std::endl;
        }
    }
    recorder.capture(input, 2);
    const auto files = recorder.finish();
    CHECK(files.size() == 1 && recorder.capturedFrames() == frames && recorder.writtenFrames() == frames);
    CHECK(recorder.lostFrames() == 0);
    std::ifstream header(files[0].path, std::ios::binary);
    std::array<char, 4> magic{};
    header.read(magic.data(), magic.size());
    CHECK(std::string(magic.data(), magic.size()) == "RF64");
    uint64_t checked = 0;
    for (size_t route = 0; route < files.size(); ++route) {
        media::WavReader reader(files[route].path);
        CHECK(reader.info().frames == frames && reader.info().channels == (route ? 1 : 2));
        for (uint64_t at = 0; at < frames; at += 65536) {
            const auto pcm = reader.read(at, 65536);
            for (size_t i = 0; i < pcm.frames(); ++i) {
                for (size_t ch = 0; ch < reader.info().channels; ++ch) {
                    CHECK(pcm.samples[i * reader.info().channels + ch] ==
                          sample(at + i, route ? route - 1 : ch));
                }
            }
            checked += pcm.frames();
        }
    }
    const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::ofstream result(root / "result.txt");
    result << "accelerated_only=1\nwall_seconds=" << wall << "\nframes=" << frames
           << "\nreadback_frames=" << checked << "\nlost=0\nrf64=1\n";
    std::cout << "Accelerated six-hour DATA passed: frames=" << frames << " readback_frames=" << checked
              << " wall_s=" << wall << " lost=0 RF64=1; NOT six-hour real-time stability" << std::endl;
}
void native(const std::filesystem::path &root, int seconds, const std::string &device, size_t tracks) {
    using namespace ally;
    CHECK(seconds >= 1 && seconds <= 21600 && (tracks == 3 || tracks == 32));
    // Real disk space only; never create a sparse substitute for recorded PCM.
    const uint64_t target = uint64_t(seconds) * 48000;
    const uint64_t expectedBytes = target * (tracks == 3 ? 16 : 256);
    CHECK(std::filesystem::space(root).available > expectedBytes + uint64_t(2) * 1024 * 1024 * 1024);
    audio::Engine engine;
    Recorder recorder(engine);
    auto input = audio::makeNativeCapture();
    auto output = audio::makeNativeBackend();
    auto plan = std::make_unique<audio::Plan>();
    plan->loopFrames = uint64_t(48000) * (seconds + 2);
    plan->strips[0].lookaheadFrames = 240;
    std::vector<RecordRoute> routes;
    if (tracks == 3) {
        routes = {{0, 0, root / "stereo.wav"}, {1, 1, root / "left.wav"}, {2, 2, root / "right.wav"}};
    } else {
        // 32 separate 44.1 kHz disk streams exceed each stream's 16 MiB cache.
        // Stereo input is fanned out to 32 real writers; this is not 32-channel hardware.
        plan->loopFrames = 60 * 48000;
        plan->sampleCount = 32;
        for (size_t track = 0; track < tracks; ++track) {
            const auto source = root / ("source-" + std::to_string(track) + ".wav");
            media::WavWriter writer(source, 44100, 2);
            std::array<float, 16384> pcm{};
            for (uint64_t at = 0; at < uint64_t(60) * 44100; at += pcm.size() / 2) {
                const size_t frames = size_t(std::min<uint64_t>(pcm.size() / 2, uint64_t(60) * 44100 - at));
                for (size_t frame = 0; frame < frames; ++frame) {
                    const float value = float(.002 * std::sin(2 * std::numbers::pi * (100 + track * 19) *
                                                              double(at + frame) / 44100));
                    pcm[frame * 2] = value;
                    pcm[frame * 2 + 1] = value * .9f;
                }
                writer.append(std::span(pcm).first(frames * 2));
            }
            writer.finish();
            plan->streams[track] = engine.prepareStream(openStream(source));
            plan->samples[track] = {0, plan->loopFrames, uint8_t(track), uint32_t(track), 0, true};
            auto &strip = plan->strips[track];
            strip.lookaheadFrames = uint32_t(track % 4) * 96;
            strip.route = int(track % 4);
            strip.lowDb = -1;
            strip.highDb = 1;
            strip.thresholdDb = -36;
            strip.ratio = 2;
            strip.delayWet = .05f;
            strip.sends[(track + 1) % 4] = .1f;
            plan->gains[track] = 1.0f / 32;
            routes.push_back({track, 0, root / ("take-" + std::to_string(track) + ".wav")});
        }
        for (size_t bus = 0; bus < 4; ++bus) {
            plan->buses[bus].lookaheadFrames = 96;
            plan->buses[bus].route = bus < 3 ? int(bus + 1) : -1;
        }
        plan->master.lookaheadFrames = 48;
        engine.prefetch(*plan, 0, true);
    }
    CHECK(engine.submit(*plan));
    std::string error;
    CHECK(output->start(engine, error));
    recorder.start(std::move(routes), false);
    audio::CaptureConfig config;
    config.deviceId = device;
    if (!input->start(recorder, config, error)) {
        throw std::runtime_error(error);
    }
    engine.play(true);
    recorder.beginCapture();
    const auto start = std::chrono::steady_clock::now();
    const auto utc = std::time(nullptr);
    std::cout << "Native soak started: utc_epoch=" << utc << " seconds=" << seconds << " tracks=" << tracks
              << " target_frames=" << target << " device=" << std::quoted(device) << " fixture=" << root
              << std::endl;
    uint64_t lastFrames = 0;
    size_t tick = 0;
    while (recorder.capturedFrames() < target) {
        ++tick;
        std::this_thread::sleep_until(start + std::chrono::seconds(tick));
        const auto captureStats = input->stats();
        const auto outputStats = output->stats();
        CHECK(captureStats.active && !captureStats.errorCode && !recorder.failed());
        CHECK(outputStats.active && !outputStats.errorCode);
        CHECK(recorder.capturedFrames() > lastFrames);
        lastFrames = recorder.capturedFrames();
        // Physical clocks can drift: require the full PCM duration, not a rounded wall timer.
        CHECK(tick <= size_t(seconds) + std::max<size_t>(30, size_t(seconds) / 1000));
        if (tick % 30 == 0 || lastFrames >= target) {
            const auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::cout << "Native soak progress: elapsed_s=" << elapsed << " capture=" << lastFrames
                      << " written=" << recorder.writtenFrames() << " lost=" << recorder.lostFrames()
                      << " queue_peak=" << recorder.queueHighWaterFrames()
                      << " input_xruns=" << captureStats.underruns
                      << " output_xruns=" << outputStats.underruns
                      << " stream_misses=" << engine.streamUnderruns()
                      << " input_rt_priority=" << captureStats.realtimePriority
                      << " output_rt_priority=" << outputStats.realtimePriority
                      << " input_period=" << captureStats.bufferFrames
                      << " output_period=" << outputStats.bufferFrames
                      << " output_max_ms=" << double(outputStats.maxProcessNs) / 1e6 << std::endl;
        }
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    input->stop();
    const auto captureStats = input->stats();
    const auto files = recorder.finish();
    const auto stats = output->stats();
    output->stop();
    CHECK(files.size() == tracks && recorder.capturedFrames() >= target);
    CHECK(recorder.lostFrames() == 0 && recorder.writtenFrames() == recorder.capturedFrames());
    // Read every recorded PCM sample, including beyond 4 GiB, and compare every route with the reference.
    // Finish/readback time is reported separately from the continuous capture interval.
    media::WavReader reference(files[0].path);
    const auto frames = recorder.capturedFrames();
    uint64_t checked = 0;
    for (const auto &file : files) {
        media::WavReader reader(file.path);
        CHECK(reader.info().frames == frames && reader.info().sampleRate == 48000);
        CHECK(reader.info().channels == (tracks == 3 && file.track ? 1 : 2));
        for (uint64_t at = 0; at < frames; at += 65536) {
            auto pcm = reader.read(at, size_t(std::min<uint64_t>(65536, frames - at)));
            auto expected = reference.read(at, pcm.frames());
            CHECK(pcm.frames() == std::min<uint64_t>(65536, frames - at));
            for (size_t frame = 0; frame < pcm.frames(); ++frame) {
                for (size_t channel = 0; channel < pcm.channels; ++channel) {
                    const auto value = pcm.samples[frame * pcm.channels + channel];
                    CHECK(std::isfinite(value));
                    const size_t inputChannel = tracks == 3 && file.track ? file.track - 1 : channel;
                    CHECK(value == expected.samples[frame * 2 + inputChannel]);
                }
            }
            checked += pcm.frames();
        }
    }
    std::ofstream result(root / "result.txt");
    result << "capture_utc_epoch=" << utc << "\nrequested_seconds=" << seconds << "\ntracks=" << tracks
           << "\nwall_capture_seconds=" << std::setprecision(12) << elapsed << "\ncaptured_frames=" << frames
           << "\nwritten_frames=" << recorder.writtenFrames() << "\nlost_frames=" << recorder.lostFrames()
           << "\nqueue_peak_frames=" << recorder.queueHighWaterFrames()
           << "\ninput_xruns=" << captureStats.underruns << "\noutput_xruns=" << stats.underruns
           << "\nstream_misses=" << engine.streamUnderruns()
           << "\noutput_max_ms=" << double(stats.maxProcessNs) / 1e6
           << "\ninput_rt_priority=" << captureStats.realtimePriority
           << "\noutput_rt_priority=" << stats.realtimePriority
           << "\ninput_period_frames=" << captureStats.bufferFrames
           << "\noutput_period_frames=" << stats.bufferFrames << "\nreadback_frames=" << checked
           << "\npdc_frames=" << engine.latencyFrames() << '\n';
    result.close();
    CHECK(result);
    // A run with any discontinuity or playback miss cannot be called successful.
    CHECK(captureStats.underruns == 0 && stats.underruns == 0 && engine.streamUnderruns() == 0);
    std::cout << "Native soak passed: seconds=" << seconds << " tracks=" << tracks << " wall_s=" << elapsed
              << " capture=" << frames << " lost=" << recorder.lostFrames()
              << " queue_peak=" << recorder.queueHighWaterFrames() << " output_xruns=" << stats.underruns
              << " output_max_ms=" << double(stats.maxProcessNs) / 1e6 << " readback_frames=" << checked
              << std::endl;
}
int main(int argc, char **argv) {
    const bool nativeMode = argc > 1 && std::string(argv[1]) == "--native";
    const bool acceleratedMode = argc > 1 && std::string(argv[1]) == "--accelerated";
    const auto base = nativeMode && argc > 4        ? std::filesystem::path(argv[4])
                      : acceleratedMode && argc > 2 ? std::filesystem::path(argv[2])
                                                    : std::filesystem::temp_directory_path();
    const auto root = base / ("ally-stability-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        if (argc > 3 && std::string(argv[1]) == "--native") {
            native(root, std::stoi(argv[2]), argv[3], argc > 5 ? size_t(std::stoul(argv[5])) : 3);
        } else if (acceleratedMode) {
            accelerated(root);
        } else if (argc > 1 && std::string(argv[1]) == "--pressure") {
            pressure(root);
        } else {
            verify(root);
        }
        if (!nativeMode && !acceleratedMode) {
            std::filesystem::remove_all(root);
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " (fixture " << root << ")\n";
        return 1;
    }
}
