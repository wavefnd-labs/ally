#include <algorithm>
#include <ally/media/wav.hpp>
#include <ally/project.hpp>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <stdexcept>

namespace ally {
std::vector<float> makeTexture(uint32_t rate) {
    std::vector<float> samples(size_t(rate) * 4);
    uint32_t seed = 42;
    for (size_t i = 0; i < samples.size() / 2; ++i) {
        seed = seed * 1664525u + 1013904223u;
        double t = double(i) / rate;
        double envelope = .35 + .25 * std::sin(t * 7) + .12 * std::sin(t * 23);
        double noise = double(seed >> 8) / 8388608 - 1;
        double wave = envelope * (.15 * std::sin(2 * std::numbers::pi * 220 * t) + .1 * noise);
        samples[i * 2] = float(wave);
        samples[i * 2 + 1] = float(wave * .85);
    }
    return samples;
}

Project Project::demo() {
    Project p;
    p.tracks.resize(5);
    const std::array names{"Analog Keys", "Sub Bass", "Drum Rack", "Texture", "Lead"};
    for (size_t i = 0; i < names.size(); ++i) {
        p.tracks[i].name = names[i];
    }
    const std::array keys{60, 64, 67, 69, 68, 67, 64, 62};
    const std::array beats{0.0, .52, 1.0, 1.52, 2.04, 2.56, 3.08, 3.56};
    const std::array lengths{.48, .40, .44, .52, .44, .48, .44, .40};
    for (size_t i = 0; i < keys.size(); ++i) {
        p.tracks[0].notes.push_back({keys[i], beats[i], lengths[i], float(.35 + i * .07)});
    }
    p.tracks[1].notes = {{36, 0, 1.0, .8f}, {36, 1.12, .84, .8f}, {43, 2.08, .84, .8f}, {33, 3, .96, .8f}};
    p.tracks[4].notes = {{72, 0, .4, .7f}, {76, 1, .4, .7f}, {79, 2, .4, .7f}, {74, 3, .4, .7f}};
    for (int i = 0; i < 16; i += 4) {
        p.tracks[2].notes.push_back({36, i * .25, .2, .85f});
    }
    p.tracks[0].clips = {{"Pattern 01", 0, 16}, {"Variation", 16, 8, false, true}};
    p.tracks[1].clips = {{"Bassline", 0, 24}};
    p.tracks[2].clips = {{"Drums 01", 0, 8}, {"Drums 01", 8, 8}, {"Fill", 16, 8, false, true}};
    p.tracks[3].clips = {{"texture.wav", 4, 24, true}};
    p.tracks[4].clips = {{"Main melody", 12, 12}};
    return p;
}

bool Project::valid() const noexcept {
    if (!std::isfinite(bpm) || bpm < 20 || bpm > 300 || tracks.empty() ||
        tracks.size() > audio::Plan::maxTracks || !std::isfinite(masterGain) || masterGain < 0 ||
        masterGain > 1) {
        return false;
    }
    size_t total = 0, samples = 0;
    for (const auto &t : tracks) {
        if (t.name.empty() || t.name.size() > 80 || !std::isfinite(t.gain) || t.gain < 0 || t.gain > 1 ||
            !std::isfinite(t.cutoff) || t.cutoff < 20 || t.cutoff > 20000 || !std::isfinite(t.attack) ||
            t.attack < .001 || t.attack > 2 || !std::isfinite(t.release) || t.release < .001 ||
            t.release > 2 || t.clips.size() > 32) {
            return false;
        }
        const auto events = t.notes.size() + size_t(std::count(t.steps.begin(), t.steps.end(), true));
        total += events; // Pattern mode also has to fit.
        for (const auto &n : t.notes) {
            if (n.key < 0 || n.key > 127 || !std::isfinite(n.beat) || n.beat < 0 || n.beat >= 4 ||
                !std::isfinite(n.length) || n.length < .0625 || n.length > 4 - n.beat ||
                !std::isfinite(n.velocity) || n.velocity < 0 || n.velocity > 1) {
                return false;
            }
        }
        for (const auto &clip : t.clips) {
            if (clip.name.empty() || clip.name.size() > 80 || !std::isfinite(clip.beat) || clip.beat < 0 ||
                clip.beat >= 32 || !std::isfinite(clip.length) || clip.length < .25 ||
                clip.length > 32 - clip.beat) {
                return false;
            }
            if (clip.audio) {
                if (clip.name != "texture.wav") {
                    return false;
                }
                ++samples;
            } else {
                total += events * size_t(std::ceil(clip.length / 4));
            }
        }
    }
    return total <= audio::Plan::capacity && samples <= audio::Plan::sampleCapacity;
}

audio::Plan Project::compile(uint32_t rate) const {
    if (!valid() || rate < 8000 || rate > 384000) {
        throw std::runtime_error("Invalid project or sample rate");
    }
    audio::Plan plan;
    const double fpb = rate * 60.0 / bpm;
    plan.loopFrames = uint64_t(std::llround(fpb * (songMode ? 32 : 4)));
    plan.loopEndFrame = uint64_t(std::llround(fpb * (songMode ? 24 : 4)));
    plan.masterGain = masterMuted ? 0 : masterGain;
    for (int beat = 0; beat < (songMode ? 32 : 4); ++beat) {
        plan.clicks[plan.clickCount++] = uint64_t(std::llround(fpb * beat));
    }
    const bool anySolo = std::any_of(tracks.begin(), tracks.end(), [](const Track &t) { return t.solo; });
    plan.gains.fill(0);
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto &t = tracks[i];
        plan.gains[i] = t.muted || (anySolo && !t.solo) ? 0 : t.gain;
        plan.cutoffs[i] = t.cutoff;
        plan.attacks[i] = uint32_t(std::llround(t.attack * rate));
        plan.releases[i] = uint32_t(std::llround(t.release * rate));
        auto pattern = [&](double start, double length, bool variation) {
            auto add = [&](const Note &n, double offset) {
                if (offset + n.beat >= length) {
                    return;
                }
                const double beginBeat = start + offset + n.beat;
                const double endBeat = beginBeat + std::min(n.length, length - offset - n.beat);
                const auto first = uint64_t(std::llround(beginBeat * fpb));
                const auto last = uint64_t(std::llround(endBeat * fpb));
                if (last <= first) {
                    return;
                }
                plan.notes[plan.count++] = {
                    first, last - first, uint8_t(std::clamp(n.key + (variation && i != 2 ? 12 : 0), 0, 127)),
                    uint8_t(i), n.velocity};
            };
            for (double offset = 0; offset < length; offset += 4) {
                for (const auto &n : t.notes) {
                    add(n, offset);
                }
                for (int s = 0; s < 16; ++s) {
                    if (t.steps[s]) {
                        add({i == 2 ? 36 : 60, s * .25, .2, .85f}, offset);
                    }
                }
            }
        };
        if (!songMode) {
            pattern(0, 4, false);
        } else {
            for (const auto &clip : t.clips) {
                if (clip.audio) {
                    const auto first = uint64_t(std::llround(clip.beat * fpb));
                    const auto last = uint64_t(std::llround((clip.beat + clip.length) * fpb));
                    plan.samples[plan.sampleCount++] = {first, last - first, uint8_t(i)};
                } else {
                    pattern(clip.beat, clip.length, clip.variation);
                }
            }
        }
    }
    std::stable_sort(plan.notes.begin(), plan.notes.begin() + plan.count,
                     [](const auto &a, const auto &b) { return a.start < b.start; });
    return plan;
}

