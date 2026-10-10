#pragma once
#include <ally/audio/engine.hpp>
#include <array>
#include <filesystem>
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
    bool operator==(const Clip &) const = default;
};
struct Track {
    std::string name;
    float gain = 0.65f, cutoff = 16000, attack = .002f, release = .005f;
    bool muted = false, solo = false;
    std::vector<Note> notes;
    std::array<bool, 16> steps{};
    std::vector<Clip> clips;
    bool operator==(const Track &) const = default;
};
struct Project {
    double bpm = 124;
    float masterGain = .78f;
    bool masterMuted = false, songMode = true, loop = true, metronome = false;
    std::vector<Track> tracks;
    bool operator==(const Project &) const = default;
    static Project demo();
    bool valid() const noexcept;
    audio::Plan compile(uint32_t sampleRate) const;
    void save(const std::filesystem::path &path) const;
    static Project load(const std::filesystem::path &path);
};
std::vector<float> makeTexture(uint32_t sampleRate = 48000);
void renderProject(const Project &project, const std::filesystem::path &path);
} // namespace ally
