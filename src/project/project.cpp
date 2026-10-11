#include <algorithm>
#include <ally/audio/resample.hpp>
#include <ally/file.hpp>
#include <ally/media/wav.hpp>
#include <ally/project.hpp>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <utility>

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

namespace {
AudioSource textureSource() {
    AudioSource source;
    source.pcm = std::make_shared<const std::vector<float>>(makeTexture());
    return source;
}
void buildPeaks(AudioSource &source) {
    source.peaks.fill(0);
    if (!source.pcm || source.pcm->empty()) {
        return;
    }
    const auto frames = source.pcm->size() / 2;
    for (size_t i = 0; i < frames; ++i) {
        auto &peak = source.peaks[std::min<size_t>(1023, i * 1024 / frames)];
        peak = std::max({peak, std::abs((*source.pcm)[i * 2]), std::abs((*source.pcm)[i * 2 + 1])});
    }
}
AudioSource readSource(const std::filesystem::path &path) {
    // Bound both compressed input and decoded PCM before allocating the prepared stereo buffer.
    if (std::filesystem::file_size(path) > 64 * 1024 * 1024) {
        throw std::runtime_error("WAV import exceeds 64 MiB file limit");
    }
    auto pcm = media::readWav(path);
    if (pcm.channels > 2 || !pcm.frames() ||
        double(pcm.frames()) * 48000 / pcm.sampleRate > 8 * 1024 * 1024) {
        throw std::runtime_error("Import requires mono/stereo WAV within 64 MiB prepared limit");
    }
    AudioSource source;
    source.path = std::filesystem::absolute(path).lexically_normal();
    source.name = pathText(path.filename());
    if (source.name.size() > 80) {
        source.name.resize(80);
    }
    source.originalRate = pcm.sampleRate;
    source.originalFrames = pcm.frames();
    uint64_t hash = 14695981039346656037ull;
    auto mix = [&](uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            hash = (hash ^ ((value >> (i * 8)) & 255)) * 1099511628211ull;
        }
    };
    mix(pcm.sampleRate);
    mix(pcm.channels);
    for (float value : pcm.samples) {
        mix(std::bit_cast<uint32_t>(value));
    }
    source.fingerprint = hash;
    std::vector<float> stereo(pcm.frames() * 2);
    for (size_t i = 0; i < pcm.frames(); ++i) {
        stereo[i * 2] = std::clamp(pcm.samples[i * pcm.channels], -1.0f, 1.0f);
        stereo[i * 2 + 1] =
            std::clamp(pcm.samples[i * pcm.channels + (pcm.channels == 2 ? 1 : 0)], -1.0f, 1.0f);
    }
    source.pcm =
        std::make_shared<const std::vector<float>>(audio::resampleStereo(stereo, pcm.sampleRate, 48000));
    buildPeaks(source);
    return source;
}
bool noteValid(const Note &n) {
    return n.key >= 0 && n.key <= 127 && std::isfinite(n.beat) && n.beat >= 0 && n.beat < 4 &&
           std::isfinite(n.length) && n.length >= .0625 && n.length <= 4 - n.beat &&
           std::isfinite(n.velocity) && n.velocity >= 0 && n.velocity <= 1;
}
} // namespace
bool AudioSource::operator==(const AudioSource &other) const {
    return id == other.id && name == other.name && path == other.path && originalRate == other.originalRate &&
           originalFrames == other.originalFrames && fingerprint == other.fingerprint && error == other.error;
}
const Pattern *Track::findPattern(uint32_t id) const noexcept {
    for (const auto &p : patterns) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}
