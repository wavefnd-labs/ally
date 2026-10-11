#include <algorithm>
#include <ally/editing.hpp>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>
namespace ally {
void Editing::clear() {
    selectedNotes.clear();
    selectedClips.clear();
}
bool Editing::contains(size_t index) const {
    return notes && std::find(selectedNotes.begin(), selectedNotes.end(), index) != selectedNotes.end();
}
bool Editing::contains(ClipRef ref) const {
    return !notes && std::find(selectedClips.begin(), selectedClips.end(), ref) != selectedClips.end();
}
void Editing::selectNote(size_t index, bool additive) {
    if (!notes || !additive) {
        clear();
    }
    notes = true;
    auto it = std::find(selectedNotes.begin(), selectedNotes.end(), index);
    if (it == selectedNotes.end()) {
        selectedNotes.push_back(index);
    } else if (additive) {
        selectedNotes.erase(it);
    }
}
void Editing::selectClip(ClipRef ref, bool additive) {
    if (notes || !additive) {
        clear();
    }
    notes = false;
    auto it = std::find(selectedClips.begin(), selectedClips.end(), ref);
    if (it == selectedClips.end()) {
        selectedClips.push_back(ref);
    } else if (additive) {
        selectedClips.erase(it);
    }
}
void Editing::copy(const Project &p, size_t track) {
    noteClipboard_.clear();
    clipClipboard_.clear();
    if (notes) {
        const auto &list = p.tracks.at(track).pattern().notes;
        for (auto i : selectedNotes) {
            noteClipboard_.push_back(list.at(i));
        }
    } else {
        for (auto ref : selectedClips) {
            const auto &t = p.tracks.at(ref.track);
            const auto &clip = t.clips.at(ref.clip);
            clipClipboard_.push_back(
                {clip, ref.track, clip.audio ? Pattern{} : *t.findPattern(clip.patternId)});
        }
    }
}
void Editing::paste(Project &project, size_t track, double beat) {
    if (track >= project.tracks.size() || !std::isfinite(beat) || beat < 0) {
        throw std::runtime_error("Invalid paste destination");
    }
    Project candidate = project;
    std::vector<size_t> newNotes;
    std::vector<ClipRef> newClips;
    if (notes && !noteClipboard_.empty()) {
        double first = std::min_element(noteClipboard_.begin(), noteClipboard_.end(), [](auto a, auto b) {
                           return a.beat < b.beat;
                       })->beat;
        auto &list = candidate.tracks[track].pattern().notes;
        for (auto n : noteClipboard_) {
            n.beat += beat - first;
            newNotes.push_back(list.size());
            list.push_back(n);
        }
    } else if (!notes && !clipClipboard_.empty()) {
        double first = Project::maxTimeline;
        size_t firstTrack = audio::Plan::maxTracks;
        for (auto c : clipClipboard_) {
            first = std::min(first, c.clip.beat);
            firstTrack = std::min(firstTrack, c.track);
        }
        std::map<std::tuple<size_t, size_t, uint32_t>, uint32_t> patterns;
        for (auto c : clipClipboard_) {
            size_t target = track + c.track - firstTrack;
            if (target >= candidate.tracks.size()) {
                throw std::runtime_error("Paste needs more destination tracks");
            }
            auto &t = candidate.tracks[target];
            c.clip.beat += beat - first;
            if (!c.clip.audio && target != c.track) {
                const auto key = std::make_tuple(target, c.track, c.pattern.id);
                if (!patterns.contains(key)) {
                    auto id = t.createPattern();
                    auto active = c.pattern;
                    active.id = id;
                    t.pattern() = std::move(active);
                    patterns[key] = id;
                }
                c.clip.patternId = patterns[key];
            }
            newClips.push_back({target, t.clips.size()});
            t.clips.push_back(c.clip);
            candidate.timelineBeats =
                std::max(candidate.timelineBeats, std::ceil((c.clip.beat + c.clip.length) / 4) * 4);
        }
    }
    if (!candidate.valid()) {
        throw std::runtime_error("Paste exceeds pattern, timeline or event limits");
    }
    project = std::move(candidate);
    selectedNotes = std::move(newNotes);
    selectedClips = std::move(newClips);
}
void Editing::erase(Project &project, size_t track) {
    if (notes) {
        auto &list = project.tracks.at(track).pattern().notes;
        auto indexes = selectedNotes;
        std::sort(indexes.rbegin(), indexes.rend());
        for (auto i : indexes) {
            list.erase(list.begin() + std::ptrdiff_t(i));
        }
    } else {
        auto refs = selectedClips;
        std::sort(refs.begin(), refs.end(),
                  [](auto a, auto b) { return a.track != b.track ? a.track > b.track : a.clip > b.clip; });
        for (auto ref : refs) {
            auto &list = project.tracks.at(ref.track).clips;
            list.erase(list.begin() + std::ptrdiff_t(ref.clip));
        }
    }
    clear();
}
void Editing::transform(Project &project, size_t track, double delta, int pitch, bool resize) const {
    double low = -Project::maxTimeline, high = Project::maxTimeline;
    int lowPitch = -127, highPitch = 127;
    if (notes) {
        auto &list = project.tracks.at(track).pattern().notes;
        for (auto i : selectedNotes) {
            const auto &n = list.at(i);
            low = std::max(low, resize ? .0625 - n.length : -n.beat);
            high = std::min(high, 4 - n.beat - n.length);
            lowPitch = std::max(lowPitch, -n.key);
            highPitch = std::min(highPitch, 127 - n.key);
        }
        delta = std::clamp(delta, low, high);
        pitch = std::clamp(pitch, lowPitch, highPitch);
        for (auto i : selectedNotes) {
            auto &n = list[i];
            if (resize) {
                n.length += delta;
            } else {
                n.beat += delta;
                n.key += pitch;
            }
        }
    } else {
        for (auto ref : selectedClips) {
            const auto &c = project.tracks.at(ref.track).clips.at(ref.clip);
            low = std::max(low, resize ? .25 - c.length : -c.beat);
            high = std::min(high, project.timelineBeats - c.beat - c.length);
        }
        delta = std::clamp(delta, low, high);
        for (auto ref : selectedClips) {
            auto &c = project.tracks[ref.track].clips[ref.clip];
            if (resize) {
                c.length += delta;
            } else {
                c.beat += delta;
            }
        }
    }
}
void Editing::duplicate(Project &project, size_t track) {
    copy(project, track);
    double end = 0;
    if (notes) {
        for (auto n : noteClipboard_) {
            end = std::max(end, n.beat + n.length);
        }
    } else {
        for (auto c : clipClipboard_) {
            end = std::max(end, c.clip.beat + c.clip.length);
        }
    }
    if (!noteClipboard_.empty() || !clipClipboard_.empty()) {
        paste(project, track, end);
    }
}
} // namespace ally
