#include <algorithm>
#include <ally/midi_take.hpp>
#include <cmath>
#include <stdexcept>
namespace ally::midi {
void Take::begin(uint64_t clock, double startBeat, double framesPerBeat) {
    held_ = {};
    notes_.clear();
    first_ = clock;
    start_ = startBeat;
    fpb_ = framesPerBeat;
    active_ = true;
}
void Take::message(const Message &message) {
    if (!active_ || message.data1 > 127) {
        return;
    }
    const auto type = message.status & 0xf0, channel = message.status & 15;
    if (type != 0x80 && type != 0x90) {
        return;
    }
    auto &held = held_[size_t(channel)][message.data1];
    if (held.velocity && (type == 0x80 || !message.data2 || type == 0x90)) {
        const auto end = std::max(message.sampleTime, held.at);
        if (notes_.size() >= 4096) {
            throw std::runtime_error("MIDI take exceeds 4096 notes");
        }
        notes_.push_back({message.data1, start_ + double(held.at - first_) / fpb_,
                          std::max(.0625, double(end - held.at) / fpb_), float(held.velocity) / 127});
        held = {};
    }
    if (type == 0x90 && message.data2) {
        held = {std::max(first_, message.sampleTime), message.data2};
    }
}
void Take::finish(uint64_t clock) {
    for (size_t ch = 0; ch < 16; ++ch) {
        for (int key = 0; key < 128; ++key) {
            if (held_[ch][size_t(key)].velocity) {
                message({clock, uint8_t(0x80 | ch), uint8_t(key), 0});
            }
        }
    }
    active_ = false;
}
void Take::apply(Project &project, size_t track) const {
    if (active_ || track >= project.tracks.size()) {
        throw std::runtime_error("Invalid MIDI take destination");
    }
    Project candidate = project;
    auto &destination = candidate.tracks[track];
    std::vector<std::pair<int, uint32_t>> bars;
    for (const auto &note : notes_) {
        double cursor = note.beat, end = std::min(Project::maxTimeline, note.beat + note.length);
        while (cursor < end) {
            int bar = int(std::floor(cursor / 4));
            auto it = std::find_if(bars.begin(), bars.end(), [&](auto b) { return b.first == bar; });
            uint32_t id;
            if (it == bars.end()) {
                id = destination.createPattern();
                destination.pattern().name = "Take " + std::to_string(bar + 1);
                bars.push_back({bar, id});
                Clip clip;
                clip.name = destination.pattern().name;
                clip.patternId = id;
                clip.beat = bar * 4;
                destination.clips.push_back(clip);
            } else {
                id = it->second;
            }
            auto &pattern = const_cast<Pattern &>(*destination.findPattern(id));
            double length = std::min(end - cursor, 4 - (cursor - bar * 4));
            if (length >= .0625) {
                pattern.notes.push_back({note.key, cursor - bar * 4, length, note.velocity});
            }
            cursor += length;
            candidate.timelineBeats = std::max(candidate.timelineBeats, double((bar + 1) * 4));
        }
    }
    if (!candidate.valid()) {
        throw std::runtime_error("MIDI take exceeds document limits; existing project preserved");
    }
    project = std::move(candidate);
}
} // namespace ally::midi
