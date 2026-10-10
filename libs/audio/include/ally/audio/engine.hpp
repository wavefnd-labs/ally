#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace ally::audio {
struct NoteEvent {
    uint64_t start = 0, duration = 1;
    uint8_t key = 60, track = 0;
    float velocity = 0.7f;
};
struct SampleEvent {
    uint64_t start = 0, duration = 1;
    uint8_t track = 0;
};
struct Plan {
    static constexpr size_t capacity = 512, maxTracks = 8, sampleCapacity = 32;
    uint64_t loopFrames = 96000;
    uint32_t count = 0, sampleCount = 0, clickCount = 0;
    uint64_t loopEndFrame = 0;
    float masterGain = 1;
    std::array<NoteEvent, capacity> notes{};
    std::array<float, maxTracks> gains{}, cutoffs{};
    std::array<uint32_t, maxTracks> attacks{}, releases{};
    std::array<SampleEvent, sampleCapacity> samples{};
    std::array<uint64_t, 32> clicks{};
    Plan() {
        gains.fill(.6f);
        cutoffs.fill(20000);
        attacks.fill(96);
        releases.fill(240);
    }
};
struct MidiEvent {
    uint8_t key = 60;
    uint8_t velocity = 0;
};
struct AuditionEvent {
    uint8_t key = 60, track = 0, velocity = 0;
};
// Bounded SPSC queue. Exactly one producer and one consumer; no allocations or locks.
template <typename T, size_t N> class SpscQueue {
  public:
    bool push(const T &value) noexcept {
        const auto w = write_.load(std::memory_order_relaxed);
        const auto next = (w + 1) % N;
        if (next == read_.load(std::memory_order_acquire)) {
            return false;
        }
        data_[w] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T &value) noexcept {
        const auto r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) {
            return false;
        }
        value = data_[r];
        read_.store((r + 1) % N, std::memory_order_release);
        return true;
    }

  private:
    std::array<T, N> data_{};
    alignas(64) std::atomic<size_t> read_{0};
    alignas(64) std::atomic<size_t> write_{0};
};
class Engine {
  public:
    explicit Engine(uint32_t sampleRate = 48000, std::span<const float> stereoTexture = {});
    bool submit(const Plan &plan) noexcept;      // UI producer. Rejects invalid plans/full queue.
    bool midi(MidiEvent event) noexcept;         // MIDI producer, separate from UI.
    bool audition(AuditionEvent event) noexcept; // UI producer, isolated from MIDI input.
    void looping(bool value) noexcept {
        looping_.store(value);
    }
    void metronome(bool value) noexcept {
        metronome_.store(value);
    }
    void seek(uint64_t frame) noexcept {
        seek_.store(frame);
    }
    void play(bool value) noexcept {
        playing_.store(value);
    }
    bool playing() const noexcept {
        return playing_.load();
    }
    void rewind() noexcept {
        reset_.store(true);
    }
    uint64_t position() const noexcept {
        return publishedFrame_.load();
    }
    float peak() const noexcept {
        return peak_.load();
    }
    float peak(size_t channel) const noexcept {
        return channel < 2 ? stereoPeaks_[channel].load() : 0;
    }
    float trackPeak(size_t track, size_t channel) const noexcept {
        return track < Plan::maxTracks && channel < 2 ? trackStereoPeaks_[track][channel].load() : 0;
    }
    float trackPeak(size_t track) const noexcept {
        return track < Plan::maxTracks ? trackPeaks_[track].load() : 0;
    }
    uint32_t sampleRate() const noexcept {
        return sampleRate_;
    }
    // Audio owner only, stereo interleaved. No allocation, I/O, or blocking synchronization.
    void process(std::span<float> stereo) noexcept;

  private:
    struct Voice {
        double phase = 0, increment = 0;
        float filtered = 0;
        uint64_t remaining = 0, age = 0, length = 0;
        uint8_t key = 0, track = 0;
        float velocity = 0;
        bool live = false, audition = false;
    };
    void trigger(uint8_t key, uint8_t track, float velocity, uint64_t duration, bool live,
                 bool audition = false) noexcept;
    uint32_t sampleRate_;
    Plan plan_{};
    SpscQueue<Plan, 4> plans_;
    SpscQueue<MidiEvent, 256> midi_;
    SpscQueue<AuditionEvent, 64> auditions_;
    std::array<Voice, 64> voices_{};
    uint64_t frame_ = 0;
    std::vector<float> texture_;
    uint32_t clickRemaining_ = 0;
    std::atomic<bool> playing_{false}, reset_{false}, looping_{true}, metronome_{false};
    std::atomic<uint64_t> seek_{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> publishedFrame_{0};
    std::atomic<float> peak_{0};
    std::array<std::atomic<float>, Plan::maxTracks> trackPeaks_{};
    std::array<std::atomic<float>, 2> stereoPeaks_{};
    std::array<std::array<std::atomic<float>, 2>, Plan::maxTracks> trackStereoPeaks_{};
};
static_assert(std::atomic<size_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<float>::is_always_lock_free);
} // namespace ally::audio
