#include <algorithm>
#include <ally/media/wav.hpp>
#include <ally/midi.hpp>
#include <ally/project.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
int main() {
    auto p = ally::Project::demo();
    CHECK(p.valid());
    const auto root =
        std::filesystem::temp_directory_path() /
        ("ally-project-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(root));
    p.save(root / "test.ally");
    auto q = ally::Project::load(root / "test.ally");
    CHECK(q == p);
    CHECK(q.bpm == p.bpm);
    p.tracks[1].solo = true;
    auto solo = p.compile(48000);
    CHECK(solo.gains[0] == 0 && solo.gains[1] > 0 && solo.gains[2] == 0);
    p.save(root / "solo.ally");
    CHECK(ally::Project::load(root / "solo.ally").tracks[1].solo);
    p.tracks[1].solo = false;
    auto plan = p.compile(48000);
    CHECK(plan.loopFrames == 743226 && plan.loopEndFrame == 557419 && plan.count == 108);
    CHECK(plan.sampleCount == 1 && plan.samples[0].track == 3);
    CHECK(plan.samples[0].start == 92903 && plan.samples[0].duration == 557420);
    CHECK(q.songMode && q.tracks[1].clips == p.tracks[1].clips);
    for (uint32_t i = 1; i < plan.count; ++i) {
        CHECK(plan.notes[i - 1].start <= plan.notes[i].start);
    }
    CHECK(p.tracks.size() == 5);
    // The long Keys clip repeats its pattern, followed by an octave variation.
    auto has = [&](uint8_t key, uint64_t start) {
        return std::any_of(plan.notes.begin(), plan.notes.begin() + plan.count,
                           [&](const auto &n) { return n.track == 0 && n.key == key && n.start == start; });
    };
    CHECK(has(60, 0) && has(60, 92903) && has(72, 371613));
    p.tracks[1].clips.clear();
    CHECK(p.compile(48000).count == 84);
    p = q;
    p.songMode = false;
    CHECK(p.compile(48000).count == 20 && p.compile(48000).loopFrames == 92903);
    CHECK(p.compile(48000).sampleCount == 0);
    p.songMode = true;
    ally::renderProject(p, root / "render.wav");
    auto pcm = ally::media::readWav(root / "render.wav");
    CHECK(pcm.frames() == 743226);
    double energy = 0;
    for (auto f : pcm.samples) {
        CHECK(std::isfinite(f));
        energy += f * f;
    }
    CHECK(energy > 1);
    std::array<uint8_t, 3> note{0x90, 60, 0};
    auto midi = ally::midi::parse(note, 123);
    CHECK(midi && midi->status == 0x80 && midi->sampleTime == 123);
    note[1] = 200;
    CHECK(!ally::midi::parse(note, 0));
    // Version 2 documents keep their one-bar playback and acquire one instance per lane.
    {
        std::ofstream old(root / "legacy.ally");
        old << "ALLY_PROJECT 2\n120\n";
        for (int i = 0; i < 4; ++i) {
            old << "\"Legacy\" 0.6 0 0 0\n";
            for (int step = 0; step < 16; ++step) {
                old << "0 ";
            }
            old << '\n';
        }
    }
    const auto legacy = ally::Project::load(root / "legacy.ally");
    CHECK(!legacy.songMode && legacy.tracks[0].clips.size() == 1 && legacy.tracks[0].clips[0].beat == 0);
    // Capacity validation accounts for arrangement expansion even in pattern mode.
    auto large = ally::Project::demo();
    large.songMode = false;
    large.tracks[0].notes.assign(65, {60, 0, .5, .7f});
    CHECK(!large.valid());
    auto invalid = ally::Project::demo();
    invalid.tracks[0].clips[0].beat = 31;
    CHECK(!invalid.valid());
    invalid = ally::Project::demo();
    invalid.tracks[3].clips[0].name = "missing.wav";
    CHECK(!invalid.valid());
    invalid = ally::Project::demo();
    invalid.tracks.resize(9);
    CHECK(!invalid.valid());
    // Version 3 fixed arrangement slots migrate to independent clips.
    {
        std::ofstream old(root / "v3.ally");
        old << "ALLY_PROJECT 3\n1 120\n";
        for (int i = 0; i < 4; ++i) {
            old << "\"Legacy\" 0.6 0 0 0\n";
            for (int step = 0; step < 16; ++step) {
                old << "0 ";
            }
            old << "\n0 0 1 0 0 0 0 0\n";
        }
    }
    auto migrated = ally::Project::load(root / "v3.ally");
    CHECK(migrated.songMode && migrated.tracks[0].clips.size() == 1);
    CHECK(migrated.tracks[0].clips[0].beat == 8);
    // Rounded endpoints must meet exactly at the arrangement boundary at any tempo.
    for (double bpm : {87.3, 124.0, 137.0, 299.9}) {
        auto edge = ally::Project::demo();
        edge.bpm = bpm;
        edge.tracks[3].clips[0].beat = 1.25;
        edge.tracks[3].clips[0].length = 30.75;
        auto rounded = edge.compile(48000);
        CHECK(rounded.samples[0].start + rounded.samples[0].duration == rounded.loopFrames);
        ally::audio::Engine engine;
        CHECK(engine.submit(rounded));
    }
    p.bpm = 0;
    CHECK(!p.valid());
    std::filesystem::remove_all(root);
    std::cout << "document roundtrip, sequencer plan, offline WAV render and MIDI parsing passed\n";
}
