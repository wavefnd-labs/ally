#include <algorithm>
#include <ally/audio/engine.hpp>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ally::audio {
bool validStrip(const Strip &s, int firstBus) noexcept {
    auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    return range(s.pan, -1, 1) && range(s.lowDb, -24, 24) && range(s.midDb, -24, 24) &&
           range(s.highDb, -24, 24) && range(s.thresholdDb, -60, 0) && range(s.ratio, 1, 20) &&
           range(s.delayWet, 0, 1) && range(s.delaySeconds, .01f, 1) && range(s.feedback, 0, .9f) &&
           s.lookaheadFrames <= Strip::maxLookahead &&
           (s.route == -1 || (s.route >= firstBus && s.route < int(Plan::maxBuses))) &&
           std::all_of(s.sends.begin(), s.sends.end(), [&](float v) { return range(v, 0, 1); });
}
Engine::Engine(uint32_t sampleRate, std::span<const float> stereoTexture) : sampleRate_(sampleRate) {
    if (stereoTexture.size() % 2 || stereoTexture.size() > size_t(sampleRate) * 240 ||
        !std::all_of(stereoTexture.begin(), stereoTexture.end(),
                     [](float s) { return std::isfinite(s) && std::abs(s) <= 1; })) {
        throw std::invalid_argument("Invalid stereo texture buffer");
    }
    if (sampleRate < 8000 || sampleRate > 384000) {
        throw std::invalid_argument("Invalid sample rate");
    }
    plan_ = std::make_unique<Plan>();
    pendingPlan_ = std::make_unique<Plan>();
    texture_.assign(stereoTexture.begin(), stereoTexture.end());
    plans_ = std::make_unique<SpscQueue<Plan, 4>>();
    monitor_ = std::make_unique<SpscQueue<std::array<float, 2>, 16384>>();
    for (auto &state : dsp_) {
        state.delay.resize(sampleRate_ + 1);
        state.lookahead.resize(Strip::maxLookahead + 1);
    }
    alignmentStorage_.resize(alignment_.size() * (maxCompensation + 1));
    coefficients();
}

std::span<const float> Engine::prepareSample(std::shared_ptr<const std::vector<float>> sample) {
    if (!sample) {
        return {};
    }
    for (const auto &existing : prepared_) {
        if (existing == sample) {
            return *existing;
        }
    }
    if (sample->empty() || sample->size() % 2 || sample->size() > 16 * 1024 * 1024 ||
        sample->size() * sizeof(float) > 256 * 1024 * 1024 - preparedBytes_ ||
        !std::all_of(sample->begin(), sample->end(),
                     [](float v) { return std::isfinite(v) && std::abs(v) <= 1; })) {
        throw std::runtime_error("Prepared audio exceeds engine memory limits (restart to reclaim)");
    }
    prepared_.push_back(sample);
    preparedBytes_ += sample->size() * sizeof(float);
    return *sample;
}

