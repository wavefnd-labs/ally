#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
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
    uint32_t source = 0;
    uint64_t offset = 0;
    bool loop = true;
};
enum class Instrument : uint8_t { Synth, Sampler, Drum };
struct Strip {
    float pan = 0, lowDb = 0, midDb = 0, highDb = 0;
    float thresholdDb = 0, ratio = 4, delayWet = 0, delaySeconds = .25f, feedback = .3f;
    // Actual lookahead delay, not an artistic echo. Included in every route's compensation.
    uint32_t lookaheadFrames = 0;
    static constexpr uint32_t maxLookahead = 2048;
    int route = -1;
    std::array<float, 4> sends{};
    bool operator==(const Strip &) const = default;
};
bool validStrip(const Strip &strip, int firstBus = 0) noexcept;
// Prepared, bounded cache. The RT method must never allocate, wait, lock or access files.
class SampleStream {
  public:
    virtual ~SampleStream() = default;
    virtual uint64_t frames() const noexcept = 0;
    virtual bool read(uint64_t frame, float &left, float &right) noexcept = 0;
    // Offline owner may wait outside process(). Real-time playback only uses read().
    virtual void prefetch(uint64_t frame, bool wait = false) = 0;
    virtual uint64_t underruns() const noexcept = 0;
};
struct Plan {
    static constexpr size_t capacity = 4096, maxTracks = 32, sampleCapacity = 128, sourceCapacity = 32,
                            maxBuses = 4;
    uint64_t loopFrames = 96000;
    uint32_t count = 0, sampleCount = 0, clickCount = 0;
    double clickIntervalFrames = 0;
    uint64_t loopEndFrame = 0;
    float masterGain = 1;
    std::array<NoteEvent, capacity> notes{};
    std::array<float, maxTracks> gains{}, cutoffs{};
    std::array<uint32_t, maxTracks> attacks{}, releases{};
    std::array<SampleEvent, sampleCapacity> samples{};
    std::array<std::span<const float>, sourceCapacity> sources{};
    std::array<SampleStream *, sourceCapacity> streams{};
    std::array<Instrument, maxTracks> instruments{};
    std::array<uint32_t, maxTracks> instrumentSources{};
    std::array<std::array<uint32_t, 16>, maxTracks> drumSources{};
    std::array<uint8_t, maxTracks> roots{}, waveforms{};
    std::array<Strip, maxTracks> strips{};
    std::array<Strip, maxBuses> buses{};
    std::array<float, maxBuses> busGains{1, 1, 1, 1};
    Strip master{};
    bool sourcesBound = false;
    std::array<uint64_t, 1024> clicks{};
    Plan() {
        gains.fill(.6f);
        roots.fill(60);
        cutoffs.fill(20000);
        attacks.fill(96);
        releases.fill(240);
    }
};
// Total prepared graph latency, including buses and master. No allocations.
uint32_t planLatencyFrames(const Plan &) noexcept;
struct MidiEvent {
    uint8_t key = 60;
    uint8_t velocity = 0;
    uint8_t track = 0;
    uint64_t sampleTime = 0;
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
    size_t size() const noexcept {
        const auto r = read_.load(std::memory_order_acquire);
        const auto w = write_.load(std::memory_order_acquire);
        return (w + N - r) % N;
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
    // Non-RT owner prepares immutable PCM. Retained until engine destruction, bounded to 256 MiB.
    std::span<const float> prepareSample(std::shared_ptr<const std::vector<float>> sample);
    SampleStream *prepareStream(std::shared_ptr<SampleStream> stream);
    void prefetch(const Plan &plan, uint64_t frame, bool wait = false);
    uint64_t audioClock() const noexcept {
        return clock_.load();
    }
    uint64_t timestamp() const noexcept;
    uint32_t latencyFrames() const noexcept {
        return graphLatency_.load();
    }
    uint64_t streamUnderruns() const noexcept;
    // One native capture producer. Monitor is consumed by the sole output owner.
    void monitorInput(std::span<const float> stereo) noexcept;
    void monitorTrack(int track) noexcept {
        monitorTrack_.store(track);
    }
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
        double phase = 0, increment = 0, samplePosition = 0, sampleStep = 1;
        float filtered = 0, filteredRight = 0;
        uint64_t remaining = 0, age = 0, length = 0;
        uint8_t key = 0, track = 0;
        uint32_t source = 0;
        float velocity = 0;
        bool live = false, audition = false;
    };
    void trigger(uint8_t key, uint8_t track, float velocity, uint64_t duration, bool live,
                 bool audition = false) noexcept;
    uint32_t sampleRate_;
    std::unique_ptr<Plan> plan_, pendingPlan_;
    std::unique_ptr<SpscQueue<Plan, 4>> plans_;
    SpscQueue<MidiEvent, 256> midi_;
    MidiEvent pendingMidi_{};
    bool hasPendingMidi_ = false;
    SpscQueue<AuditionEvent, 64> auditions_;
    std::array<Voice, 64> voices_{};
    uint64_t frame_ = 0;
    std::vector<float> texture_;
    std::vector<std::shared_ptr<const std::vector<float>>> prepared_;
    std::vector<std::shared_ptr<SampleStream>> streams_;
    std::unique_ptr<SpscQueue<std::array<float, 2>, 16384>> monitor_;
    std::atomic<int> monitorTrack_{-1};
    std::atomic<uint64_t> clock_{0};
    uint64_t clockFrame_ = 0;
    std::atomic<int64_t> anchorNs_{0};
    std::atomic<uint64_t> clockVersion_{0};
    struct DspState {
        std::array<float, 2> low{}, high{};
        float envelope = 0;
        size_t cursor = 0, delayValid = 0;
        std::vector<std::array<float, 2>> delay;
        std::vector<std::array<float, 2>> lookahead;
        size_t lookaheadCursor = 0, lookaheadValid = 0;
    };
    std::array<DspState, Plan::maxTracks + Plan::maxBuses + 1> dsp_;
    std::array<float, Plan::maxTracks + Plan::maxBuses + 1> lowGain_{}, midGain_{}, highGain_{},
        thresholds_{};
    static constexpr size_t destinations = Plan::maxBuses + 1;
    static constexpr size_t nodes = Plan::maxTracks + Plan::maxBuses + 1;
    static constexpr size_t maxCompensation = Strip::maxLookahead * (Plan::maxBuses + 1);
    struct Alignment {
        uint32_t frames = 0;
        size_t cursor = 0, valid = 0;
    };
    std::array<Alignment, nodes * destinations> alignment_{};
    std::vector<std::array<float, 2>> alignmentStorage_;
    std::atomic<uint32_t> graphLatency_{0};
    std::array<float, 2> align(std::array<float, 2>, size_t source, size_t destination) noexcept;
    void prepareAlignment() noexcept;
    void coefficients() noexcept;
    void resetDsp() noexcept;
    std::array<float, 2> processStrip(std::array<float, 2>, const Strip &, size_t) noexcept;
    std::array<float, 2> readSource(uint32_t source, uint64_t frame, bool loop) noexcept;
    size_t preparedBytes_ = 0;
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