void Project::save(const std::filesystem::path &path) const {
    if (!valid()) {
        throw std::runtime_error("Cannot save invalid project");
    }
    auto temp = path;
    temp += ".tmp";
    std::ofstream out(temp, std::ios::trunc);
    out << std::setprecision(17) << "ALLY_PROJECT 4\n"
        << songMode << ' ' << bpm << ' ' << tracks.size() << ' ' << masterGain << ' ' << masterMuted << ' '
        << loop << ' ' << metronome << '\n';
    for (const auto &t : tracks) {
        out << std::quoted(t.name) << ' ' << t.gain << ' ' << t.muted << ' ' << t.solo << ' ' << t.cutoff
            << ' ' << t.attack << ' ' << t.release << ' ' << t.notes.size() << ' ' << t.clips.size() << '\n';
        for (bool step : t.steps) {
            out << step << ' ';
        }
        out << '\n';
        for (const auto &n : t.notes) {
            out << n.key << ' ' << n.beat << ' ' << n.length << ' ' << n.velocity << '\n';
        }
        for (const auto &clip : t.clips) {
            out << std::quoted(clip.name) << ' ' << clip.beat << ' ' << clip.length << ' ' << clip.audio
                << ' ' << clip.variation << '\n';
        }
    }
    out.flush();
    if (!out) {
        throw std::runtime_error("Project write failed");
    }
    out.close();
    std::filesystem::rename(temp, path);
}

