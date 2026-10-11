#include <algorithm>
#include <ally/editing.hpp>
#include <ally/media/stream.hpp>
#include <ally/midi_take.hpp>
#include <ally/recording.hpp>
#include <ally/stream.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
static thread_local bool forbidNew = false;
void *operator new(std::size_t n) {
    if (forbidNew) {
        throw std::bad_alloc();
    }
    if (auto *p = std::malloc(n ? n : 1)) {
        return p;
    }
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void *p) noexcept {
    std::free(p);
}
void operator delete[](void *p) noexcept {
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    std::free(p);
}
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
void checkProduction(const std::filesystem::path &root) {
    using namespace ally;
    auto p = Project::empty();
    CHECK(p.valid() && p.tracks[0].pattern().notes.empty());
    p.tracks[0].pattern().notes = {{60, 0, .5, .7f}, {64, 1, .5, .8f}};
    Editing edit;
    edit.selectNote(0, false);
    edit.selectNote(1, true);
    edit.transform(p, 0, 1, 2, false);
    CHECK(p.tracks[0].pattern().notes[0].beat == 1 && p.tracks[0].pattern().notes[1].key == 66);
    edit.transform(p, 0, 100, 200, false); // Group clamps together, preserving spacing.
    CHECK(p.tracks[0].pattern().notes[0].beat == 2.5 && p.tracks[0].pattern().notes[1].beat == 3.5);
    CHECK(p.tracks[0].pattern().notes[1].key == 127 && p.tracks[0].pattern().notes[0].key == 123);
    edit.copy(p, 0);
    edit.paste(p, 0, 0);
    CHECK(p.tracks[0].pattern().notes.size() == 4);
    edit.erase(p, 0);
    CHECK(p.tracks[0].pattern().notes.size() == 2);
    edit.selectNote(0, false);
    edit.copy(p, 0);
    edit.notes = false; // Switching editor must not reinterpret the clipboard.
    auto noteDestination = Project::empty();
    edit.paste(noteDestination, 0, 0);
    CHECK(edit.notes && noteDestination.tracks[0].pattern().notes.size() == 1);
    edit.selectedNotes = {0, 9999};
    auto unchanged = noteDestination;
    bool stale = false;
    try {
        edit.erase(noteDestination, 0);
    } catch (const std::exception &) {
        stale = true;
    }
    CHECK(stale && noteDestination == unchanged);
    p = Project::empty();
    p.tracks[0].clips = {{"A", 0, 4}, {"B", 8, 4}};
    edit.selectClip({0, 0}, false);
    edit.selectClip({0, 1}, true);
    edit.duplicate(p, 0);
    CHECK(p.tracks[0].clips.size() == 4 && p.tracks[0].clips[2].beat == 12 &&
          p.tracks[0].clips[3].beat == 20);
    for (size_t i = 1; i < 16; ++i) {
        Track t;
        t.name = "Lane " + std::to_string(i);
        p.tracks.push_back(t);
    }
    p.timelineBeats = 128;
    p.loopBeats = 128;
    CHECK(p.valid());
    edit.selectClip({0, 0}, false);
    edit.copy(p, 0);
    edit.paste(p, 1, 40);
    CHECK(p.tracks[1].clips[0].beat == 40 && p.tracks[1].findPattern(p.tracks[1].clips[0].patternId));
    p.tracks[0].pattern().notes = {{71, 0, 1, .8f}};
    edit.selectClip({0, 0}, false);
    edit.copy(p, 0);
    auto clipDestination = Project::empty();
    edit.paste(clipDestination, 0, 0);
    CHECK(clipDestination.tracks[0].findPattern(clipDestination.tracks[0].clips[0].patternId)->notes[0].key ==
          71);
    p.tracks[1].strip.route = 0;
    p.tracks[1].strip.sends[1] = .2f;
    p.tracks[1].strip.pan = -.5f;
    p.tracks[1].strip.lowDb = 4;
    p.tracks[1].strip.thresholdDb = -18;
    p.buses[0].delayWet = .3f;
    p.buses[0].route = 1;
    p.tracks[2].instrument = audio::Instrument::Drum;
    p.tracks[2].drumSources[0] = 1;
    p.save(root / "mix.ally");
    CHECK(Project::load(root / "mix.ally") == p);
    p.buses[1].route = 0;
    CHECK(!p.valid()); // No cyclic/backward routes.
    p = Project::empty();
    midi::Take take;
    take.begin(1000, 0, 24000);
    take.message({1000, 0x90, 60, 100});
    take.message({97000, 0x80, 60, 0});
    take.message({85000, 0x91, 64, 90});
    take.finish(109000);
    take.apply(p, 0);
    CHECK(p.tracks[0].patterns.size() == 3 && p.tracks[0].clips.size() == 2);
    CHECK(p.tracks[0].patterns[1].notes[0].length == 4);
    CHECK(p.tracks[0].patterns[2].notes[0].beat == 0 && p.tracks[0].patterns[2].notes[0].length == .5);
    // Independent streaming parser/decoder: seek across the cache boundary and rate conversion.
    auto longPath = root / "long.wav";
    {
        media::WavWriter writer(longPath, 48000, 2);
        std::array<float, 8192> block{};
        for (size_t first = 0; first < 2200000; first += 4096) {
            size_t count = std::min<size_t>(4096, 2200000 - first);
            for (size_t i = 0; i < count; ++i) {
                block[i * 2] = float((first + i) % 100) / 200;
                block[i * 2 + 1] = -.25f;
            }
            writer.append(std::span(block).first(count * 2));
        }
        writer.finish();
    }
    media::WavReader reader(longPath);
    CHECK(reader.info().frames == 2200000);
    CHECK(reader.read(4095, 2).samples[2] == float(4096 % 100) / 200);
    auto stream = openStream(longPath);
    float l = 0, r = 0;
    stream->prefetch(999999, true);
    forbidNew = true;
    bool available = stream->read(999999, l, r);
    forbidNew = false;
    CHECK(available);
    CHECK(l == .495f && r == -.25f);
    p = Project::empty();
    p.bpm = 120;
    p.importWav(longPath, 0);
    CHECK(p.sources.back().stream && !p.sources.back().pcm && p.timelineBeats == 92);
    edit.selectClip({0, 0}, false);
    edit.copy(p, 0);
    auto audioDestination = Project::empty();
    edit.paste(audioDestination, 0, 0);
    CHECK(audioDestination.valid() && audioDestination.sources.size() == 2 &&
          audioDestination.source(audioDestination.tracks[0].clips[0].sourceId)->stream);
    p.tracks[0].clips[0].sourceOffset = 20;
    p.tracks[0].clips[0].length = 1;
    p.save(root / "stream.ally");
    auto restored = Project::load(root / "stream.ally");
    CHECK(restored.sources.back().stream && restored.sourceWarnings().empty());
    p.timelineBeats = 4;
    p.loopBeats = 4;
    CHECK(p.valid());
    renderProject(p, root / "stream-render.wav");
    auto rendered = media::readWav(root / "stream-render.wav");
    CHECK(rendered.frames() == 96000);
    CHECK(std::abs(rendered.samples[1] + .25f * .65f * .78f) < 1e-6f);
    // Callback capture demultiplexes simultaneously armed tracks on the disk worker.
    audio::Engine engine;
    Recorder recorder(engine);
    bool invalidRoute = false;
    try {
        recorder.start({{0, 0, root / "duplicate.wav"}, {1, 1, root / "duplicate.wav"}});
    } catch (const std::exception &) {
        invalidRoute = true;
    }
    CHECK(invalidRoute && !std::filesystem::exists(root / "duplicate.wav.partial.wav"));
    recorder.start({{0, 1, root / "left.wav"}, {1, 2, root / "right.wav"}, {2, 0, root / "stereo.wav"}});
    std::array<float, 512> input{};
    for (size_t i = 0; i < 256; ++i) {
        input[i * 2] = .25f;
        input[i * 2 + 1] = -.5f;
    }
    forbidNew = true;
    for (int block = 0; block < 16; ++block) {
        recorder.capture(input, 2);
    }
    forbidNew = false;
    const auto files = recorder.finish();
    CHECK(files.size() == 3 && files[0].frames == 4096 && recorder.lostFrames() == 0);
    auto left = media::readWav(root / "left.wav"), right = media::readWav(root / "right.wav"),
         stereo = media::readWav(root / "stereo.wav");
    CHECK(left.channels == 1 && left.samples[1024] == .25f && right.samples[1024] == -.5f &&
          stereo.channels == 2);
    recorder.monitor(0);
    recorder.capture(input, 2);
    std::array<float, 512> monitored{};
    engine.process(monitored);
    CHECK(engine.trackPeak(0) > 0 && monitored[0] > 0);
    // Truncated streaming chunks and final block bounds fail explicitly.
    std::filesystem::copy_file(longPath, root / "broken.wav");
    std::filesystem::resize_file(root / "broken.wav", 42);
    bool failed = false;
    try {
        media::WavReader broken(root / "broken.wav");
    } catch (...) {
        failed = true;
    }
    CHECK(failed);
    std::cout << "Group editing, extended arrangement, instruments/mix roundtrip, MIDI takes, disk "
                 "streaming/offline render, multitrack capture passed\n";
}

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() /
                          ("ally-production-" +
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        // Windows cannot remove files while stream readers still hold their handles.
        checkProduction(root);
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Production test failed: " << error.what() << '\n';
        return 1;
    }
}