bool Engine::submit(const Plan &plan) noexcept {
    if (!plan.loopFrames || plan.count > Plan::capacity || plan.sampleCount > Plan::sampleCapacity ||
        !std::isfinite(plan.clickIntervalFrames) || plan.clickIntervalFrames < 0 ||
        (plan.clickIntervalFrames > 0 && plan.clickIntervalFrames < 1) ||
        plan.clickCount > plan.clicks.size() || plan.loopEndFrame > plan.loopFrames ||
        !std::isfinite(plan.masterGain) || plan.masterGain < 0 || plan.masterGain > 1) {
        return false;
    }
    for (float gain : plan.gains) {
        if (!std::isfinite(gain) || gain < 0 || gain > 1) {
            return false;
        }
    }
    for (size_t i = 0; i < Plan::maxTracks; ++i) {
        if (!std::isfinite(plan.cutoffs[i]) || plan.cutoffs[i] < 20 || plan.cutoffs[i] > 20000 ||
            !plan.attacks[i] || plan.attacks[i] > sampleRate_ * 2 || !plan.releases[i] ||
            plan.releases[i] > sampleRate_ * 2) {
            return false;
        }
    }
    if (!validStrip(plan.master, int(Plan::maxBuses))) {
        return false;
    }
    for (size_t i = 0; i < Plan::maxBuses; ++i) {
        if (!validStrip(plan.buses[i], int(i + 1)) || !std::isfinite(plan.busGains[i]) ||
            plan.busGains[i] < 0 || plan.busGains[i] > 1 ||
            std::any_of(plan.buses[i].sends.begin(), plan.buses[i].sends.end(),
                        [](float v) { return v != 0; })) {
            return false;
        }
    }
    for (size_t i = 0; i < Plan::maxTracks; ++i) {
        if (!validStrip(plan.strips[i]) || plan.instruments[i] > Instrument::Drum || plan.roots[i] > 127 ||
            plan.waveforms[i] > 2 || plan.instrumentSources[i] >= Plan::sourceCapacity ||
            std::any_of(plan.drumSources[i].begin(), plan.drumSources[i].end(),
                        [](uint32_t s) { return s >= Plan::sourceCapacity; })) {
            return false;
        }
    }
    for (const auto *stream : plan.streams) {
        if (stream && !std::any_of(streams_.begin(), streams_.end(),
                                   [&](const auto &s) { return s.get() == stream; })) {
            return false;
        }
    }
    for (const auto source : plan.sources) {
        if (source.size() % 2) {
            return false;
        }
        if (!source.empty() && !std::any_of(prepared_.begin(), prepared_.end(), [&](const auto &owned) {
                return owned->data() == source.data() && owned->size() == source.size();
            })) {
            return false;
        }
    }
    for (uint32_t i = 0; i < plan.sampleCount; ++i) {
        const auto &s = plan.samples[i];
        if (s.offset > uint64_t(sampleRate_) * 21600 || s.source >= Plan::sourceCapacity ||
            s.track >= Plan::maxTracks || s.start >= plan.loopFrames || !s.duration ||
            s.duration > plan.loopFrames - s.start) {
            return false;
        }
    }
    for (uint32_t i = 0; i < plan.clickCount; ++i) {
        if (plan.clicks[i] >= plan.loopFrames || (i && plan.clicks[i] <= plan.clicks[i - 1])) {
            return false;
        }
    }
    for (uint32_t i = 0; i < plan.count; ++i) {
        const auto &n = plan.notes[i];
        if (n.start >= plan.loopFrames || !n.duration || n.duration > plan.loopFrames ||
            n.track >= Plan::maxTracks || n.key > 127 || !std::isfinite(n.velocity) || n.velocity < 0 ||
            n.velocity > 1 || (i && n.start < plan.notes[i - 1].start)) {
            return false;
        }
    }
    return plans_->push(plan);
}

bool Engine::midi(MidiEvent event) noexcept {
    if (event.key > 127 || event.velocity > 127 || event.track >= Plan::maxTracks) {
        return false;
    }
    return midi_.push(event);
}

bool Engine::audition(AuditionEvent event) noexcept {
    if (event.key > 127 || event.track >= Plan::maxTracks || event.velocity > 127) {
        return false;
    }
    return auditions_.push(event);
}

void Engine::trigger(uint8_t key, uint8_t track, float velocity, uint64_t duration, bool live,
                     bool audition) noexcept {
    auto it = std::find_if(voices_.begin(), voices_.end(), [](const Voice &v) { return !v.remaining; });
    if (it == voices_.end()) {
        it = std::min_element(voices_.begin(), voices_.end(),
                              [](const Voice &a, const Voice &b) { return a.remaining < b.remaining; });
    }
    *it = {};
    it->increment = 440.0 * std::exp2((double(key) - 69.0) / 12.0) / sampleRate_;
    it->remaining = it->length = duration;
    it->key = key;
    it->track = track;
    it->velocity = velocity;
    it->source = plan_->instrumentSources[track];
    if (plan_->instruments[track] == Instrument::Drum) {
        it->source = plan_->drumSources[track][size_t(key >= 36 && key < 52 ? key - 36 : 0)];
        if (auto *stream = plan_->streams[it->source]) {
            it->remaining = it->length = stream->frames();
        } else if (!plan_->sources[it->source].empty()) {
            it->remaining = it->length = plan_->sources[it->source].size() / 2;
        }
    }
    it->sampleStep = plan_->instruments[track] == Instrument::Drum
                         ? 1
                         : std::exp2((int(key) - int(plan_->roots[track])) / 12.0);
    it->live = live;
    it->audition = audition;
}

