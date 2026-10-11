#include <algorithm>
#include <ally/audio/engine.hpp>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ally::audio {
Engine::Engine(uint32_t sampleRate, std::span<const float> stereoTexture) : sampleRate_(sampleRate) {
    if (stereoTexture.size() % 2 || stereoTexture.size() > size_t(sampleRate) * 240 ||
        !std::all_of(stereoTexture.begin(), stereoTexture.end(),
                     [](float s) { return std::isfinite(s) && std::abs(s) <= 1; })) {
        throw std::invalid_argument("Invalid stereo texture buffer");
    }
    if (sampleRate < 8000 || sampleRate > 384000) {
        throw std::invalid_argument("Invalid sample rate");
    }
    texture_.assign(stereoTexture.begin(), stereoTexture.end());
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
        plan.clickCount > 32 || plan.loopEndFrame > plan.loopFrames || !std::isfinite(plan.masterGain) ||
        plan.masterGain < 0 || plan.masterGain > 1) {
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
        if (s.offset > uint64_t(sampleRate_) * 1100 || s.source >= Plan::sampleCapacity ||
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
    return plans_.push(plan);
}

bool Engine::midi(MidiEvent event) noexcept {
    if (event.key > 127 || event.velocity > 127) {
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
    it->live = live;
    it->audition = audition;
}

void Engine::process(std::span<float> out) noexcept {
    Plan pending;
    bool changed = false;
    while (plans_.pop(pending)) {
        plan_ = pending;
        changed = true;
    }
    if (changed) {
        voices_.fill({});
        clickRemaining_ = 0;
        frame_ %= plan_.loopFrames;
    }
    if (reset_.exchange(false)) {
        frame_ = 0;
        voices_.fill({});
        clickRemaining_ = 0;
    }
    const auto seek = seek_.exchange(std::numeric_limits<uint64_t>::max());
    if (seek != std::numeric_limits<uint64_t>::max()) {
        frame_ = seek % plan_.loopFrames;
        voices_.fill({});
        clickRemaining_ = 0;
    }
    MidiEvent event;
    while (midi_.pop(event)) {
        if (event.velocity) {
            trigger(event.key, 0, float(event.velocity) / 127.0f, sampleRate_ * 10, true);
        } else {
            for (auto &v : voices_) {
                if (v.live && !v.audition && v.key == event.key) {
                    v.remaining = std::min<uint64_t>(v.remaining, 240);
                }
            }
        }
    }
    AuditionEvent preview;
    while (auditions_.pop(preview)) {
        if (preview.velocity) {
            trigger(preview.key, preview.track, float(preview.velocity) / 127.0f, sampleRate_ * 10, true,
                    true);
        } else {
            for (auto &v : voices_) {
                if (v.audition && v.key == preview.key && v.track == preview.track) {
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
        if (run) {
            const auto begin =
                std::lower_bound(plan_.notes.begin(), plan_.notes.begin() + plan_.count, frame_,
                                 [](const NoteEvent &n, uint64_t f) { return n.start < f; });
            for (auto it = begin; it != plan_.notes.begin() + plan_.count && it->start == frame_; ++it) {
                trigger(it->key, it->track, it->velocity, it->duration, false);
            }
        }
        float left = 0, right = 0;
        std::array<std::array<float, 2>, Plan::maxTracks> trackSample{};
        for (auto &v : voices_) {
            if (!v.remaining || (!run && !v.live)) {
                continue;
            }
            const float attack = std::min(1.0f, float(v.age) / plan_.attacks[v.track]);
            const float release = std::min(1.0f, float(v.remaining) / plan_.releases[v.track]);
            double wave = std::sin(2.0 * std::numbers::pi * v.phase);
            if (v.track == 1) {
                wave += 0.25 * std::sin(4.0 * std::numbers::pi * v.phase);
            }
            if (v.track == 2) {
                wave *= std::exp(-double(v.age) / (sampleRate_ * 0.07));
            }
            if (v.track == 3) {
                wave += 0.15 * std::sin(6.0 * std::numbers::pi * v.phase);
            }
            const float alpha =
                std::min(1.0f, float(2 * std::numbers::pi * plan_.cutoffs[v.track] / sampleRate_));
            v.filtered += alpha * (float(wave) - v.filtered);
            float s = v.filtered * v.velocity * attack * release * plan_.gains[v.track] * 0.2f;
            const float pan = float(v.track) * 0.13f;
            const float l = s * (1 - pan), r = s * (0.7f + pan);
            trackSample[v.track][0] += l;
            trackSample[v.track][1] += r;
            left += l;
            right += r;
            v.phase += v.increment;
            v.phase -= std::floor(v.phase);
            ++v.age;
            --v.remaining;
        }
        if (run) {
            for (uint32_t n = 0; n < plan_.sampleCount; ++n) {
                const auto &event = plan_.samples[n];
                const auto buffer =
                    plan_.sourcesBound ? plan_.sources[event.source] : std::span<const float>(texture_);
                if (!buffer.empty() && frame_ >= event.start && frame_ - event.start < event.duration) {
                    const auto sourceFrame = event.offset + frame_ - event.start;
                    if (!event.loop && sourceFrame >= buffer.size() / 2) {
                        continue;
                    }
                    const size_t at = size_t(sourceFrame % (buffer.size() / 2)) * 2;
                    float l = buffer[at] * plan_.gains[event.track],
                          r = buffer[at + 1] * plan_.gains[event.track];
                    left += l;
                    right += r;
                    trackSample[event.track][0] += l;
                    trackSample[event.track][1] += r;
                }
            }
            if (click &&
                std::binary_search(plan_.clicks.begin(), plan_.clicks.begin() + plan_.clickCount, frame_)) {
                clickRemaining_ = 960;
            }
        }
        if (clickRemaining_ && run && click) {
            float s = float(std::sin(double(clickRemaining_) * .18)) * .12f * float(clickRemaining_) / 960;
            left += s;
            right += s;
            --clickRemaining_;
        }
        for (size_t track = 0; track < Plan::maxTracks; ++track) {
            for (size_t channel = 0; channel < 2; ++channel) {
                const float magnitude = std::abs(trackSample[track][channel]);
                trackStereoPeaks[track][channel] = std::max(trackStereoPeaks[track][channel], magnitude);
                trackPeaks[track] = std::max(trackPeaks[track], magnitude);
            }
        }
        out[i] = std::clamp(left * plan_.masterGain, -1.0f, 1.0f);
        out[i + 1] = std::clamp(right * plan_.masterGain, -1.0f, 1.0f);
        peak = std::max({peak, std::abs(out[i]), std::abs(out[i + 1])});
        stereoPeaks[0] = std::max(stereoPeaks[0], std::abs(out[i]));
        stereoPeaks[1] = std::max(stereoPeaks[1], std::abs(out[i + 1]));
        if (run) {
            ++frame_;
            uint64_t end = loop && plan_.loopEndFrame ? plan_.loopEndFrame : plan_.loopFrames;
            if (frame_ >= end) {
                if (loop) {
                    frame_ = 0;
                } else {
                    frame_ = plan_.loopFrames - 1;
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
    publishedFrame_.store(frame_);
}
} // namespace ally::audio