const Pattern &Track::pattern() const {
    const auto *p = findPattern(activePattern);
    if (!p) {
        throw std::runtime_error("Missing active pattern");
    }
    return *p;
}
Pattern &Track::pattern() {
    return const_cast<Pattern &>(std::as_const(*this).pattern());
}
uint32_t Track::createPattern(bool duplicate) {
    if (patterns.size() >= 32) {
        throw std::runtime_error("Maximum 32 patterns per track");
    }
    uint32_t next = 1;
    for (const auto &p : patterns) {
        next = std::max(next, p.id + 1);
    }
    if (!next) {
        throw std::runtime_error("Pattern ID exhausted");
    }
    Pattern p = duplicate ? pattern() : Pattern{};
    p.id = next;
    p.name = duplicate ? p.name.substr(0, 74) + " copy" : "Pattern " + std::to_string(next);
    patterns.push_back(std::move(p));
    return activePattern = next;
}
const AudioSource *Project::source(uint32_t id) const noexcept {
    for (const auto &s : sources) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}
Project Project::demo() {
    Project p;
    p.sources.push_back(textureSource());
    buildPeaks(p.sources[0]);
    p.tracks.resize(5);
    const std::array names{"Analog Keys", "Sub Bass", "Drum Rack", "Texture", "Lead"};
    for (size_t i = 0; i < names.size(); ++i) {
        p.tracks[i].name = names[i];
    }
    const std::array keys{60, 64, 67, 69, 68, 67, 64, 62};
    const std::array beats{0.0, .52, 1.0, 1.52, 2.04, 2.56, 3.08, 3.56};
    const std::array lengths{.48, .40, .44, .52, .44, .48, .44, .40};
    for (size_t i = 0; i < keys.size(); ++i) {
        p.tracks[0].pattern().notes.push_back({keys[i], beats[i], lengths[i], float(.35 + i * .07)});
    }
    p.tracks[1].pattern().notes = {
        {36, 0, 1.0, .8f}, {36, 1.12, .84, .8f}, {43, 2.08, .84, .8f}, {33, 3, .96, .8f}};
    p.tracks[4].pattern().notes = {{72, 0, .4, .7f}, {76, 1, .4, .7f}, {79, 2, .4, .7f}, {74, 3, .4, .7f}};
    for (int i = 0; i < 16; i += 4) {
        p.tracks[2].pattern().notes.push_back({36, i * .25, .2, .85f});
    }
    p.tracks[0].clips = {{"Pattern 01", 0, 16}, {"Variation", 16, 8, false, true}};
    p.tracks[1].clips = {{"Bassline", 0, 24}};
    p.tracks[2].clips = {{"Drums 01", 0, 8}, {"Drums 01", 8, 8}, {"Fill", 16, 8, false, true}};
    p.tracks[3].clips = {{"texture.wav", 4, 24, true}};
    p.tracks[4].clips = {{"Main melody", 12, 12}};
    // Variations are real, independently editable patterns, preserving the original audible demo.
    auto variation = p.tracks[0].createPattern(true);
    p.tracks[0].pattern().name = "Variation";
    for (auto &note : p.tracks[0].pattern().notes) {
        note.key = std::min(127, note.key + 12);
    }
    p.tracks[0].clips[1].patternId = variation;
    p.tracks[0].clips[1].variation = false;
    p.tracks[0].activePattern = 1;
    auto fill = p.tracks[2].createPattern(true);
    p.tracks[2].pattern().name = "Fill";
    p.tracks[2].clips[2].patternId = fill;
    p.tracks[2].clips[2].variation = false;
    p.tracks[2].activePattern = 1;
    return p;
}
bool Project::valid() const noexcept {
    if (!std::isfinite(bpm) || bpm < 20 || bpm > 300 || tracks.empty() ||
        tracks.size() > audio::Plan::maxTracks || !std::isfinite(masterGain) || masterGain < 0 ||
        masterGain > 1 || sources.size() > audio::Plan::sampleCapacity) {
        return false;
    }
    size_t bytes = 0;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto &s = sources[i];
        if (!s.id || s.name.empty() || s.name.size() > 80 || s.originalRate < 8000 ||
            s.originalRate > 384000 || !s.originalFrames ||
            s.originalFrames > uint64_t(s.originalRate) * 1100 ||
            (s.path.empty() &&
             (s.id != 1 || s.fingerprint != 0 || s.originalRate != 48000 || s.originalFrames != 96000))) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (sources[j].id == s.id) {
                return false;
            }
        }
        if (s.pcm) {
            if (s.pcm->empty() || s.pcm->size() % 2 || s.pcm->size() > 16 * 1024 * 1024) {
                return false;
            }
            bytes += s.pcm->size() * sizeof(float);
        }
    }
    if (bytes > 128 * 1024 * 1024) {
        return false;
    }
    size_t patternEvents = 0, songEvents = 0, samples = 0;
    for (const auto &t : tracks) {
        if (t.name.empty() || t.name.size() > 80 || !std::isfinite(t.gain) || t.gain < 0 || t.gain > 1 ||
            !std::isfinite(t.cutoff) || t.cutoff < 20 || t.cutoff > 20000 || !std::isfinite(t.attack) ||
            t.attack < .001 || t.attack > 2 || !std::isfinite(t.release) || t.release < .001 ||
            t.release > 2 || t.clips.size() > 32 || t.patterns.empty() || t.patterns.size() > 32 ||
            !t.findPattern(t.activePattern)) {
            return false;
        }
        for (size_t i = 0; i < t.patterns.size(); ++i) {
            const auto &p = t.patterns[i];
            if (!p.id || p.id == UINT32_MAX || p.name.empty() || p.name.size() > 80 ||
                p.notes.size() > audio::Plan::capacity ||
                !std::all_of(p.notes.begin(), p.notes.end(), noteValid)) {
                return false;
            }
            for (size_t j = 0; j < i; ++j) {
                if (t.patterns[j].id == p.id) {
                    return false;
                }
            }
            if (p.id == t.activePattern) {
                patternEvents += p.notes.size() + size_t(std::count(p.steps.begin(), p.steps.end(), true));
            }
        }
        for (const auto &clip : t.clips) {
            if (clip.name.empty() || clip.name.size() > 80 || !std::isfinite(clip.beat) || clip.beat < 0 ||
                clip.beat >= 32 || !std::isfinite(clip.length) || clip.length < .25 ||
                clip.length > 32 - clip.beat || !std::isfinite(clip.sourceOffset) || clip.sourceOffset < 0 ||
                clip.sourceOffset > 1100) {
                return false;
            }
            if (clip.audio) {
                const auto *s = source(clip.sourceId);
                if (!s || clip.sourceOffset >= double(s->originalFrames) / s->originalRate) {
                    return false;
                }
                ++samples;
            } else {
                const auto *p = t.findPattern(clip.patternId);
                if (!p || clip.variation) {
                    return false;
                }
                songEvents += (p->notes.size() + size_t(std::count(p->steps.begin(), p->steps.end(), true))) *
                              size_t(std::ceil(clip.length / 4));
            }
        }
    }
    return patternEvents <= audio::Plan::capacity && songEvents <= audio::Plan::capacity &&
           samples <= audio::Plan::sampleCapacity;
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
        auto pattern = [&](const Pattern &p, double start, double length) {
            auto add = [&](const Note &n, double offset) {
                if (offset + n.beat >= length) {
                    return;
                }
                const double begin = start + offset + n.beat;
                const double end = begin + std::min(n.length, length - offset - n.beat);
                auto first = uint64_t(std::llround(begin * fpb)), last = uint64_t(std::llround(end * fpb));
                if (last > first) {
                    plan.notes[plan.count++] = {first, last - first, uint8_t(n.key), uint8_t(i), n.velocity};
                }
            };
            for (double offset = 0; offset < length; offset += 4) {
                for (const auto &n : p.notes) {
                    add(n, offset);
                }
                for (int step = 0; step < 16; ++step) {
                    if (p.steps[step]) {
                        add({i == 2 ? 36 : 60, step * .25, .2, .85f}, offset);
                    }
                }
            }
        };
        if (!songMode) {
            pattern(t.pattern(), 0, 4);
        } else {
            for (const auto &clip : t.clips) {
                if (clip.audio) {
                    const auto first = uint64_t(std::llround(clip.beat * fpb));
                    const auto last = uint64_t(std::llround((clip.beat + clip.length) * fpb));
                    const auto index = size_t(source(clip.sourceId) - sources.data());
                    plan.samples[plan.sampleCount++] = {first,
                                                        last - first,
                                                        uint8_t(i),
                                                        uint32_t(index),
                                                        uint64_t(std::llround(clip.sourceOffset * rate)),
                                                        clip.sourceLoop};
                } else {
                    pattern(*t.findPattern(clip.patternId), clip.beat, clip.length);
                }
            }
        }
    }
    std::stable_sort(plan.notes.begin(), plan.notes.begin() + plan.count,
                     [](const auto &a, const auto &b) { return a.start < b.start; });
    return plan;
}
audio::Plan Project::prepare(audio::Engine &engine) const {
    auto plan = compile(engine.sampleRate());
    plan.sourcesBound = true;
    for (size_t i = 0; i < sources.size(); ++i) {
        auto pcm = sources[i].pcm;
        if (pcm && engine.sampleRate() != 48000) {
            pcm = std::make_shared<const std::vector<float>>(
                audio::resampleStereo(*pcm, 48000, engine.sampleRate()));
        }
        plan.sources[i] = engine.prepareSample(pcm);
    }
    return plan;
}
void Project::importWav(const std::filesystem::path &path, size_t track, double beat) {
    if (track >= tracks.size() || !std::isfinite(beat) || beat < 0 || beat >= 32 || sources.size() >= 32) {
        throw std::runtime_error("No room for imported audio source");
    }
    auto source = readSource(path);
    uint32_t next = 1;
    for (const auto &existing : sources) {
        next = std::max(next, existing.id + 1);
    }
    source.id = next;
    Clip clip;
    clip.name = source.name;
    clip.audio = true;
    clip.sourceId = source.id;
    clip.sourceLoop = false;
    clip.beat = beat;
    clip.length = std::clamp(double(source.pcm->size() / 2) / 48000 * bpm / 60, .25, 32 - beat);
    Project candidate = *this;
    candidate.sources.push_back(std::move(source));
    candidate.tracks[track].clips.push_back(clip);
    if (!candidate.valid()) {
        throw std::runtime_error("Import exceeds document event or 128 MiB PCM limit");
    }
    *this = std::move(candidate);
}
void Project::relinkSource(uint32_t id, const std::filesystem::path &path) {
    auto replacement = readSource(path);
    replacement.id = id;
    Project candidate = *this;
    auto it = std::find_if(candidate.sources.begin(), candidate.sources.end(),
                           [&](const auto &s) { return s.id == id; });
    if (it == candidate.sources.end() || it->path.empty()) {
        throw std::runtime_error("Cannot relink this source");
    }
    *it = std::move(replacement);
    if (!candidate.valid()) {
        throw std::runtime_error("Replacement does not fit clip source offsets or PCM limits");
    }
    *this = std::move(candidate);
}
std::string Project::sourceWarnings() const {
    std::string result;
    for (const auto &s : sources) {
        if (!s.error.empty()) {
            result += (result.empty() ? "" : "; ") + s.name + ": " + s.error;
        }
    }
    return result;
}
void Project::save(const std::filesystem::path &path) const {
    if (!valid()) {
        throw std::runtime_error("Cannot save invalid project");
    }
    std::ostringstream out;
    out << std::setprecision(17) << "ALLY_PROJECT 5\n"
        << songMode << ' ' << bpm << ' ' << tracks.size() << ' ' << masterGain << ' ' << masterMuted << ' '
        << loop << ' ' << metronome << ' ' << sources.size() << '\n';
    const auto parent = std::filesystem::absolute(path).parent_path();
    for (const auto &source : sources) {
        auto relative =
            source.path.empty() ? std::filesystem::path{} : source.path.lexically_relative(parent);
        if (!source.path.empty() && relative.empty()) {
            relative = source.path;
        }
        out << source.id << ' ' << std::quoted(source.name) << ' ' << std::quoted(pathText(relative)) << ' '
            << source.originalRate << ' ' << source.originalFrames << ' ' << source.fingerprint << '\n';
    }
    for (const auto &t : tracks) {
        out << std::quoted(t.name) << ' ' << t.gain << ' ' << t.muted << ' ' << t.solo << ' ' << t.cutoff
            << ' ' << t.attack << ' ' << t.release << ' ' << t.activePattern << ' ' << t.patterns.size()
            << ' ' << t.clips.size() << '\n';
        for (const auto &p : t.patterns) {
            out << p.id << ' ' << std::quoted(p.name) << ' ' << p.notes.size() << '\n';
            for (bool step : p.steps) {
                out << step << ' ';
            }
            out << '\n';
            for (const auto &n : p.notes) {
                out << n.key << ' ' << n.beat << ' ' << n.length << ' ' << n.velocity << '\n';
            }
        }
        for (const auto &clip : t.clips) {
            out << std::quoted(clip.name) << ' ' << clip.beat << ' ' << clip.length << ' ' << clip.audio
                << ' ' << clip.patternId << ' ' << clip.sourceId << ' ' << clip.sourceOffset << ' '
                << clip.sourceLoop << '\n';
        }
    }
    const auto text = out.str();
    if (text.size() > 1024 * 1024) {
        throw std::runtime_error("Project exceeds 1 MiB document size limit");
    }
    atomicWrite(path, text);
}
Project Project::load(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 1024 * 1024) {
        throw std::runtime_error("Project exceeds size limit");
    }
    std::ifstream in(path);
    std::string magic;
    int version = 0;
    in >> magic >> version;
    if (!((magic == "ALLY_PROJECT" && version >= 2 && version <= 5) ||
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
    size_t tracks = 4, sources = 0;
    if (version >= 4) {
        in >> tracks >> p.masterGain >> p.masterMuted >> p.loop >> p.metronome;
    }
    if (version >= 5) {
        in >> sources;
    }
    if (!in || !tracks || tracks > audio::Plan::maxTracks || sources > 32) {
        throw std::runtime_error("Invalid document counts");
    }
    for (size_t i = 0; i < sources; ++i) {
        AudioSource source;
        std::string location;
        in >> source.id >> std::quoted(source.name) >> std::quoted(location) >> source.originalRate >>
            source.originalFrames >> source.fingerprint;
        if (!in || location.size() > 4096) {
            throw std::runtime_error("Invalid source metadata");
        }
        source.path =
            location.empty()
                ? std::filesystem::path{}
                : (std::filesystem::absolute(path).parent_path() / textPath(location)).lexically_normal();
        p.sources.push_back(std::move(source));
    }
    p.tracks.resize(tracks);
    size_t allNotes = 0;
    for (auto &t : p.tracks) {
        size_t count = 0, clips = 0, patterns = 1;
        in >> std::quoted(t.name) >> t.gain >> t.muted;
        if (version >= 2) {
            in >> t.solo;
        }
        if (version >= 4) {
            in >> t.cutoff >> t.attack >> t.release;
        }
        if (version >= 5) {
            in >> t.activePattern >> patterns >> clips;
        } else {
            in >> count;
            if (version >= 4) {
                in >> clips;
            }
        }
        if (!in || count > audio::Plan::capacity || !patterns || patterns > 32 || clips > 32) {
            throw std::runtime_error("Invalid track counts");
        }
        t.patterns.resize(patterns);
        for (auto &pattern : t.patterns) {
            if (version >= 5) {
                in >> pattern.id >> std::quoted(pattern.name) >> count;
            }
            allNotes += count;
            if (!in || count > audio::Plan::capacity || allNotes > 32768) {
                throw std::runtime_error("Invalid pattern note count");
            }
            for (auto &step : pattern.steps) {
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
            pattern.notes.resize(count);
            for (auto &n : pattern.notes) {
                in >> n.key >> n.beat >> n.length >> n.velocity;
            }
        }
        if (version >= 4) {
            t.clips.resize(clips);
            for (auto &clip : t.clips) {
                in >> std::quoted(clip.name) >> clip.beat >> clip.length >> clip.audio;
                if (version >= 5) {
                    in >> clip.patternId >> clip.sourceId >> clip.sourceOffset >> clip.sourceLoop;
                } else {
                    in >> clip.variation;
                }
            }
        }
        if (version < 5) {
            // Materialize legacy transposed variations as independent patterns.
            for (auto &clip : t.clips) {
                if (clip.audio && clip.name != "texture.wav") {
                    throw std::runtime_error("Unsupported legacy audio source");
                }
                if (!clip.audio && clip.variation) {
                    auto id = t.createPattern(true);
                    t.pattern().name = clip.name;
                    if (size_t(&t - p.tracks.data()) != 2) {
                        for (auto &n : t.pattern().notes) {
                            n.key = std::min(127, n.key + 12);
                        }
                        // Legacy steps also transposed except on the drum lane.
                        for (int step = 0; step < 16; ++step) {
                            if (t.pattern().steps[step]) {
                                t.pattern().notes.push_back({72, step * .25, .2, .85f});
                                t.pattern().steps[step] = false;
                            }
                        }
                    }
                    clip.patternId = id;
                    clip.variation = false;
                    t.activePattern = 1;
                }
            }
        }
    }
    if (version < 5) {
        p.sources.push_back(textureSource());
        buildPeaks(p.sources[0]);
    }
    if (!in || !p.valid()) {
        throw std::runtime_error("Invalid project data");
    }
    in >> std::ws;
    if (!in.eof()) {
        throw std::runtime_error("Unexpected project data");
    }
    // Parse and validate the complete document before accessing external files.
    size_t loadedBytes = 0;
    for (auto &source : p.sources) {
        if (source.path.empty()) {
            source.pcm = textureSource().pcm;
            buildPeaks(source);
        } else {
            try {
                auto restored = readSource(source.path);
                if (restored.originalRate != source.originalRate ||
                    restored.originalFrames != source.originalFrames ||
                    restored.fingerprint != source.fingerprint) {
                    throw std::runtime_error("File changed; relink explicitly");
                }
                if (restored.pcm->size() * sizeof(float) > 128 * 1024 * 1024 - loadedBytes) {
                    throw std::runtime_error("Project PCM budget exceeded");
                }
                source.pcm = restored.pcm;
                source.peaks = restored.peaks;
            } catch (const std::exception &error) {
                source.error = error.what();
            }
        }
        if (source.pcm) {
            loadedBytes += source.pcm->size() * sizeof(float);
        }
    }
    return p;
}
void renderProject(const Project &project, const std::filesystem::path &path) {
    if (!project.sourceWarnings().empty()) {
        throw std::runtime_error("Cannot export missing/changed audio sources: " + project.sourceWarnings());
    }
    audio::Engine engine(48000);
    auto plan = project.prepare(engine);
    if (!engine.submit(plan)) {
        throw std::runtime_error("Cannot submit render plan");
    }
    engine.looping(false);
    engine.play(true);
    media::Pcm pcm;
    pcm.samples.resize(size_t(plan.loopFrames) * 2);
    for (size_t at = 0; at < pcm.samples.size(); at += 512) {
        engine.process(std::span(pcm.samples).subspan(at, std::min<size_t>(512, pcm.samples.size() - at)));
    }
    const auto bytes = media::encodeWav(pcm);
    atomicWrite(path, {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
}
} // namespace ally
