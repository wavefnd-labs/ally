#include <algorithm>
#include <ally/document.hpp>
#include <ally/file.hpp>
#include <ally/media/wav.hpp>
#include <ally/project.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            throw std::runtime_error(#x);                                                                    \
        }                                                                                                    \
    } while (false)
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception &) {
        failed = true;
    }
    CHECK(failed);
}
int main() {
    const auto root =
        std::filesystem::temp_directory_path() /
        ("ally-workflow-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "bundle" / "audio");
    {
        ally::FileLease first(root / "session.lock");
        ally::FileLease second(root / "session.lock");
        CHECK(first.acquired() && !second.acquired());
    }
    {
        ally::FileLease next(root / "session.lock");
        CHECK(next.acquired());
    }
    auto project = ally::Project::demo();
    auto &track = project.tracks[0];
    const auto original = track.pattern().notes;
    const auto duplicate = track.createPattern(true);
    track.pattern().notes[0].key = 61;
    CHECK(track.findPattern(1)->notes == original);
    track.clips[0].patternId = duplicate;
    auto plan = project.compile(48000);
    CHECK(plan.notes[0].key == 61);
    track.pattern().name = "Verse / chorus";
    project.save(root / "patterns.ally");
    CHECK(ally::Project::load(root / "patterns.ally") == project);
    // The empty pattern really replaces the assigned clip, leaving the independent variation intact.
    track.clips[0].patternId = track.createPattern();
    CHECK(track.pattern().notes.empty());
    CHECK(project.compile(48000).count == plan.count - 32);
    track.activePattern = 1;
    track.clips[0].patternId = 1;
    project.tracks.emplace_back();
    project.tracks.back().name = "Imported";
    ally::media::Pcm mono{44100, 1, std::vector<float>(44100, .25f)};
    const auto audio = root / "bundle" / "audio" / "sample.wav";
    ally::media::writeWav(audio, mono);
    project.importWav(audio, 5, 0);
    const auto id = project.tracks[5].clips[0].sourceId;
    CHECK(project.source(id)->pcm->size() == 96000);
    CHECK(!project.tracks[5].clips[0].sourceLoop);
    project.tracks[5].clips[0].sourceOffset = .25;
    project.tracks[5].clips[0].length = 4;
    ally::audio::Engine engine;
    CHECK(engine.submit(project.prepare(engine)));
    engine.looping(false);
    engine.play(true);
    std::vector<float> output(48000 * 2);
    engine.process(output);
    CHECK(engine.trackPeak(5) > .1f);
    // Prepared source trim is exact and non-looping audio ends in silence.
    for (auto &t : project.tracks) {
        t.clips.clear();
    }
    ally::Clip clip{"Audio", 0, 4, true};
    clip.sourceId = id;
    clip.sourceOffset = .25;
    clip.sourceLoop = false;
    project.tracks[5].clips.push_back(clip);
    CHECK(engine.submit(project.prepare(engine)));
    engine.rewind();
    engine.process(output);
    CHECK(output[0] > .1f);
    CHECK(std::all_of(output.begin() + 36001 * 2, output.end(), [](float v) { return v == 0; }));
    project.tracks[5].clips[0].sourceLoop = true;
    CHECK(engine.submit(project.prepare(engine)));
    engine.rewind();
    engine.process(output);
    CHECK(output.back() > .1f);
    const auto target = root / "bundle" / "song.ally";
    project.save(target);
    project.bpm = 128;
    project.save(target); // Existing files must be replaceable on Windows too.
    CHECK(ally::Project::load(target) == project);
    std::filesystem::copy(root / "bundle", root / "moved", std::filesystem::copy_options::recursive);
    auto moved = ally::Project::load(root / "moved" / "song.ally");
    CHECK(moved.sourceWarnings().empty());
    CHECK(moved.source(id)->path == root / "moved" / "audio" / "sample.wav");
    std::filesystem::remove(root / "moved" / "audio" / "sample.wav");
    auto missing = ally::Project::load(root / "moved" / "song.ally");
    CHECK(missing.valid() && !missing.sourceWarnings().empty() && !missing.source(id)->pcm);
    CHECK(missing.tracks[5].clips[0].sourceId == id);
    rejects([&] { ally::renderProject(missing, root / "missing.wav"); });
    missing.relinkSource(id, audio);
    CHECK(missing.sourceWarnings().empty() && missing.source(id)->pcm);
    auto invalid = project;
    invalid.tracks[0].activePattern = 900;
    rejects([&] { invalid.save(target); });
    CHECK(ally::Project::load(target) == project);
    rejects([&] { project.save(root / "bundle"); });
    CHECK(std::filesystem::is_directory(root / "bundle"));
    rejects([&] { project.importWav(root / "missing.wav", 5); });
    CHECK(ally::Project::load(target) == project);
    // Changed audio is reported and muted until the user deliberately relinks it.
    mono.samples[0] = .5f;
    ally::media::writeWav(audio, mono);
    auto changed = ally::Project::load(target);
    CHECK(!changed.sourceWarnings().empty() && !changed.source(id)->pcm);
    changed.relinkSource(id, audio);
    CHECK(changed.sourceWarnings().empty());
    ally::DocumentSession session(changed, target, root / "recovery" / "autosave.ally");
    CHECK(!session.dirty(changed));
    auto edited = changed;
    edited.bpm = 145;
    CHECK(session.dirty(edited));
    CHECK(!session.autosave(edited, 29));
    CHECK(session.autosave(edited, 31));
    CHECK(ally::Project::load(session.recoveryPath()) == edited);
    CHECK(!session.autosave(edited, 70));
    session.recovered();
    CHECK(session.path().empty() && session.dirty(changed));
    session.saved(edited, target);
    CHECK(!session.dirty(edited) && !std::filesystem::exists(session.recoveryPath()));
    // Version 4 variation migration preserves its audible transpose and separates the notes.
    {
        std::ofstream old(root / "v4.ally");
        old << "ALLY_PROJECT 4\n1 120 1 1 0 1 0\n\"Keys\" .6 0 0 16000 .002 .005 1 2\n";
        for (int i = 0; i < 16; ++i) {
            old << "0 ";
        }
        old << "\n60 0 .5 .7\n\"Verse\" 0 4 0 0\n\"Variation\" 4 4 0 1\n";
    }
    auto legacy = ally::Project::load(root / "v4.ally");
    CHECK(legacy.tracks[0].patterns.size() == 2);
    CHECK(legacy.tracks[0].findPattern(legacy.tracks[0].clips[1].patternId)->notes[0].key == 72);
    CHECK(legacy.compile(48000).notes[1].key == 72);
    std::filesystem::remove_all(root);
    std::cout << "Independent patterns, resampled WAV import/trim/loop/relink, portable sources, atomic "
                 "replace, recovery and v4 migration passed\n";
}