SampleStream *Engine::prepareStream(std::shared_ptr<SampleStream> stream) {
    if (!stream) {
        return nullptr;
    }
    if (!stream->frames()) {
        throw std::runtime_error("Stream has no output frames");
    }
    for (const auto &existing : streams_) {
        if (existing == stream) {
            return existing.get();
        }
    }
    if (streams_.size() >= 64) {
        throw std::runtime_error("Stream lifetime limit: restart to reclaim");
    }
    streams_.push_back(std::move(stream));
    return streams_.back().get();
}
uint64_t Engine::timestamp() const noexcept {
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    // Read a coherent clock/anchor pair without waiting for the audio owner.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const auto version = clockVersion_.load();
        const auto anchor = anchorNs_.load();
        const auto frame = clock_.load();
        if (!(version & 1) && version == clockVersion_.load()) {
            const auto elapsed = anchor ? std::clamp<int64_t>(now - anchor, 0, 50000000) : 0;
            return frame + uint64_t(elapsed) * sampleRate_ / 1000000000;
        }
    }
    return clock_.load();
}
uint64_t Engine::streamUnderruns() const noexcept {
    uint64_t total = 0;
    // Called by the UI owner, which also registers sources.
    for (const auto &s : streams_) {
        total += s->underruns();
    }
    return total;
}
void Engine::prefetch(const Plan &plan, uint64_t frame, bool wait) {
    for (uint32_t i = 0; i < plan.sampleCount; ++i) {
        const auto &event = plan.samples[i];
        if (auto *stream = plan.streams[event.source]; stream && frame < event.start + event.duration) {
            auto at = event.offset + (frame > event.start ? frame - event.start : 0);
            if (event.loop) {
                at %= stream->frames();
            }
            if (at < stream->frames()) {
                stream->prefetch(at, wait);
            }
        }
    }
}
void Engine::monitorInput(std::span<const float> stereo) noexcept {
    if (monitorTrack_.load() < 0) {
        return;
    }
    for (size_t i = 0; i + 1 < stereo.size(); i += 2) {
        if (!monitor_->push({stereo[i], stereo[i + 1]})) {
            break;
        }
    }
}
std::array<float, 2> Engine::readSource(uint32_t source, uint64_t frame, bool loop) noexcept {
    if (auto *stream = plan_->streams[source]) {
        if (loop) {
            frame %= stream->frames();
        }
        if (frame >= stream->frames()) {
            return {};
        }
        std::array<float, 2> signal{};
        stream->read(frame, signal[0], signal[1]);
        return signal;
    }
    const auto buffer = plan_->sourcesBound ? plan_->sources[source] : std::span<const float>(texture_);
    if (buffer.empty() || (!loop && frame >= buffer.size() / 2)) {
        return {};
    }
    const auto at = size_t(frame % (buffer.size() / 2)) * 2;
    return {buffer[at], buffer[at + 1]};
}
namespace {
struct Arrival {
    std::array<uint32_t, Plan::maxTracks> tracks{};
    std::array<uint32_t, Plan::maxBuses> inputs{}, buses{};
    uint32_t master = 0;
};
Arrival arrivals(const Plan &plan) noexcept {
    Arrival a;
    for (size_t track = 0; track < Plan::maxTracks; ++track) {
        a.tracks[track] = plan.strips[track].lookaheadFrames;
        const auto &strip = plan.strips[track];
        if (strip.route < 0) {
            a.master = std::max(a.master, a.tracks[track]);
        } else {
            auto &input = a.inputs[size_t(strip.route)];
            input = std::max(input, a.tracks[track]);
        }
        for (size_t bus = 0; bus < Plan::maxBuses; ++bus) {
            if (strip.sends[bus] > 0) {
                a.inputs[bus] = std::max(a.inputs[bus], a.tracks[track]);
            }
        }
    }
    for (size_t bus = 0; bus < Plan::maxBuses; ++bus) {
        a.buses[bus] = a.inputs[bus] + plan.buses[bus].lookaheadFrames;
        const int route = plan.buses[bus].route;
        if (route < 0) {
            a.master = std::max(a.master, a.buses[bus]);
        } else {
            auto &input = a.inputs[size_t(route)];
            input = std::max(input, a.buses[bus]);
        }
    }
    return a;
}
} // namespace
uint32_t planLatencyFrames(const Plan &plan) noexcept {
    if (!validStrip(plan.master, int(Plan::maxBuses))) {
        return 0;
    }
    for (const auto &strip : plan.strips) {
        if (!validStrip(strip)) {
            return 0;
        }
    }
    for (size_t bus = 0; bus < Plan::maxBuses; ++bus) {
        if (!validStrip(plan.buses[bus], int(bus + 1))) {
            return 0;
        }
    }
    return arrivals(plan).master + plan.master.lookaheadFrames;
}
void Engine::prepareAlignment() noexcept {
    const auto a = arrivals(*plan_);
    for (size_t node = 0; node < nodes; ++node) {
        const uint32_t source = node < Plan::maxTracks                    ? a.tracks[node]
                                : node < Plan::maxTracks + Plan::maxBuses ? a.buses[node - Plan::maxTracks]
                                                                          : 0;
        for (size_t dest = 0; dest < destinations; ++dest) {
            const auto target = dest < Plan::maxBuses ? a.inputs[dest] : a.master;
            auto &state = alignment_[node * destinations + dest];
            // Unconnected edges may have negative differences; they are never consumed.
            state.frames = target > source ? target - source : 0;
        }
    }
    graphLatency_.store(a.master + plan_->master.lookaheadFrames);
}
std::array<float, 2> Engine::align(std::array<float, 2> signal, size_t source, size_t dest) noexcept {
    const size_t index = source * destinations + dest;
    auto &state = alignment_[index];
    if (!state.frames) {
        return signal;
    }
    const auto size = size_t(state.frames) + 1;
    const size_t base = index * (maxCompensation + 1);
    auto output = state.valid >= state.frames
                      ? alignmentStorage_[base + (state.cursor + size - state.frames) % size]
                      : std::array<float, 2>{};
    alignmentStorage_[base + state.cursor] = signal;
    state.cursor = (state.cursor + 1) % size;
    state.valid = std::min(state.valid + 1, size);
    return output;
}
void Engine::coefficients() noexcept {
    prepareAlignment();
    for (size_t i = 0; i < dsp_.size(); ++i) {
        const auto &s = i < Plan::maxTracks                    ? plan_->strips[i]
                        : i < Plan::maxTracks + Plan::maxBuses ? plan_->buses[i - Plan::maxTracks]
                                                               : plan_->master;
        lowGain_[i] = std::pow(10.0f, s.lowDb / 20);
        midGain_[i] = std::pow(10.0f, s.midDb / 20);
        highGain_[i] = std::pow(10.0f, s.highDb / 20);
        thresholds_[i] = std::pow(10.0f, s.thresholdDb / 20);
    }
}
void Engine::resetDsp() noexcept {
    for (auto &state : dsp_) {
        state.low = state.high = {};
        state.envelope = 0;
        state.cursor = state.delayValid = 0;
        state.lookaheadCursor = state.lookaheadValid = 0;
    }
    for (auto &state : alignment_) {
        state.cursor = state.valid = 0;
    }
}

