#pragma once
#include <ally/audio/engine.hpp>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace ally {
struct Note {
    int key = 60;
    double beat = 0, length = 0.5;
    float velocity = 0.75f;
    bool operator==(const Note &) const = default;
};
struct Clip {
    std::string name = "Pattern 01";
    double beat = 0, length = 4;
    bool audio = false, variation = false;
    uint32_t patternId = 1, sourceId = 1;
    double sourceOffset = 0;
    bool sourceLoop = true;
    bool operator==(const Clip &) const = default;
};
struct Pattern {
    uint32_t id = 1;
    std::string name = "Pattern 01";
    std::vector<Note> notes;
    std::array<bool, 16> steps{};
    bool operator==(const Pattern &) const = default;
};
struct AudioSource {
    uint32_t id = 1;
    std::string name = "texture.wav";
    std::filesystem::path path;
    uint32_t originalRate = 48000;
    uint64_t originalFrames = 96000, fingerprint = 0;
    std::shared_ptr<const std::vector<float>> pcm;
    std::shared_ptr<audio::SampleStream> stream;
    std::array<float, 1024> peaks{};
    std::string error;
    bool operator==(const AudioSource &other) const;
};
struct Track {
    std::string name;
    float gain = 0.65f, cutoff = 16000, attack = .002f, release = .005f;
    bool muted = false, solo = false, armed = false;
    audio::Instrument instrument = audio::Instrument::Synth;
    uint32_t instrumentSource = 1;
    std::array<uint32_t, 16> drumSources{};
    uint8_t rootKey = 60, waveform = 0;
    int inputChannel = 0; // 0 stereo, 1 input left, 2 input right
    audio::Strip strip{};
    std::vector<Pattern> patterns{Pattern{}};
    uint32_t activePattern = 1;
    std::vector<Clip> clips;
    Pattern &pattern();
    const Pattern &pattern() const;
    const Pattern *findPattern(uint32_t id) const noexcept;
    uint32_t createPattern(bool duplicate = false);
    bool operator==(const Track &) const = default;
};
struct Project {
    static constexpr double maxTimeline = 1024;
    double bpm = 124, timelineBeats = 32, loopBeats = 24;
    std::array<audio::Strip, audio::Plan::maxBuses> buses{};
    std::array<float, audio::Plan::maxBuses> busGains{1, 1, 1, 1};
    audio::Strip masterStrip{};
    float masterGain = .78f;
    bool masterMuted = false, songMode = true, loop = true, metronome = false;
    std::vector<Track> tracks;
    std::vector<AudioSource> sources;
    bool operator==(const Project &) const = default;
    static Project demo();
    static Project empty();
    bool valid() const noexcept;
    audio::Plan compile(uint32_t sampleRate) const;
    void save(const std::filesystem::path &path) const;
    static Project load(const std::filesystem::path &path);
    const AudioSource *source(uint32_t id) const noexcept;
    void importWav(const std::filesystem::path &path, size_t track, double beat = 0);
    void relinkSource(uint32_t id, const std::filesystem::path &path);
    audio::Plan prepare(audio::Engine &engine) const;
    std::string sourceWarnings() const;
};
std::vector<float> makeTexture(uint32_t sampleRate = 48000);
void renderProject(const Project &project, const std::filesystem::path &path);
} // namespace ally