Project Project::load(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 1024 * 1024) {
        throw std::runtime_error("Project exceeds size limit");
    }
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (!((magic == "ALLY_PROJECT" && version >= 2 && version <= 4) ||
          (magic == "DAW_PROJECT" && version == 1))) {
        throw std::runtime_error("Unsupported project version");
    }
    Project p;
    p.songMode = false;
    p.masterGain = 1;
    if (version >= 3) {
        in >> p.songMode;
    }
    in >> p.bpm;
    size_t tracks = 4;
    if (version >= 4) {
        in >> tracks >> p.masterGain >> p.masterMuted >> p.loop >> p.metronome;
    }
    if (!in || tracks == 0 || tracks > audio::Plan::maxTracks) {
        throw std::runtime_error("Invalid track count");
    }
    p.tracks.resize(tracks);
    for (auto &t : p.tracks) {
        size_t count = 0, clips = 0;
        in >> std::quoted(t.name) >> t.gain >> t.muted;
        if (version >= 2) {
            in >> t.solo;
        }
        if (version >= 4) {
            in >> t.cutoff >> t.attack >> t.release;
        }
        in >> count;
        if (version >= 4) {
            in >> clips;
        }
        if (!in || count > audio::Plan::capacity || clips > 32) {
            throw std::runtime_error("Invalid track");
        }
        for (auto &step : t.steps) {
            in >> step;
        }
        if (version < 4) {
            std::array<bool, 8> slots{true};
            if (version >= 3) {
                for (auto &slot : slots) {
                    in >> slot;
                }
            }
            for (int bar = 0; bar < 8; ++bar) {
                if (slots[bar]) {
                    t.clips.push_back({"Pattern 01", bar * 4.0, 4});
                }
            }
        }
        t.notes.resize(count);
        for (auto &n : t.notes) {
            in >> n.key >> n.beat >> n.length >> n.velocity;
        }
        if (version >= 4) {
            t.clips.resize(clips);
            for (auto &clip : t.clips) {
                in >> std::quoted(clip.name) >> clip.beat >> clip.length >> clip.audio >> clip.variation;
            }
        }
    }
    if (!in || !p.valid()) {
        throw std::runtime_error("Invalid project data");
    }
    in >> std::ws;
    if (!in.eof()) {
        throw std::runtime_error("Unexpected project data");
    }
    return p;
}

void renderProject(const Project &project, const std::filesystem::path &path) {
    auto texture = makeTexture();
    audio::Engine engine(48000, texture);
    auto plan = project.compile(engine.sampleRate());
    if (!engine.submit(plan)) {
        throw std::runtime_error("Cannot submit render plan");
    }
    engine.looping(false);
    engine.play(true);
    media::Pcm pcm;
    pcm.samples.resize(size_t(plan.loopFrames) * 2);
    for (size_t p = 0; p < pcm.samples.size(); p += 512) {
        engine.process(std::span(pcm.samples).subspan(p, std::min<size_t>(512, pcm.samples.size() - p)));
    }
    media::writeWav(path, pcm);
}
} // namespace ally