std::array<float, 2> Engine::processStrip(std::array<float, 2> signal, const Strip &s,
                                          size_t index) noexcept {
    auto &state = dsp_[index];
    if (s.lowDb != 0 || s.midDb != 0 || s.highDb != 0) {
        const float lowAlpha = 1 - std::exp(float(-2 * std::numbers::pi * 250 / sampleRate_));
        const float highAlpha = 1 - std::exp(float(-2 * std::numbers::pi * 4000 / sampleRate_));
        for (size_t ch = 0; ch < 2; ++ch) {
            state.low[ch] += lowAlpha * (signal[ch] - state.low[ch]);
            state.high[ch] += highAlpha * (signal[ch] - state.high[ch]);
            signal[ch] = state.low[ch] * lowGain_[index] +
                         (state.high[ch] - state.low[ch]) * midGain_[index] +
                         (signal[ch] - state.high[ch]) * highGain_[index];
        }
    }
    float compression = 1;
    if (s.thresholdDb < 0) {
        float peak = std::max(std::abs(signal[0]), std::abs(signal[1]));
        const float alpha = peak > state.envelope ? .02f : .0002f;
        state.envelope += alpha * (peak - state.envelope);
        if (state.envelope > thresholds_[index]) {
            compression = std::pow(thresholds_[index] / state.envelope, 1 - 1 / s.ratio);
        }
    }
    if (s.lookaheadFrames) {
        const auto size = size_t(s.lookaheadFrames) + 1;
        auto delayed = state.lookaheadValid >= s.lookaheadFrames
                           ? state.lookahead[(state.lookaheadCursor + size - s.lookaheadFrames) % size]
                           : std::array<float, 2>{};
        state.lookahead[state.lookaheadCursor] = signal;
        state.lookaheadCursor = (state.lookaheadCursor + 1) % size;
        state.lookaheadValid = std::min(state.lookaheadValid + 1, size);
        signal = delayed;
    }
    signal[0] *= compression;
    signal[1] *= compression;
    if (s.delayWet > 0) {
        size_t distance = std::clamp(size_t(s.delaySeconds * sampleRate_), size_t(1), state.delay.size() - 1);
        auto echo = state.delayValid >= distance
                        ? state.delay[(state.cursor + state.delay.size() - distance) % state.delay.size()]
                        : std::array<float, 2>{};
        state.delay[state.cursor] = {signal[0] + echo[0] * s.feedback, signal[1] + echo[1] * s.feedback};
        state.cursor = (state.cursor + 1) % state.delay.size();
        state.delayValid = std::min(state.delayValid + 1, state.delay.size());
        signal[0] += echo[0] * s.delayWet;
        signal[1] += echo[1] * s.delayWet;
    }
    signal[0] *= std::min(1.0f, 1 - s.pan);
    signal[1] *= std::min(1.0f, 1 + s.pan);
    return signal;
}

void Engine::process(std::span<float> out) noexcept {
    auto &pending = *pendingPlan_;
    bool changed = false;
    while (plans_->pop(pending)) {
        *plan_ = pending;
        changed = true;
    }
    if (changed) {
        voices_.fill({});
        clickRemaining_ = 0;
        coefficients();
        resetDsp();
        frame_ %= plan_->loopFrames;
    }
    if (reset_.exchange(false)) {
        resetDsp();
        frame_ = 0;
        voices_.fill({});
        clickRemaining_ = 0;
    }
    const auto seek = seek_.exchange(std::numeric_limits<uint64_t>::max());
    if (seek != std::numeric_limits<uint64_t>::max()) {
        resetDsp();
        frame_ = seek % plan_->loopFrames;
        voices_.fill({});
        clickRemaining_ = 0;
    }
    MidiEvent nextMidi = pendingMidi_;
    bool haveMidi = hasPendingMidi_ || midi_.pop(nextMidi);
    hasPendingMidi_ = false;
    AuditionEvent preview;
    while (auditions_.pop(preview)) {
        if (preview.velocity) {
            trigger(preview.key, preview.track, float(preview.velocity) / 127.0f, sampleRate_ * 10, true,
                    true);
        } else {
            for (auto &v : voices_) {
                if (v.audition && v.key == preview.key && v.track == preview.track &&
                    plan_->instruments[v.track] != Instrument::Drum) {
                    v.remaining = std::min<uint64_t>(v.remaining, 240);
                }
            }
        }
    }
    bool run = playing_.load();
    const bool loop = looping_.load(), click = metronome_.load();
    float peak = 0;
    std::array<float, Plan::maxTracks> trackPeaks{};
    std::array<float, 2> stereoPeaks{};
    std::array<std::array<float, 2>, Plan::maxTracks> trackStereoPeaks{};
    for (size_t i = 0; i + 1 < out.size(); i += 2) {
        while (haveMidi && nextMidi.sampleTime <= clockFrame_) {
            if (nextMidi.velocity) {
                trigger(nextMidi.key, nextMidi.track, float(nextMidi.velocity) / 127, sampleRate_ * 30, true);
            } else {
                for (auto &v : voices_) {
                    if (v.live && !v.audition && v.key == nextMidi.key && v.track == nextMidi.track &&
                        plan_->instruments[v.track] != Instrument::Drum) {
                        v.remaining = std::min<uint64_t>(v.remaining, plan_->releases[v.track]);
                    }
                }
            }
            haveMidi = midi_.pop(nextMidi);
        }
        if (run) {
            const auto begin =
                std::lower_bound(plan_->notes.begin(), plan_->notes.begin() + plan_->count, frame_,
                                 [](const NoteEvent &n, uint64_t f) { return n.start < f; });
            for (auto it = begin; it != plan_->notes.begin() + plan_->count && it->start == frame_; ++it) {
                trigger(it->key, it->track, it->velocity, it->duration, false);
            }
        }
        float left = 0, right = 0;
        std::array<std::array<float, 2>, Plan::maxTracks> trackSample{};
        for (auto &v : voices_) {
            if (!v.remaining || (!run && !v.live)) {
                continue;
            }
            const float attack = std::min(1.0f, float(v.age) / plan_->attacks[v.track]);
            const float release = std::min(1.0f, float(v.remaining) / plan_->releases[v.track]);
            std::array<float, 2> signal{};
            if (plan_->instruments[v.track] == Instrument::Synth) {
                double wave = std::sin(2 * std::numbers::pi * v.phase);
                if (plan_->waveforms[v.track] == 1) {
                    // PolyBLEP saw reduces the discontinuity without per-voice allocations.
                    double t = v.phase, dt = v.increment, correction = 0;
                    if (t < dt) {
                        t /= dt;
                        correction = t + t - t * t - 1;
                    } else if (t > 1 - dt) {
                        t = (t - 1) / dt;
                        correction = t * t + t + t + 1;
                    }
                    wave = 2 * v.phase - 1 - correction;
                } else if (plan_->waveforms[v.track] == 2) {
                    wave = 1 - 4 * std::abs(v.phase - .5);
                }
                const float alpha =
                    std::min(1.0f, float(2 * std::numbers::pi * plan_->cutoffs[v.track] / sampleRate_));
                v.filtered += alpha * (float(wave) - v.filtered);
                // Keep the established centered instrument stereo balance.
                signal = {v.filtered * .2f, v.filtered * .14f};
            } else {
                const auto at = uint64_t(v.samplePosition);
                auto a = readSource(v.source, at, false);
                auto b = readSource(v.source, at + 1, false);
                const auto fraction = float(v.samplePosition - double(at));
                signal = {std::lerp(a[0], b[0], fraction), std::lerp(a[1], b[1], fraction)};
                const float alpha =
                    std::min(1.0f, float(2 * std::numbers::pi * plan_->cutoffs[v.track] / sampleRate_));
                v.filtered += alpha * (signal[0] - v.filtered);
                v.filteredRight += alpha * (signal[1] - v.filteredRight);
                signal = {v.filtered, v.filteredRight};
                v.samplePosition += v.sampleStep;
            }
            for (size_t channel = 0; channel < 2; ++channel) {
                trackSample[v.track][channel] += signal[channel] * v.velocity * attack * release;
            }
            v.phase += v.increment;
            v.phase -= std::floor(v.phase);
            ++v.age;
            --v.remaining;
        }
        if (run) {
            for (uint32_t n = 0; n < plan_->sampleCount; ++n) {
                const auto &event = plan_->samples[n];
                if (frame_ >= event.start && frame_ - event.start < event.duration) {
                    auto signal = readSource(event.source, event.offset + frame_ - event.start, event.loop);
                    trackSample[event.track][0] += signal[0];
                    trackSample[event.track][1] += signal[1];
                }
            }
            const bool beatClick =
                plan_->clickIntervalFrames > 0
                    ? frame_ ==
                          uint64_t(std::llround(std::round(double(frame_) / plan_->clickIntervalFrames) *
                                                plan_->clickIntervalFrames))
                    : std::binary_search(plan_->clicks.begin(), plan_->clicks.begin() + plan_->clickCount,
                                         frame_);
            if (click && beatClick) {
                clickRemaining_ = 960;
            }
        }
        if (clickRemaining_ && run && click) {
            float s = float(std::sin(double(clickRemaining_) * .18)) * .12f * float(clickRemaining_) / 960;
            auto signal = align({s, s}, nodes - 1, Plan::maxBuses);
            left += signal[0];
            right += signal[1];
            --clickRemaining_;
        } else {
            auto signal = align({}, nodes - 1, Plan::maxBuses);
            left += signal[0];
            right += signal[1];
        }
        std::array<float, 2> input{};
        if (monitor_->pop(input)) {
            const int target = monitorTrack_.load();
            if (target >= 0 && target < int(Plan::maxTracks)) {
                trackSample[size_t(target)][0] += input[0];
                trackSample[size_t(target)][1] += input[1];
            }
        }
        std::array<std::array<float, 2>, Plan::maxBuses> busSample{};
        auto route = [&](const std::array<float, 2> &raw, int target, size_t source) {
            const auto signal = align(raw, source, target < 0 ? Plan::maxBuses : size_t(target));
            if (target < 0) {
                left += signal[0];
                right += signal[1];
            } else {
                busSample[size_t(target)][0] += signal[0];
                busSample[size_t(target)][1] += signal[1];
            }
        };
        for (size_t track = 0; track < Plan::maxTracks; ++track) {
            auto signal = processStrip(trackSample[track], plan_->strips[track], track);
            for (size_t channel = 0; channel < 2; ++channel) {
                signal[channel] *= plan_->gains[track];
                const float magnitude = std::abs(signal[channel]);
                trackStereoPeaks[track][channel] = std::max(trackStereoPeaks[track][channel], magnitude);
                trackPeaks[track] = std::max(trackPeaks[track], magnitude);
            }
            auto mainSignal = signal;
            const int target = plan_->strips[track].route;
            if (target >= 0) {
                mainSignal[0] *= 1 + plan_->strips[track].sends[size_t(target)];
                mainSignal[1] *= 1 + plan_->strips[track].sends[size_t(target)];
            }
            route(mainSignal, target, track);
            for (size_t bus = 0; bus < Plan::maxBuses; ++bus) {
                if (plan_->strips[track].sends[bus] != 0 && int(bus) != target) {
                    auto sent = align(signal, track, bus);
                    busSample[bus][0] += sent[0] * plan_->strips[track].sends[bus];
                    busSample[bus][1] += sent[1] * plan_->strips[track].sends[bus];
                }
            }
        }
        // Prepared acyclic order: each bus may only feed a later bus or master.
        for (size_t bus = 0; bus < Plan::maxBuses; ++bus) {
            auto signal = processStrip(busSample[bus], plan_->buses[bus], Plan::maxTracks + bus);
            signal[0] *= plan_->busGains[bus];
            signal[1] *= plan_->busGains[bus];
            route(signal, plan_->buses[bus].route, Plan::maxTracks + bus);
        }
        auto master = processStrip({left, right}, plan_->master, Plan::maxTracks + Plan::maxBuses);
        left = master[0];
        right = master[1];
        out[i] = std::clamp(left * plan_->masterGain, -1.0f, 1.0f);
        out[i + 1] = std::clamp(right * plan_->masterGain, -1.0f, 1.0f);
        peak = std::max({peak, std::abs(out[i]), std::abs(out[i + 1])});
        stereoPeaks[0] = std::max(stereoPeaks[0], std::abs(out[i]));
        stereoPeaks[1] = std::max(stereoPeaks[1], std::abs(out[i + 1]));
        ++clockFrame_;
        if (run) {
            ++frame_;
            uint64_t end = loop && plan_->loopEndFrame ? plan_->loopEndFrame : plan_->loopFrames;
            if (frame_ >= end) {
                if (loop) {
                    frame_ = 0;
                } else {
                    frame_ = plan_->loopFrames - 1;
                    run = false;
                    playing_.store(false);
                }
            }
        }
    }
    if (out.size() & 1) {
        out.back() = 0;
    }
    if (!run) {
        for (auto &v : voices_) {
            if (!v.live) {
                v.remaining = 0;
            }
        }
    }
    peak_.store(peak);
    for (size_t i = 0; i < Plan::maxTracks; ++i) {
        trackPeaks_[i].store(trackPeaks[i]);
        for (size_t channel = 0; channel < 2; ++channel) {
            trackStereoPeaks_[i][channel].store(trackStereoPeaks[i][channel]);
        }
    }
    for (size_t channel = 0; channel < 2; ++channel) {
        stereoPeaks_[channel].store(stereoPeaks[channel]);
    }
    hasPendingMidi_ = haveMidi;
    pendingMidi_ = nextMidi;
    publishedFrame_.store(frame_);
    clockVersion_.fetch_add(1);
    clock_.store(clockFrame_);
    anchorNs_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
    clockVersion_.fetch_add(1);
}
} // namespace ally::audio
