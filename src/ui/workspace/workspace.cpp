#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include <ally/file.hpp>
#include <ally/media/wav.hpp>
#include <ally/workspace.hpp>
#include <chrono>
#include <thread>
#if defined(__linux__)
#include "include/ports/SkFontMgr_fontconfig.h"
#elif defined(__APPLE__)
#include "include/ports/SkFontMgr_mac_ct.h"
#elif defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#endif
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>

namespace ally::ui {
namespace {
// Studio 04: literal palette and dimensions from the user's HTML visual specification.
constexpr SkColor bg = 0xff1e2022, panel = 0xff252729, head = 0xff292b2d, edge = 0xff3d4041;
constexpr SkColor ink = 0xffc9cac9, dim = 0xff858a8b, gold = 0xffd8aa70;
constexpr std::array<SkColor, 8> colors{0xffc9ae81, 0xff94aeb2, 0xffc08d87, 0xff9cb4a1,
                                        0xffa9a0ba, 0xff94aaa0, 0xffb09a88, 0xffa1adb9};
sk_sp<SkTypeface> face(bool bold) {
    static auto mgr = [] {
#if defined(__linux__)
        return SkFontMgr_New_FontConfig(nullptr);
#elif defined(__APPLE__)
        return SkFontMgr_New_CoreText(nullptr);
#else
        return SkFontMgr_New_DirectWrite();
#endif
    }();
    static auto normal = mgr->matchFamilyStyle("Inter", SkFontStyle::Normal());
    static auto medium = mgr->matchFamilyStyle(
        "Inter", SkFontStyle(600, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
    return bold ? medium : normal;
}
void box(SkCanvas &c, Rect r, SkColor color, float radius = 0) {
    SkPaint p;
    p.setColor(color);
    p.setAntiAlias(radius > 0);
    c.drawRoundRect(SkRect::MakeXYWH(r.x, r.y, r.w, r.h), radius, radius, p);
}
void line(SkCanvas &c, float x, float y, float xx, float yy, SkColor color = edge, float width = 1) {
    SkPaint p;
    p.setColor(color);
    p.setStrokeWidth(width);
    c.drawLine(x, y, xx, yy, p);
}
void text(SkCanvas &c, const std::string &s, float x, float y, float size = 11, SkColor color = ink,
          bool bold = false) {
    SkPaint p;
    p.setColor(color);
    p.setAntiAlias(true);
    SkFont f(face(bold), size);
    f.setEdging(SkFont::Edging::kSubpixelAntiAlias);
    c.drawSimpleText(s.data(), s.size(), SkTextEncoding::kUTF8, x, y, f, p);
}
void centered(SkCanvas &c, const std::string &s, Rect r, float size = 11, SkColor color = ink,
              bool bold = false) {
    SkFont f(face(bold), size);
    float w = f.measureText(s.data(), s.size(), SkTextEncoding::kUTF8);
    text(c, s, r.x + (r.w - w) / 2, r.y + r.h / 2 + size * .35f, size, color, bold);
}
void outline(SkCanvas &c, Rect r, SkColor color = edge) {
    SkPaint p;
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(1);
    p.setColor(color);
    c.drawRect(SkRect::MakeXYWH(r.x + .5f, r.y + .5f, r.w - 1, r.h - 1), p);
}
void circle(SkCanvas &c, float x, float y, float radius, SkColor color) {
    SkPaint p;
    p.setColor(color);
    p.setAntiAlias(true);
    c.drawCircle(x, y, radius, p);
}
Rect intersect(Rect a, Rect b) {
    float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::max(0.0f, std::min(a.x + a.w, b.x + b.w) - x),
            std::max(0.0f, std::min(a.y + a.h, b.y + b.h) - y)};
}
SkColor mixed(SkColor a, SkColor b, float f) {
    return SkColorSetARGB(255, uint8_t(SkColorGetR(a) * (1 - f) + SkColorGetR(b) * f),
                          uint8_t(SkColorGetG(a) * (1 - f) + SkColorGetG(b) * f),
                          uint8_t(SkColorGetB(a) * (1 - f) + SkColorGetB(b) * f));
}
void symbol(SkCanvas &c, const std::string &name, Rect r, SkColor color) {
    float x = r.x + r.w / 2, y = r.y + r.h / 2;
    if (name == "plus" || name == "＋") {
        line(c, x - 4, y, x + 4, y, color);
        line(c, x, y - 4, x, y + 4, color);
    } else if (name == "minus" || name == "−") {
        line(c, x - 4, y, x + 4, y, color);
    } else if (name == "down" || name == "⌄" || name == "▾") {
        line(c, x - 3, y - 1, x, y + 2, color);
        line(c, x, y + 2, x + 3, y - 1, color);
    } else if (name == "left" || name == "‹") {
        line(c, x + 1, y - 3, x - 2, y, color);
        line(c, x - 2, y, x + 1, y + 3, color);
    } else if (name == "right" || name == "›") {
        line(c, x - 1, y - 3, x + 2, y, color);
        line(c, x + 2, y, x - 1, y + 3, color);
    } else if (name == "home" || name == "⌂") {
        line(c, x - 5, y, x, y - 5, color);
        line(c, x, y - 5, x + 5, y, color);
        line(c, x - 3, y - 2, x - 3, y + 4, color);
        line(c, x + 3, y - 2, x + 3, y + 4, color);
        line(c, x - 3, y + 4, x + 3, y + 4, color);
    } else if (name == "sample" || name == "◴") {
        SkPaint p;
        p.setColor(color);
        p.setStyle(SkPaint::kStroke_Style);
        p.setAntiAlias(true);
        c.drawCircle(x, y, 4, p);
        line(c, x, y - 4, x, y, color);
        line(c, x, y, x + 4, y, color);
    } else if (name == "plugin" || name == "⬡") {
        SkPaint p;
        p.setColor(color);
        p.setStyle(SkPaint::kStroke_Style);
        p.setAntiAlias(true);
        SkPath path;
        for (int i = 0; i < 6; ++i) {
            float xx = x + 5 * std::cos(i * std::numbers::pi / 3),
                  yy = y + 5 * std::sin(i * std::numbers::pi / 3);
            if (i == 0) {
                path.moveTo(xx, yy);
            } else {
                path.lineTo(xx, yy);
            }
        }
        path.close();
        c.drawPath(path, p);
    } else if (name == "click") {
        circle(c, x - 2, y + 4, 2.5, color);
        line(c, x, y + 3, x, y - 6, color, 1.5);
        line(c, x, y - 6, x + 4, y - 4, color, 1.5);
    } else if (name == "undo" || name == "redo" || name == "loop") {
        SkPaint p;
        p.setColor(color);
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(1.2);
        p.setAntiAlias(true);
        bool reverse = name == "undo";
        c.drawArc(SkRect::MakeXYWH(x - 5, y - 5, 10, 10), reverse ? 210 : -30, reverse ? -260 : 260, false,
                  p);
        SkPath arrow;
        if (reverse) {
            arrow.moveTo(x - 6, y - 5);
            arrow.lineTo(x - 6, y);
            arrow.lineTo(x - 1, y - 3);
        } else {
            arrow.moveTo(x + 6, y - 5);
            arrow.lineTo(x + 6, y);
            arrow.lineTo(x + 1, y - 3);
        }
        arrow.close();
        p.setStyle(SkPaint::kFill_Style);
        c.drawPath(arrow, p);
    } else if (name == "stop") {
        box(c, {x - 4, y - 4, 8, 8}, color);
    } else if (name == "record") {
        circle(c, x, y, 4, color);
    } else if (name == "play") {
        SkPaint p;
        p.setColor(color);
        p.setAntiAlias(true);
        SkPath path;
        path.moveTo(x - 3, y - 6);
        path.lineTo(x + 6, y);
        path.lineTo(x - 3, y + 6);
        path.close();
        c.drawPath(path, p);
    } else if (name == "pause") {
        box(c, {x - 4, y - 5, 2, 10}, color);
        box(c, {x + 2, y - 5, 2, 10}, color);
    } else if (name == "draw") {
        line(c, x - 4, y + 4, x + 4, y - 4, color, 1.5);
        line(c, x - 5, y + 5, x - 2, y + 4, color, 1.5);
    } else if (name == "automation") {
        line(c, x - 6, y + 3, x - 2, y - 4, color);
        line(c, x - 2, y - 4, x + 2, y + 3, color);
        line(c, x + 2, y + 3, x + 6, y - 2, color);
    } else if (name == "browser") {
        for (int k = 0; k < 3; ++k) {
            box(c, {x - 6, y - 4 + k * 4.0f, 2, 1}, color);
            box(c, {x - 2, y - 4 + k * 4.0f, 8, 1}, color);
        }
    } else if (name == "search") {
        SkPaint p;
        p.setStyle(SkPaint::kStroke_Style);
        p.setStrokeWidth(1);
        p.setColor(color);
        p.setAntiAlias(true);
        c.drawCircle(x - 1, y - 1, 3, p);
        line(c, x + 1, y + 1, x + 5, y + 5, color);
    } else {
        const std::string s = name == "undo"    ? "↶"
                              : name == "redo"  ? "↷"
                              : name == "loop"  ? "↻"
                              : name == "click" ? "♩"
                                                : name;
        centered(c, s, r, 15, color);
    }
}
std::string noteName(int key) {
    static const std::array names{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(names[size_t(key % 12)]) + std::to_string(key / 12 - 1);
}
bool black(int key) {
    return key % 12 == 1 || key % 12 == 3 || key % 12 == 6 || key % 12 == 8 || key % 12 == 10;
}
std::string lower(std::string s) {
    for (char &v : s) {
        v = char(std::tolower(static_cast<unsigned char>(v)));
    }
    return s;
}
std::string number(double n) {
    char s[32];
    std::snprintf(s, sizeof(s), "%.0f", n);
    return s;
}
} // namespace
Workspace::Workspace(Project &p, audio::Engine &e, std::span<const float> texture)
    : project_(p), document_(p), engine_(e), texture_(texture) {}
void Workspace::setDocument(const std::filesystem::path &path, const std::filesystem::path &recovery,
                            bool recovered) {
    document_ = DocumentSession(project_, path, recovery);
    if (recovered) {
        document_.recovered();
    }
}
std::string Workspace::title() const {
    return "Ally | " +
           (document_.path().empty() ? std::string("Untitled") : pathText(document_.path().filename())) +
           (modified() ? " *" : "");
}
void Workspace::fileSelected(const std::string &act, const std::filesystem::path &path) {
    dialogPending_ = false;
    if (recordingAudio || recordingMidi) {
        status = "Stop the take before opening or importing files";
        return;
    }
    if (act == "record-audio") {
        if (!path.empty() && startAudioTake) {
            startAudioTake(path);
        }
        return;
    }
    if (path.empty()) {
        status = "Cancelled";
        return;
    }
    if (job_.valid()) {
        return;
    }
    release();
    focus_ = Focus::None;
    patternMenu_ = audioMenu_ = inputMenu_ = midiMenu_ = false;
    const auto snapshot = project_;
    const auto track = size_t(selected_);
    const auto relink = relinkId_;
    job_ = std::async(std::launch::async, [snapshot, path, act, track, relink]() mutable {
        FileResult result{act, path, snapshot};
        if (act == "save" || act == "saveas") {
            result.project.save(path);
        } else if (act == "load") {
            result.project = Project::load(path);
        } else if (act == "import") {
            double beat = 0;
            for (const auto &clip : result.project.tracks[track].clips) {
                beat = std::max(beat, clip.beat + clip.length);
            }
            if (beat >= Project::maxTimeline) {
                throw std::runtime_error("Selected track has no room after its clips; choose an empty track");
            }
            result.project.importWav(path, track, beat);
        } else if (act == "relink") {
            result.project.relinkSource(relink, path);
        } else if (act == "export") {
            renderProject(result.project, path);
        }
        return result;
    });
    status = "Working: " + act + " " + pathText(path.filename());
}
void Workspace::midiMessage(const midi::Message &message) {
    if (recordingMidi) {
        try {
            midiTake_.message(message);
        } catch (const std::exception &e) {
            finishMidi();
            status = e.what();
        }
    }
}
void Workspace::finishMidi() {
    if (!recordingMidi) {
        return;
    }
    recordingMidi = false;
    try {
        midiTake_.finish(engine_.timestamp());
        before_ = project_;
        midiTake_.apply(project_, midiTrack_);
        commitEdit();
        changed();
        status = "MIDI take committed";
    } catch (const std::exception &e) {
        before_.reset();
        status = e.what();
    }
}
void Workspace::recorded(std::vector<RecordedFile> files, double startBeat, uint64_t lostFrames) {
    if (files.empty()) {
        status = "No audio captured";
        return;
    }
    const auto snapshot = project_;
    job_ =
        std::async(std::launch::async, [snapshot, files = std::move(files), startBeat, lostFrames]() mutable {
            FileResult result{"recorded", {}, snapshot};
            result.lostFrames = lostFrames;
            for (const auto &file : files) {
                if (file.frames) {
                    const double trimSeconds = std::max(0.0, -startBeat * 60 / snapshot.bpm);
                    if (trimSeconds * 48000 >= file.frames) {
                        continue;
                    }
                    result.project.importWav(file.path, file.track, std::max(0.0, startBeat));
                    auto &clip = result.project.tracks[file.track].clips.back();
                    clip.sourceOffset = trimSeconds;
                    clip.length = std::max(.25, clip.length - trimSeconds * snapshot.bpm / 60);
                }
            }
            return result;
        });
    status = "Preparing recorded clips";
}
void Workspace::tick(double seconds) {
    if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto result = job_.get();
            if (result.action == "save" || result.action == "saveas") {
                document_.saved(result.project, result.path);
            } else if (result.action != "export") {
                // Prepare ownership before swapping the document; failure keeps the previous project.
                result.project.prepare(engine_);
                remember(project_);
                project_ = std::move(result.project);
                if (result.action == "load") {
                    engine_.play(false);
                    engine_.rewind();
                    document_.opened(project_, result.path);
                    undo_.clear();
                    redo_.clear();
                    editing_ = {};
                    selected_ = selectedClip_ = 0;
                } else if (result.action == "import") {
                    selectedClip_ = int(project_.tracks[size_t(selected_)].clips.size()) - 1;
                }
                selectedNote_ = -1;
                changed();
            }
            status = "Completed: " + result.action + " " + pathText(result.path.filename());
            if (result.lostFrames) {
                status +=
                    " / Lost " + std::to_string(result.lostFrames) + " capture frames; silence inserted";
            }
            auto warnings = project_.sourceWarnings();
            if (!warnings.empty()) {
                status = "Missing/changed source: " + warnings;
            }
        } catch (const std::exception &error) {
            status = error.what();
        }
    }
    if (!busy() && !recordingAudio && !recordingMidi) {
        try {
            if (document_.autosave(project_, seconds)) {
                status = "Recovery snapshot saved";
            }
        } catch (const std::exception &error) {
            status = "Autosave failed: " + std::string(error.what());
        }
    }
}
void Workspace::remember(const Project &p) {
    if (undo_.size() == 64) {
        undo_.erase(undo_.begin());
    }
    undo_.push_back(p);
    redo_.clear();
}
void Workspace::commitEdit() {
    if (before_) {
        if (!project_.valid()) {
            project_ = *before_;
            status = "Edit exceeds document limits";
            changed();
        } else if (project_ != *before_) {
            remember(*before_);
        }
        before_.reset();
    }
}
void Workspace::changed() {
    dirty_ = true;
    sync();
}
bool Workspace::sync() {
    engine_.looping(project_.loop && !recordingMidi && !recordingAudio);
    engine_.metronome(project_.metronome);
    if (releaseKey_ >= 0 && engine_.audition({uint8_t(releaseKey_), uint8_t(releaseTrack_), 0})) {
        releaseKey_ = -1;
    }
    if (dirty_ && project_.valid()) {
        try {
            auto plan = project_.prepare(engine_);
            if (recordingAudio) {
                // Disk recording is not limited by the current arrangement's visible end.
                plan.loopFrames =
                    std::max(plan.loopFrames, engine_.position() + uint64_t(engine_.sampleRate()) * 21600);
                plan.loopEndFrame = 0;
            }
            if (engine_.submit(plan)) {
                dirty_ = false;
            }
        } catch (const std::exception &error) {
            status = error.what();
        }
    }
    return !dirty_;
}
void Workspace::tool(SkCanvas &c, Rect r, const std::string &label, const std::string &act, bool active,
                     bool enabled, const std::string &tip) {
    if (active) {
        box(c, r, 0xffd7ac74, 2);
    } else if (r.contains(mouseX_, mouseY_) && enabled) {
        box(c, r, 0xff3c3f41, 2);
    }
    SkColor color =
        active ? 0xff1e2021 : (act == "record" || act.starts_with("arm") ? 0xffd97f7b : 0xffbfc1c0);
    if (!enabled) {
        color = mixed(color, head, .35f);
    }
    if (label == "＋" || label == "−" || label == "⌄" || label == "‹") {
        symbol(c, label, r, color);
    } else if (label.starts_with('@')) {
        symbol(c, label.substr(1), r, color);
    } else {
        centered(
            c, label, r,
            (act.starts_with("mute") || act.starts_with("solo") || act == "mastermute" || act == "mastersolo")
                ? 9
                : 11,
            color);
    }
    Rect hit = intersect(r, hitClip_);
    if (hit.w > 0 && hit.h > 0) {
        buttons_.push_back({hit, act, tip.empty() ? act : tip, enabled});
    }
}
void Workspace::draw(SkCanvas &c, float w, float h) {
    width_ = w;
    height_ = h;
    buttons_.clear();
    clips_.clear();
    faders_.fill({});
    grid_ = {};
    velocity_ = {};
    hitClip_ = {0, 0, w, h};
    c.clear(bg);
    box(c, {0, 0, w, 58}, 0xff2b2d2f);
    line(c, 0, 57, w, 57, 0xff444749);
    centered(c, "a.", {0, 0, 58, 58}, 22, 0xfff0f0ec, true);
    line(c, 57, 0, 57, 58, 0xff414447);
    tool(c, {71, 14.5, 29, 29}, "@stop", "stop", false, true, "Stop");
    tool(c, {102, 14.5, 29, 29}, engine_.playing() ? "@pause" : "@play", "play", true, true, "Play / Pause");
    tool(c, {133, 14.5, 29, 29}, "@record", "record", recordingAudio || recordingMidi, !busy(),
         "Record armed audio tracks; Shift+R records MIDI");
    const auto position = engine_.position();
    const auto latency = engine_.latencyFrames();
    double beat = double(engine_.playing() ? (position > latency ? position - latency : 0) : position) /
                  (engine_.sampleRate() * 60.0 / project_.bpm);
    char clock[32];
    std::snprintf(clock, sizeof(clock), "%02d.%d.%d", int(beat / 4) + 1, int(std::fmod(beat, 4)) + 1,
                  int(std::fmod(beat, 1) * 4) + 1);
    box(c, {175, 12.5, 116, 33}, 0xff1b1d1e, 2);
    outline(c, {175, 12.5, 116, 33}, 0xff414445);
    centered(c, clock, {175, 12.5, 116, 33}, 20, 0xffebeae5);
    float gap = w <= 1100 ? 7 : 11, x = 291 + gap;
    line(c, x, 15.5, x, 42.5, 0xff414447);
    x += gap + 1;
    tempo_ = {x, 14.5, 49, 29};
    centered(c, focus_ == Focus::Tempo ? tempoText_ : number(project_.bpm), tempo_, 14, 0xffe1e1dc);
    buttons_.push_back({tempo_, "tempo", "Tempo: type a value, Enter to apply", true});
    text(c, "BPM", x + 55, 33, 10, 0xff909493);
    x += 80 + gap;
    line(c, x, 15.5, x, 42.5, 0xff414447);
    x += gap + 1;
    tool(c, {x, 14.5, 35, 29}, "4/4", "signature", false, false,
         "Time signature changes are not implemented");
    x += 35 + gap;
    line(c, x, 15.5, x, 42.5, 0xff414447);
    x += gap + 1;
    tool(c, {x, 14.5, 29, 29}, "@click", "metronome", project_.metronome, true, "Metronome");
    x += 31;
    tool(c, {x, 14.5, 29, 29}, "@loop", "loop", project_.loop, true, "Loop configured range");
    x += 29 + gap;
    line(c, x, 15.5, x, 42.5, 0xff414447);
    x += gap + 1;
    tool(c, {x, 14.5, 29, 29}, "@undo", "undo", false, !undo_.empty(), "Undo");
    tool(c, {x + 31, 14.5, 29, 29}, "@redo", "redo", false, !redo_.empty(), "Redo");
    if (w > 1100) {
        circle(c, w - 229, 29, 2.5f,
               backendStatus.starts_with("Offline") || backendStatus == "Audio unavailable" ? dim
                                                                                            : 0xff7aaf8e);
        text(c, "48 kHz · Stereo", w - 220, 33, 10, 0xffa7adab);
    }
    if (w > 1100) {
        buttons_.push_back(
            {{w - 235, 12, 115, 34}, "audio-settings", "Audio device, buffer and diagnostics", true});
    }
    tool(c, {w - 115, 14.5, 53, 29}, "Export", "export", false, true, "Export WAV");
    tool(c, {w - 57, 14.5, 44, 29}, "Save", "save", false, true, "Save project (Ctrl+Shift+S: Save As)");
    float browserW = browser_ ? (w <= 1100 ? 175.0f : 205.0f) : 0;
    float bottom = h - 23;
    if (browser_) {
        browser(c, {0, 58, browserW, bottom - 58});
    }
    float left = browserW, width = w - left;
    box(c, {left, 58, width, 33}, 0xff242628);
    line(c, left, 90, w, 90, 0xff404344);
    tool(c, {left + 7, 60, 29, 29}, "@browser", "browser", false, true, "Toggle browser");
    box(c, {left + 39, 62, 120, 25}, 0xff343638, 2);
    centered(c, "ARRANGEMENT", {left + 39, 62, 120, 25}, 10, 0xffe9e8e0, true);
    tool(c, {left + 162, 62, 84, 25}, "SESSION", "session", false, false, "Session view is not implemented");
    tool(c, {left + 251, 62, 62, 25}, "+8 bars", "extend-timeline", false,
         project_.timelineBeats < Project::maxTimeline);
    tool(c, {w - 184, 62, 58, 25}, snap_ == .25 ? "1/16 ⌄" : "1/8 ⌄", "snap", false, true,
         "Grid quantization");
    tool(c, {w - 122, 60, 29, 29}, "@draw", "draw", false, true, erase_ ? "Erase tool" : "Draw tool");
    tool(c, {w - 92, 60, 29, 29}, "@automation", "automation", false, false, "Automation is not implemented");
    tool(c, {w - 62, 60, 29, 29}, "−", "zoom-", false, true, "Zoom out");
    tool(c, {w - 32, 60, 29, 29}, "＋", "zoom+", false, true, "Zoom in");
    float detailH = std::clamp(folded_ ? 155.0f : detailHeight_, 155.0f, std::max(155.0f, h - 269));
    float splitY = bottom - detailH - 5;
    arrangement(c, {left, 91, width, splitY - 91}, beat);
    divider_ = {left, splitY, width, 5};
    box(c, divider_, 0xff181a1b);
    line(c, left, splitY, w, splitY, 0xff484b4b);
    line(c, left, splitY + 4, w, splitY + 4, 0xff474a4a);
    float dy = splitY + 5;
    box(c, {left, dy, width, 34}, head);
    line(c, left, dy + 33, w, dy + 33, 0xff404445);
    for (int i = 0; i < 3; ++i) {
        float xx = left + 7 + (i == 0 ? 0 : i == 1 ? 97 : 177), ww = i == 0 ? 95 : 78;
        bool active = detail_ == Detail(i);
        centered(c,
                 i == 0   ? "PIANO ROLL"
                 : i == 1 ? "DEVICES"
                          : "MIXER",
                 {xx, dy + 3, ww, 27}, 10, active ? 0xffe3e4de : 0xff89908e, true);
        if (active) {
            box(c, {xx, dy + 28, ww, 2}, gold);
        }
        buttons_.push_back({{xx, dy + 3, ww, 27},
                            i == 0   ? "piano"
                            : i == 1 ? "devices"
                                     : "mixer",
                            "Switch detail editor",
                            true});
    }
    const auto &track = project_.tracks[size_t(selected_)];
    std::string selection = focus_ == Focus::PatternName ? editText_ : track.pattern().name;
    const float selectorW = std::min(150.0f, std::max(85.0f, width - 322));
    tool(c, {w - 52 - selectorW, dy + 5, selectorW, 24}, selection, "patterns", patternMenu_, true,
         "Select/create/duplicate/rename pattern; assign to selected clip");
    tool(c, {w - 45, dy + 5, 23, 24}, "Q", "quantize", false, detail_ == Detail::Piano, "Quantize notes");
    tool(c, {w - 23, dy + 5, 21, 24}, "⌄", "fold", false, true, "Collapse / expand editor");
    Rect detail{left, dy + 34, width, detailH - 34};
    c.save();
    c.clipRect(SkRect::MakeXYWH(detail.x, detail.y, detail.w, detail.h));
    hitClip_ = detail;
    if (detail_ == Detail::Piano) {
        const auto &selectedClips = project_.tracks[size_t(selected_)].clips;
        if (selectedClip_ >= 0 && selectedClip_ < int(selectedClips.size()) &&
            selectedClips[size_t(selectedClip_)].audio) {
            audioClip(c, detail);
        } else {
            piano(c, detail, std::fmod(beat, 4));
        }
    } else if (detail_ == Detail::Devices) {
        devices(c, detail);
    } else {
        mixer(c, detail);
    }
    c.restore();
    hitClip_ = {0, 0, w, h};
    box(c, {0, bottom, w, 23}, 0xff202224);
    line(c, 0, bottom, w, bottom, 0xff3b3f40);
    text(c, "●", 11, h - 7, 10, 0xffc5c7c2);
    text(c, status.substr(0, 95), 25, h - 7, 10, 0xff8f9794);
    text(c, backendStatus, w - std::min(300.0f, float(backendStatus.size() * 5.2)) - 11, h - 7, 10,
         0xff8f9794);
    buttons_.push_back(
        {{w - 310, bottom, 310, 23}, "audio-settings", "Audio settings / xrun diagnostics", true});
    if (marquee_) {
        box(c, selectionBox_, 0x22d8aa70);
        outline(c, selectionBox_, gold);
    }
    menu(c);
    if (busy()) {
        box(c, {w / 2 - 170, h / 2 - 22, 340, 44}, head);
        outline(c, {w / 2 - 170, h / 2 - 22, 340, 44});
        centered(c, "File operation in progress…", {w / 2 - 170, h / 2 - 22, 340, 44}, 12);
    }
    for (const auto &b : buttons_) {
        if (b.rect.contains(mouseX_, mouseY_) && !b.tooltip.empty()) {
            float tw = std::min(w - 10, float(b.tooltip.size() * 6 + 14));
            float tx = std::clamp(mouseX_ + 12, 5.0f, w - tw - 5), ty = std::min(h - 49, mouseY_ + 22);
            box(c, {tx, ty, tw, 23}, 0xff191c1d, 2);
            outline(c, {tx, ty, tw, 23}, 0xff585e5f);
            text(c, b.tooltip, tx + 7, ty + 15, 10);
            break;
        }
    }
}

void Workspace::browser(SkCanvas &c, Rect r) {
    box(c, r, 0xff232527);
    line(c, r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h, 0xff434647);
    box(c, {r.x, r.y, r.w, 33}, head);
    line(c, r.x, r.y + 32, r.x + r.w, r.y + 32, 0xff404344);
    symbol(c, "browser", {8, r.y + 7, 18, 18}, ink);
    text(c, "BROWSER", 34, r.y + 21, 10, 0xffc3c7c6, true);
    tool(c, {r.w - 26, r.y + 3, 22, 27}, "‹", "browser", false, true, "Hide browser");
    searchBox_ = {10, r.y + 42, r.w - 20, 28};
    box(c, searchBox_, 0xff1c1e20);
    outline(c, searchBox_, 0xff393d3f);
    symbol(c, "search", {searchBox_.x + 5, searchBox_.y + 5, 17, 17}, dim);
    text(c, search_.empty() ? "Search" : search_, searchBox_.x + 28, searchBox_.y + 18, 11,
         search_.empty() ? 0xff8e9595 : ink);
    if (focus_ == Focus::Search) {
        outline(c, searchBox_, gold);
    }
    buttons_.push_back({searchBox_, "search", "Search browser", true});
    float y = r.y + 77;
    auto entry = [&](const std::string &label, const std::string &act, bool indent, bool active, bool enabled,
                     const std::string &iconName) {
        if (!search_.empty() && lower(label).find(lower(search_)) == std::string::npos) {
            return;
        }
        Rect rr{0, y, r.w - 1, 28};
        if (active) {
            box(c, rr, 0xff36383a);
        }
        if (!indent) {
            symbol(c, label == "Instruments" ? "down" : "right", {9, y + 8, 12, 12}, 0xff7e8483);
        }
        symbol(c, iconName, {indent ? 26.0f : 29.0f, y + 7, 16, 16}, 0xffa0aaa8);
        text(c, label, indent ? 49.0f : 51.0f, y + 18, 11, active ? 0xffe9e7df : 0xffa9afae);
        buttons_.push_back({rr, act, enabled ? label : label + ": not implemented", enabled});
        y += 28;
    };
    entry("Instruments", "instruments", false, false, true, "browser");
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        if (i <= 2) {
            entry(project_.tracks[i].name, "track" + std::to_string(i), true, int(i) == selected_, true,
                  i == 2 ? "browser" : "record");
        }
    }
    entry("Audio Effects", "effects", false, false, false, "record");
    entry("Samples", "import", false, false, true, "◴");
    entry("Plugins", "plugins", false, false, false, "⬡");
    entry("Projects", "load", false, false, true, "browser");
    float foot = r.y + r.h - 31;
    line(c, 0, foot, r.w, foot, 0xff393d3e);
    symbol(c, "home", {8, foot + 8, 16, 16}, 0xffb1b7b3);
    text(c, "Places", 34, foot + 20, 10, 0xff8c9390);
    tool(c, {r.w - 27, foot + 2, 23, 27}, "＋", "places", false, false,
         "Custom browser places are not implemented");
}

void Workspace::arrangement(SkCanvas &c, Rect r, double beat) {
    float labelW = width_ <= 1100 ? 148 : 174;
    box(c, r, 0xff212426);
    box(c, {r.x, r.y, labelW, 45}, head);
    line(c, r.x + labelW - 1, r.y, r.x + labelW - 1, r.y + 45, 0xff484b4d);
    symbol(c, "down", {r.x + 8, r.y + 17, 12, 12}, 0xffa7acab);
    text(c, "TRACKS", r.x + 30, r.y + 27, 10, 0xffa7acab);
    tool(c, {r.x + labelW - 39, r.y + 8, 29, 29}, "＋", "addtrack", false,
         project_.tracks.size() < audio::Plan::maxTracks, "Add instrument track (up to 32)");
    float lx = r.x + labelW, lw = r.w - labelW;
    const double visible = visibleBeats();
    arrangementScroll_ = std::clamp(arrangementScroll_, 0.0, std::max(0.0, project_.timelineBeats - visible));
    auto at = [&](double beat) { return lx + float((beat - arrangementScroll_) / visible) * lw; };
    const int first = int(std::floor(arrangementScroll_)),
              last = int(std::ceil(arrangementScroll_ + visible));
    box(c, {lx, r.y, lw, 45}, head);
    for (int tick = first; tick <= last; ++tick) {
        float xx = at(tick);
        if (tick % 4 == 0) {
            line(c, xx, r.y, xx, r.y + 23, 0xff46494a);
            text(c, std::to_string(tick / 4 + 1), xx + 8, r.y + 16, 10, 0xffaeb3b0);
        }
        line(c, xx, r.y + 23, xx, r.y + 45, 0xff393d3d);
    }
    line(c, lx, r.y + 23, lx + lw, r.y + 23, 0xff383b3d);
    if (project_.loop) {
        float start = std::clamp(at(0), lx, lx + lw), end = std::clamp(at(project_.loopBeats), lx, lx + lw);
        box(c, {start, r.y + 28, std::max(0.0f, end - start), 13}, 0x25bea47a);
        line(c, start + 1, r.y + 28, start + 1, r.y + 41, 0xffcbaa77, 2);
        line(c, end - 1, r.y + 28, end - 1, r.y + 41, 0xffcbaa77, 2);
    }
    buttons_.push_back({{lx, r.y, lw, 45}, "seek", "Click ruler to seek", true});
    line(c, r.x, r.y + 44, r.x + r.w, r.y + 44, 0xff444748);
    Rect viewport{r.x, r.y + 45, r.w, std::max(0.0f, r.h - 45)};
    lanes_ = {lx, viewport.y, lw, viewport.h};
    trackScroll_ =
        std::clamp(trackScroll_, 0.0f, std::max(0.0f, float(project_.tracks.size() * 79) - viewport.h));
    c.save();
    c.clipRect(SkRect::MakeXYWH(viewport.x, viewport.y, viewport.w, viewport.h));
    hitClip_ = viewport;
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        float y = viewport.y + float(i) * 79 - trackScroll_;
        const auto &t = project_.tracks[i];
        box(c, {r.x, y, labelW, 79}, int(i) == selected_ ? 0xff333638 : 0xff282a2c);
        box(c, {r.x, y, 3, 79}, colors[i % colors.size()]);
        line(c, r.x + labelW - 1, y, r.x + labelW - 1, y + 79, 0xff454849);
        char id[8];
        std::snprintf(id, sizeof(id), "%02d", int(i) + 1);
        text(c, id, r.x + 13, y + 24, 10, 0xff747b7a);
        text(c, t.name, r.x + 39, y + 24, 11, 0xffe3e3dd, true);
        Rect nameHit = intersect({r.x, y, labelW, 36}, viewport);
        if (nameHit.h > 0) {
            buttons_.push_back({nameHit, "track" + std::to_string(i), "Select channel", true});
        }
        for (int k = 0; k < 3; ++k) {
            Rect b{r.x + 13 + k * 22.0f, y + 42, 19, 17};
            box(c, b, (k == 0 ? t.muted : k == 1 ? t.solo : t.armed) ? 0xff8b7760 : 0xff282a2c, 2);
            outline(c, b, 0xff424647);
            tool(c, b,
                 k == 0   ? "M"
                 : k == 1 ? "S"
                          : "@record",
                 (k == 0   ? "mute"
                  : k == 1 ? "solo"
                           : "arm") +
                     std::to_string(i),
                 false, true,
                 k == 0   ? "Mute"
                 : k == 1 ? "Solo"
                          : "Arm audio recording");
        }
        c.save();
        c.clipRect(SkRect::MakeXYWH(lx, viewport.y, lw, viewport.h));
        box(c, {lx, y, lw, 79}, i % 2 ? 0xff202324 : 0xff212425);
        for (int k = first; k <= last; ++k) {
            line(c, at(k), y, at(k), y + 79, k % 4 == 0 ? 0xff454848 : 0xff313638);
        }
        for (size_t n = 0; n < t.clips.size(); ++n) {
            const auto &clip = t.clips[n];
            Rect cr{at(clip.beat), y + 8, float(clip.length / visible) * lw, 63};
            if (cr.x + cr.w <= lx || cr.x >= lx + lw) {
                continue;
            }
            box(c, cr, mixed(colors[i % colors.size()], 0xff333739, .27f));
            outline(c, cr, mixed(colors[i % colors.size()], 0xff131616, .21f));
            if (editing_.contains(ClipRef{i, n}) || (int(i) == selected_ && int(n) == selectedClip_)) {
                Rect inner{cr.x + 1, cr.y + 1, cr.w - 2, cr.h - 2};
                outline(c, inner, 0xfff2eadb);
            }
            c.save();
            c.clipRect(SkRect::MakeXYWH(cr.x, cr.y, cr.w, cr.h));
            text(c, clip.name, cr.x + 7, cr.y + 14, 10, 0xff171a1b, true);
            if (clip.audio) {
                const auto *source = project_.source(clip.sourceId);
                if (source && (source->pcm || source->stream)) {
                    const double duration = double(source->originalFrames) / source->originalRate;
                    for (int px = std::max(0, int(lx - cr.x - 5));
                         px < std::min(int(cr.w) - 10, int(lx + lw - cr.x - 5)); px += 3) {
                        const double seconds = clip.sourceOffset + double(px) / std::max(1.0f, cr.w - 10) *
                                                                       clip.length * 60 / project_.bpm;
                        if (!clip.sourceLoop && seconds >= duration) {
                            continue;
                        }
                        const auto bin =
                            std::min<size_t>(1023, size_t(std::fmod(seconds, duration) / duration * 1024));
                        const float hh = std::clamp(source->peaks[bin] * 2, .03f, 1.0f) * (cr.h - 37);
                        line(c, cr.x + 5 + px, cr.y + 24 + (cr.h - 37 - hh) / 2, cr.x + 5 + px,
                             cr.y + 24 + (cr.h - 37 + hh) / 2, 0xa0283334);
                    }
                } else {
                    text(c, "Source unavailable - relink", cr.x + 7, cr.y + 40, 10, 0xff57372d);
                }
            } else if (const auto *pattern = t.findPattern(clip.patternId)) {
                for (const auto &note : pattern->notes) {
                    float nx = cr.x + 5 + float(note.beat / 4) * (cr.w - 10),
                          ny = cr.y + 29 + float((72 - note.key + 24) % 12) / 12 * 27;
                    box(c, {nx, ny, std::max(2.0f, float(note.length / 4) * (cr.w - 10)), 3}, 0x88273132);
                }
            }
            c.restore();
            Rect hit = intersect(cr, lanes_);
            if (hit.w > 0 && hit.h > 0) {
                clips_.push_back({hit, i, n});
            }
        }
        c.restore();
        line(c, r.x, y + 78, r.x + r.w, y + 78, 0xff343839);
    }
    float emptyY = viewport.y + float(project_.tracks.size()) * 79 - trackScroll_;
    for (int bar = first; bar <= last; ++bar) {
        if (bar % 4 == 0) {
            line(c, at(bar), std::max(viewport.y, emptyY), at(bar), viewport.y + viewport.h, 0xff292d2e);
        }
    }
    c.restore();
    hitClip_ = {0, 0, width_, height_};
    float ph = at(beat);
    if (ph < lx || ph > lx + lw) {
        return;
    }
    line(c, ph, viewport.y, ph, viewport.y + viewport.h, 0xffe2b679);
    SkPaint p;
    p.setColor(0xffe2b679);
    SkPath marker;
    marker.moveTo(ph - 4, viewport.y);
    marker.lineTo(ph + 4, viewport.y);
    marker.lineTo(ph, viewport.y + 6);
    marker.close();
    c.drawPath(marker, p);
}

void Workspace::menu(SkCanvas &c) {
    if (!patternMenu_ && !audioMenu_ && !inputMenu_ && !midiMenu_) {
        menuRect_ = {};
        return;
    }
    const float width = std::min(410.0f, width_ - 20);
    const float height = audioMenu_ || inputMenu_ ? 430 : 370;
    Rect r{width_ - width - 10,
           std::clamp(audioMenu_ || inputMenu_ ? 60.0f : divider_.y - height, 62.0f, height_ - height - 30),
           width, height};
    menuRect_ = r;
    box(c, r, head);
    outline(c, r, 0xff656661);
    hitClip_ = {0, 0, width_, height_};
    if (patternMenu_) {
        const auto &track = project_.tracks[size_t(selected_)];
        text(c, track.name + " / Patterns", r.x + 12, r.y + 20, 11, ink, true);
        const auto begin = size_t(std::max(0, patternPage_)) * 8;
        for (size_t i = begin; i < std::min(begin + 8, track.patterns.size()); ++i) {
            tool(c, {r.x + 8, r.y + 30 + float(i - begin) * 25, width - 16, 24}, track.patterns[i].name,
                 "select-pattern:" + std::to_string(track.patterns[i].id),
                 track.activePattern == track.patterns[i].id, true, "Edit this independent pattern");
        }
        tool(c, {r.x + 8, r.y + 230, width - 16, 24}, "Next page", "pattern-page", false,
             track.patterns.size() > 8);
        tool(c, {r.x + 8, r.y + 256, (width - 24) / 2, 24}, "New", "new-pattern", false,
             track.patterns.size() < 32);
        tool(c, {r.x + 16 + (width - 24) / 2, r.y + 256, (width - 24) / 2, 24}, "Duplicate",
             "duplicate-pattern", false, track.patterns.size() < 32);
        tool(c, {r.x + 8, r.y + 283, width - 16, 24}, "Rename", "rename-pattern");
        const bool midiClip = selectedClip_ >= 0 && selectedClip_ < int(track.clips.size()) &&
                              !track.clips[size_t(selectedClip_)].audio;
        tool(c, {r.x + 8, r.y + 310, width - 16, 24}, "Assign pattern to selected clip", "assign-pattern",
             false, midiClip);
        tool(c, {r.x + width - 75, r.y + 340, 66, 23}, "Close", "patterns");
    } else if (inputMenu_ || midiMenu_) {
        text(c, inputMenu_ ? "AUDIO INPUT" : "MIDI INPUT", r.x + 12, r.y + 20, 11, ink, true);
        if (inputMenu_) {
            const size_t begin = size_t(inputPage_) * 8;
            for (size_t i = begin; i < std::min(begin + 8, inputDevices.size()); ++i) {
                tool(c, {r.x + 8, r.y + 30 + float(i - begin) * 25, width - 16, 24},
                     inputDevices[i].name.substr(0, 57), "input:" + inputDevices[i].id,
                     inputConfig.deviceId == inputDevices[i].id);
            }
            tool(c, {r.x + 8, r.y + 261, width - 16, 24},
                 project_.automaticRecordLatency ? "Record compensation: Estimated"
                                                 : "Record compensation: Manual only",
                 "record-latency-auto", project_.automaticRecordLatency, !recordingAudio);
            text(c, "Additional offset: " + number(double(project_.recordOffsetFrames) / 48) + " ms",
                 r.x + 12, r.y + 305, 10);
            tool(c, {r.x + width - 133, r.y + 288, 38, 24}, "−", "record-offset:-48", false, !recordingAudio,
                 "Move take later by 1 ms");
            tool(c, {r.x + width - 91, r.y + 288, 38, 24}, "＋", "record-offset:48", false, !recordingAudio,
                 "Move take earlier by 1 ms");
            tool(c, {r.x + width - 49, r.y + 288, 38, 24}, "0", "record-offset-reset", false,
                 !recordingAudio);
            text(c,
                 "Input estimate " + number(inputStats.inputLatencyMs) + " ms / PDC " +
                     std::to_string(engine_.latencyFrames()) + " samples",
                 r.x + 12, r.y + 332, 10, dim);
            text(c,
                 "Queue peak " + std::to_string(recordingQueueHighWater) + " / lost " +
                     std::to_string(recordingLostFrames) + " frames",
                 r.x + 12, r.y + 354, 10, dim);
            text(c, "Hardware loopback calibration is not automatic", r.x + 12, r.y + 375, 10, dim);
        } else {
            const size_t begin = size_t(midiPage_) * 8;
            for (size_t i = begin; i < std::min(begin + 8, midiPorts.size()); ++i) {
                tool(c, {r.x + 8, r.y + 30 + float(i - begin) * 25, width - 16, 24},
                     midiPorts[i].name.substr(0, 57), "midi-port:" + std::to_string(midiPorts[i].index));
            }
            text(c, midiStatus.substr(0, 65), r.x + 12, r.y + 260, 10);
            text(c, "Dropped MIDI events: " + std::to_string(midiDropped), r.x + 12, r.y + 279, 10, dim);
        }
        tool(c, {r.x + 8, r.y + 230, width - 16, 24}, "Next devices", inputMenu_ ? "input-page" : "midi-page",
             false, (inputMenu_ ? inputDevices.size() : midiPorts.size()) > 8);
        tool(c, {r.x + 8, r.y + (inputMenu_ ? 392 : 290), width - 16, 24}, "Back to audio output",
             "audio-settings");
    } else {
        text(c, "AUDIO OUTPUT", r.x + 12, r.y + 20, 11, ink, true);
        const auto begin = size_t(std::max(0, devicePage_)) * 8;
        for (size_t i = begin; i < std::min(begin + 8, audioDevices.size()); ++i) {
            tool(c, {r.x + 8, r.y + 30 + float(i - begin) * 25, width - 16, 24},
                 audioDevices[i].name.substr(0, 57), "device:" + audioDevices[i].id,
                 audioConfig.deviceId == audioDevices[i].id, true, audioDevices[i].id);
        }
        tool(c, {r.x + 8, r.y + 230, width - 16, 24}, "Next devices", "device-page", false,
             audioDevices.size() > 8);
        text(c, "Requested buffer (device may round)", r.x + 12, r.y + 275, 10, dim);
        for (int i = 0; i < 4; ++i) {
            auto frames = 128u << i;
            tool(c, {r.x + 8 + float(i) * (width - 16) / 4, r.y + 285, (width - 20) / 4, 24},
                 std::to_string(frames), "buffer:" + std::to_string(frames),
                 audioConfig.bufferFrames == frames);
        }
        text(c,
             "Device buffer: " + std::to_string(audioStats.bufferFrames) + " frames / latency " +
                 number(audioStats.outputLatencyMs) + " ms + " + std::to_string(engine_.latencyFrames()) +
                 " PDC",
             r.x + 12, r.y + 331, 10);
        text(c,
             "XRUNs: " + std::to_string(audioStats.underruns) + " / process max " +
                 number(double(audioStats.maxProcessNs) / 1e6) + " ms",
             r.x + 12, r.y + 352, 10);
        text(c,
             "Stream misses: " + std::to_string(engine_.streamUnderruns()) +
                 " / Frames: " + std::to_string(audioStats.processedFrames) +
                 " / native error: " + std::to_string(audioStats.errorCode),
             r.x + 12, r.y + 373, 10, dim);
        tool(c, {r.x + 8, r.y + 393, 75, 24}, "Input", "input-settings");
        tool(c, {r.x + 90, r.y + 393, 75, 24}, "MIDI", "midi-settings");
        tool(c, {r.x + width - 75, r.y + 393, 66, 24}, "Close", "audio-settings");
    }
}
void Workspace::audioClip(SkCanvas &c, Rect r) {
    const auto &clip = project_.tracks[size_t(selected_)].clips[size_t(selectedClip_)];
    const auto *source = project_.source(clip.sourceId);
    box(c, r, panel);
    if (!source) {
        return;
    }
    text(c, source->name + " / " + std::to_string(source->originalRate) + " Hz", r.x + 12, r.y + 20, 11, ink,
         true);
    tool(c, {r.x + 12, r.y + 29, 160, 25},
         "Start: " +
             (focus_ == Focus::SourceOffset ? editText_ : std::to_string(clip.sourceOffset).substr(0, 8)) +
             " s",
         "source-offset", false, true, "Trim by source offset; arrangement right edge sets end");
    tool(c, {r.x + 180, r.y + 29, 65, 25}, "Loop", "source-loop", clip.sourceLoop);
    tool(c, {r.x + 253, r.y + 29, 76, 25}, "Relink", "relink", false, !source->path.empty());
    Rect wave{r.x + 12, r.y + 65, r.w - 24, std::max(15.0f, r.h - 88)};
    box(c, wave, bg);
    if (!source->error.empty()) {
        text(c, source->error.substr(0, 100), wave.x + 10, wave.y + 23, 11, 0xffd97f7b);
    } else {
        for (int x = 0; x < int(wave.w); ++x) {
            auto peak = source->peaks[std::min<size_t>(1023, size_t(double(x) / wave.w * 1024))];
            const float height = std::min(1.0f, peak * 2) * wave.h * .85f;
            line(c, wave.x + x, wave.y + (wave.h - height) / 2, wave.x + x, wave.y + (wave.h + height) / 2,
                 colors[size_t(selected_) % colors.size()]);
        }
        const double duration = double(source->originalFrames) / source->originalRate;
        const float at = wave.x + float(clip.sourceOffset / duration) * wave.w;
        line(c, at, wave.y, at, wave.y + wave.h, gold, 2);
    }
}

void Workspace::piano(SkCanvas &c, Rect r, double beat) {
    float keyW = 56, velH = 57;
    grid_ = {r.x + keyW, r.y, r.w - keyW, r.h - velH};
    velocity_ = {grid_.x, r.y + r.h - velH, grid_.w, velH};
    float row = grid_.h / 13, bw = grid_.w / 4 * float(zoom_);
    box(c, grid_, 0xff242729);
    box(c, {r.x, r.y, keyW, grid_.h}, 0xffd4d5d1);
    for (int k = 0; k < 13; ++k) {
        int key = topKey_ - k;
        float y = r.y + k * row;
        box(c, {r.x, y, keyW, row}, 0xffd4d5d1);
        line(c, r.x, y + row, r.x + keyW, y + row, 0xffa5a9a6);
        if (black(key)) {
            box(c, {r.x, y, keyW * .68f, row}, 0xff444849);
            line(c, r.x, y + row, r.x + keyW * .68f, y + row, 0xff2b2d2e);
        }
        if (key == auditionKey_ && selected_ == auditionTrack_) {
            box(c, {r.x, y, keyW, row}, gold);
        }
        text(c, noteName(key), r.x + 3, y + 12, 9, black(key) ? 0xffcbd0cb : 0xff656967);
        buttons_.push_back(
            {{r.x, y, keyW, row}, "key" + std::to_string(key), "Audition " + noteName(key), true});
        line(c, grid_.x, y, grid_.x + grid_.w, y, 0xff3b3f40);
    }
    line(c, grid_.x - 1, r.y, grid_.x - 1, r.y + grid_.h, 0xff666b6a);
    c.save();
    c.clipRect(SkRect::MakeXYWH(grid_.x, grid_.y, grid_.w, grid_.h));
    for (int k = 0; k <= 32; ++k) {
        float x = grid_.x + float(k * .125 - scrollBeat_) * bw;
        line(c, x, r.y, x, r.y + grid_.h, k % 4 == 0 ? 0xff4e5252 : 0xff333839);
    }
    box(c, {grid_.x, grid_.y + grid_.h / 2, grid_.w, grid_.h / 2}, 0x22111111);
    for (size_t i = 0; i < project_.tracks[size_t(selected_)].pattern().notes.size(); ++i) {
        const auto &n = project_.tracks[size_t(selected_)].pattern().notes[i];
        if (n.key > topKey_ || n.key < topKey_ - 12) {
            continue;
        }
        Rect nr{grid_.x + float(n.beat - scrollBeat_) * bw, grid_.y + (topKey_ - n.key) * row + 1,
                std::max(3.0f, float(n.length) * bw), row - 2};
        box(c, nr, (editing_.contains(i) || int(i) == selectedNote_) ? 0xffe8be87 : 0xffd9ab76, 1);
        outline(c, nr, 0xffefd0a4);
        text(c, noteName(n.key), nr.x + 4, nr.y + 11, 9, 0xff463528);
    }
    float ph = grid_.x + float(beat - scrollBeat_) * bw;
    line(c, ph, grid_.y, ph, grid_.y + grid_.h, 0xffdcb274);
    c.restore();
    box(c, {r.x, velocity_.y, keyW, velH}, panel);
    line(c, r.x, velocity_.y, r.x + r.w, velocity_.y, 0xff414546, 2);
    text(c, "VEL", r.x + 8, velocity_.y + 20, 11, 0xff989f9b);
    line(c, grid_.x - 1, velocity_.y, grid_.x - 1, velocity_.y + velH, 0xff424747);
    box(c, velocity_, 0xff222527);
    c.save();
    c.clipRect(SkRect::MakeXYWH(velocity_.x, velocity_.y, velocity_.w, velocity_.h));
    for (int k = 0; k <= 8; ++k) {
        float x = grid_.x + float(k * .5 - scrollBeat_) * bw;
        line(c, x, velocity_.y, x, velocity_.y + velH, 0xff3b4040);
    }
    for (const auto &n : project_.tracks[size_t(selected_)].pattern().notes) {
        float x = grid_.x + float(n.beat - scrollBeat_) * bw, hh = n.velocity * (velH - 10);
        box(c, {x + 2, velocity_.y + velH - 5 - hh, 3, hh}, 0xffd9ac78);
    }
    c.restore();
}

void Workspace::devices(SkCanvas &c, Rect r) {
    box(c, r, panel);
    const auto &track = project_.tracks[size_t(selected_)];
    const std::array names{track.instrument == audio::Instrument::Synth     ? std::string("Synth")
                           : track.instrument == audio::Instrument::Sampler ? std::string("Sampler")
                                                                            : std::string("Drum machine"),
                           std::string("EQ"), std::string("Compressor / Delay")};
    const std::array<std::array<const char *, 3>, 3> labels{
        {{{"Cutoff", "Attack", "Release"}}, {{"Low", "Mid", "High"}}, {{"Thresh", "Wet", "Time"}}}};
    const std::array<std::array<int, 3>, 3> knobs{{{{0, 2, 3}}, {{4, 5, 6}}, {{7, 8, 9}}}};
    const float cardW = std::min(176.0f, (r.w - 32) / 3);
    for (int i = 0; i < 3; ++i) {
        float x = r.x + 10 + i * (cardW + 7);
        Rect card{x, r.y + 10, cardW, r.h - 20};
        box(c, card, 0xff2d3031);
        outline(c, card, 0xff515657);
        text(c, names[i], x + 10, r.y + 31, 10, 0xffd8dad5, true);
        if (i == 0) {
            buttons_.push_back({{x + 4, r.y + 12, cardW - 8, 24},
                                "instrument",
                                "Cycle Synth / Sampler / Drum machine",
                                true});
        }
        for (int k = 0; k < 3; ++k) {
            float cx = x + cardW * (k + .6f) / 3, cy = r.y + 64.5f;
            SkPaint paint;
            paint.setStyle(SkPaint::kStroke_Style);
            paint.setStrokeWidth(5);
            paint.setColor(0xff575e5c);
            paint.setAntiAlias(true);
            c.drawCircle(cx, cy, 13, paint);
            paint.setColor(0xffd6aa73);
            c.drawArc(SkRect::MakeXYWH(cx - 13, cy - 13, 26, 26), -135, 90, false, paint);
            centered(
                c, i == 0 && k == 0 && track.instrument == audio::Instrument::Sampler ? "Root" : labels[i][k],
                {cx - 22, cy + 21.5f, 44, 13}, 9, 0xffaaaaaa);
            buttons_.push_back(
                {{cx - 17, cy - 17, 34, 48},
                 "knob" + std::to_string(i == 0 && k == 0 && track.instrument == audio::Instrument::Sampler
                                             ? 11
                                             : knobs[i][k]),
                 std::string("Drag: ") + labels[i][k],
                 true});
        }
        if (i == 0) {
            const auto *source = project_.source(track.instrument == audio::Instrument::Drum &&
                                                         track.drumSources[size_t(drumPad_)]
                                                     ? track.drumSources[size_t(drumPad_)]
                                                     : track.instrumentSource);
            tool(c, {x + 8, r.y + 111, cardW - 16, 23},
                 track.instrument == audio::Instrument::Synth
                     ? std::array{"Sine", "Saw", "Triangle"}[track.waveform]
                 : source ? source->name
                          : "Missing sample",
                 track.instrument == audio::Instrument::Synth ? "waveform" : "instrument-source");
            tool(c, {x + 8, r.y + 139, cardW - 16, 23},
                 track.instrument == audio::Instrument::Drum ? "Pad: " + std::to_string(36 + drumPad_)
                 : monitoring                                ? "Monitor: On"
                                                             : "Monitor: Off",
                 track.instrument == audio::Instrument::Drum ? "drum-pad" : "monitor", monitoring);
            tool(c, {x + 8, r.y + 167, cardW - 16, 23},
                 "Input: " + std::string(track.inputChannel == 0   ? "Stereo"
                                         : track.inputChannel == 1 ? "Left"
                                                                   : "Right"),
                 "input-channel");
        } else if (i == 1) {
            text(c, "Low / Mid / High", x + 10, r.y + 128, 10, dim);
            text(c,
                 number(track.strip.lowDb) + " / " + number(track.strip.midDb) + " / " +
                     number(track.strip.highDb) + " dB",
                 x + 10, r.y + 149, 10);
        } else {
            text(c, "Threshold " + number(track.strip.thresholdDb) + " dB", x + 10, r.y + 128, 10);
            tool(c, {x + 8, r.y + 136, cardW - 16, 23},
                 "Lookahead " + number(double(track.strip.lookaheadFrames) / 48) + " ms", "lookahead", false,
                 true, "Cycle 0 / 2 / 5 / 10 ms; mixer routes compensate automatically");
            tool(c, {x + 8, r.y + 167, cardW - 16, 23},
                 project_.buses[0].delayWet > 0 ? "Bus 1 delay: On" : "Bus 1 delay: Off", "bus-delay",
                 project_.buses[0].delayWet > 0);
        }
    }
}

void Workspace::mixer(SkCanvas &c, Rect r) {
    box(c, r, 0xff202324);
    mixerScroll_ =
        std::clamp(mixerScroll_, 0.0f,
                   std::max(0.0f, float((project_.tracks.size() + 1 + audio::Plan::maxBuses) * 95) - r.w));
    for (size_t i = 0; i <= project_.tracks.size(); ++i) {
        bool master = i == project_.tracks.size();
        float x = r.x + i * 95 - mixerScroll_;
        box(c, {x, r.y, 95, r.h}, 0xff2b2e30);
        line(c, x + 94, r.y, x + 94, r.y + r.h, 0xff44494a);
        box(c, {x + 9, r.y + 8, 77, 3}, master ? 0xffb8babc : colors[i % colors.size()]);
        text(c, master ? "Master" : project_.tracks[i].name, x + 9, r.y + 30, 10, 0xffd4d8d4);
        float fh = std::min(122.0f, r.h - 90), fy = r.y + 42 + (r.h - 90 - fh) / 2;
        float gain = master ? project_.masterGain : project_.tracks[i].gain;
        faders_[i] = {x + 25, fy, 21, fh};
        box(c, {x + 34, fy, 3, fh}, 0xff111718);
        float at = fy + (1 - gain) * fh;
        box(c, {x + 26, at - 4, 19, 8}, 0xffa9aaa5, 2);
        line(c, x + 29, at, x + 42, at, 0xff343837);
        buttons_.push_back({intersect(faders_[i], hitClip_), "gain" + std::to_string(i),
                            master ? "Master volume" : "Channel volume", true});
        Rect meter{x + 55, fy, 7, fh};
        box(c, meter, 0xff121718);
        outline(c, meter, 0xff3d4642);
        float peak = master ? engine_.peak() : engine_.trackPeak(i);
        float level = peak > 0 ? std::clamp((20 * std::log10(peak) + 60) / 60, 0.0f, 1.0f) : 0;
        for (int j = 0; j < int(fh * level); ++j) {
            float t = float(j) / fh;
            SkColor color = t > .8 ? mixed(0xff93af7d, 0xffd1b776, (t - .8f) / .2f)
                                   : mixed(0xff4a8568, 0xff93af7d, t / .8f);
            box(c, {meter.x + 1, meter.y + fh - j - 1, 5, 1}, color);
        }
        float y = r.y + r.h - 31;
        if (!master) {
            tool(c, {x + 5, r.y + 32, 40, 15}, "Pan " + number(project_.tracks[i].strip.pan),
                 "pan" + std::to_string(i), false, true, "Drag pan");
            tool(c, {x + 46, r.y + 32, 44, 15},
                 project_.tracks[i].strip.route < 0
                     ? "Master"
                     : "Bus " + std::to_string(project_.tracks[i].strip.route + 1),
                 "route" + std::to_string(i));
            tool(c, {x + 5, r.y + r.h - 52, 85, 17}, "Send 1: " + number(project_.tracks[i].strip.sends[0]),
                 "send" + std::to_string(i));
        }
        for (int k = 0; k < 2; ++k) {
            Rect b{x + 9 + k * 40.0f, y, 37, 23};
            bool on = master ? (k == 0 && project_.masterMuted)
                             : (k == 0 ? project_.tracks[i].muted : project_.tracks[i].solo);
            box(c, b, on ? 0xff9c7654 : 0xff2b2e30);
            outline(c, b, 0xff434849);
            tool(c, b, k == 0 ? "M" : "S",
                 master ? (k == 0 ? "mastermute" : "mastersolo")
                        : (k == 0 ? "mute" : "solo") + std::to_string(i),
                 false, !master || k == 0,
                 master && k == 1 ? "Master solo is not available"
                 : k == 0         ? "Mute"
                                  : "Solo");
        }
    }
    for (size_t bus = 0; bus < audio::Plan::maxBuses; ++bus) {
        float x = r.x + float(project_.tracks.size() + 1 + bus) * 95 - mixerScroll_;
        box(c, {x, r.y, 95, r.h}, 0xff2b2e30);
        line(c, x + 94, r.y, x + 94, r.y + r.h, edge);
        box(c, {x + 9, r.y + 8, 77, 3}, gold);
        text(c, "Bus " + std::to_string(bus + 1), x + 9, r.y + 30, 10);
        tool(c, {x + 8, r.y + 35, 79, 20},
             project_.buses[bus].route < 0 ? "To Master"
                                           : "To Bus " + std::to_string(project_.buses[bus].route + 1),
             "bus-route:" + std::to_string(bus));
        float fh = std::max(20.0f, r.h - 113), fy = r.y + 62;
        size_t index = project_.tracks.size() + 1 + bus;
        faders_[index] = {x + 28, fy, 22, fh};
        box(c, {x + 37, fy, 3, fh}, bg);
        float at = fy + (1 - project_.busGains[bus]) * fh;
        box(c, {x + 28, at - 4, 22, 8}, 0xffa9aaa5, 2);
        buttons_.push_back(
            {intersect(faders_[index], hitClip_), "gain" + std::to_string(index), "Bus volume", true});
        tool(c, {x + 8, r.y + r.h - 34, 79, 23}, project_.buses[bus].delayWet > 0 ? "Delay On" : "Delay Off",
             "bus-fx:" + std::to_string(bus));
    }
}

void Workspace::action(const std::string &act) {
    try {
        if (busy()) {
            return;
        }
        if ((recordingAudio || recordingMidi) && act != "stop" && act != "record" && act != "record-midi" &&
            act != "piano" && act != "devices" && act != "mixer" && act != "audio-settings" &&
            act != "input-settings" && act != "input-page" && act != "midi-settings" && act != "midi-page" &&
            act != "device-page") {
            status = "Stop the take before editing the document";
            return;
        }
        if (act == "record") {
            if (requestRecord) {
                requestRecord();
            }
            return;
        }
        if (act == "monitor") {
            if (requestMonitor) {
                requestMonitor();
            }
            return;
        }
        if (act == "record-midi") {
            if (recordingMidi) {
                finishMidi();
            } else {
                if (recordingAudio) {
                    status = "Stop audio take first";
                    return;
                }
                midiTrack_ = size_t(selected_);
                midiTake_.begin(engine_.timestamp(),
                                double(engine_.position()) / (engine_.sampleRate() * 60 / project_.bpm),
                                engine_.sampleRate() * 60 / project_.bpm);
                recordingMidi = true;
                engine_.looping(false);
                engine_.play(true);
                status = "Recording MIDI to " + project_.tracks[midiTrack_].name;
            }
            return;
        }
        if (act == "play") {
            engine_.play(!engine_.playing());
            status = engine_.playing() ? "Playing" : "Paused";
            return;
        }
        if (act == "stop") {
            if (recordingAudio && requestRecord) {
                requestRecord();
            }
            finishMidi();
            engine_.play(false);
            engine_.rewind();
            status = "Ready";
            return;
        }
        if (act == "piano" || act == "devices" || act == "mixer") {
            release();
            detail_ = act == "piano" ? Detail::Piano : act == "devices" ? Detail::Devices : Detail::Mixer;
            return;
        }
        if (act == "browser") {
            browser_ = !browser_;
            return;
        }
        if (act == "fold") {
            folded_ = !folded_;
            return;
        }
        if (act == "draw") {
            erase_ = !erase_;
            status = erase_ ? "Erase tool" : "Draw tool";
            return;
        }
        if (act == "snap") {
            snap_ = snap_ == .25 ? .5 : .25;
            return;
        }
        if (act == "zoom+" || act == "zoom-") {
            arrangementZoom_ = std::clamp(arrangementZoom_ * (act == "zoom+" ? 1.25 : .8), .25, 8.0);
            return;
        }
        if (act == "tempo") {
            focus_ = Focus::Tempo;
            tempoText_.clear();
            status = "Enter tempo (20–300 BPM)";
            return;
        }
        if (act == "search") {
            focus_ = Focus::Search;
            return;
        }
        if (act == "instruments") {
            detail_ = Detail::Devices;
            search_.clear();
            focus_ = Focus::None;
            return;
        }
        if (act == "save" || act == "saveas" || act == "load" || act == "export" || act == "import" ||
            act == "relink" || act == "record-audio") {
            if (act == "relink") {
                auto &clips = project_.tracks[size_t(selected_)].clips;
                if (selectedClip_ < 0 || selectedClip_ >= int(clips.size()) ||
                    !clips[size_t(selectedClip_)].audio) {
                    return;
                }
                relinkId_ = clips[size_t(selectedClip_)].sourceId;
            }
            if (act == "save" && !document_.path().empty()) {
                fileSelected(act, document_.path());
            } else if (requestFile) {
                dialogPending_ = true;
                status = "Choose file: " + act;
                requestFile(act);
            } else {
                status = "File dialog unavailable";
            }
            return;
        }
        if (act == "pattern-page") {
            patternPage_ =
                (patternPage_ + 1) % int((project_.tracks[size_t(selected_)].patterns.size() + 7) / 8);
            return;
        }
        if (act == "device-page") {
            devicePage_ = (devicePage_ + 1) % std::max(1, int((audioDevices.size() + 7) / 8));
            return;
        }
        if (act == "patterns") {
            patternMenu_ = !patternMenu_;
            audioMenu_ = false;
            return;
        }
        if (act == "audio-settings") {
            inputMenu_ = midiMenu_ = false;
            audioMenu_ = !audioMenu_;
            patternMenu_ = false;
            return;
        }
        if (act.starts_with("device:")) {
            audioConfig.deviceId = act.substr(7);
            if (requestAudio) {
                requestAudio(audioConfig);
            }
            audioMenu_ = false;
            return;
        }
        if (act.starts_with("buffer:")) {
            audioConfig.bufferFrames = uint32_t(std::stoul(act.substr(7)));
            if (requestAudio) {
                requestAudio(audioConfig);
            }
            return;
        }
        if (act == "rename-pattern") {
            focus_ = Focus::PatternName;
            editText_ = project_.tracks[size_t(selected_)].pattern().name;
            patternMenu_ = false;
            status = "Pattern name: type and Enter";
            return;
        }
        if (act == "source-offset") {
            focus_ = Focus::SourceOffset;
            editText_.clear();
            status = "Source start in seconds: type and Enter";
            return;
        }
        if (act == "drum-pad") {
            drumPad_ = (drumPad_ + 1) % 16;
            return;
        }
        if (act == "copy") {
            editing_.copy(project_, size_t(selected_));
            status = "Selection copied";
            return;
        }
        if (act == "select-all") {
            editing_.clear();
            if (editing_.notes) {
                for (size_t i = 0; i < project_.tracks[size_t(selected_)].pattern().notes.size(); ++i) {
                    editing_.selectedNotes.push_back(i);
                }
            } else {
                for (size_t t = 0; t < project_.tracks.size(); ++t) {
                    for (size_t i = 0; i < project_.tracks[t].clips.size(); ++i) {
                        editing_.selectedClips.push_back({t, i});
                    }
                }
            }
            return;
        }
        if (act == "input-settings" || act == "midi-settings") {
            inputMenu_ = act == "input-settings";
            midiMenu_ = !inputMenu_;
            audioMenu_ = patternMenu_ = false;
            return;
        }
        if (act == "input-page" || act == "midi-page") {
            auto &page = act == "input-page" ? inputPage_ : midiPage_;
            const auto count = act == "input-page" ? inputDevices.size() : midiPorts.size();
            page = (page + 1) % int(std::max<size_t>(1, (count + 7) / 8));
            return;
        }
        if (act.starts_with("input:")) {
            inputConfig.deviceId = act.substr(6);
            if (requestInput) {
                requestInput(inputConfig);
            }
            inputMenu_ = false;
            return;
        }
        if (act.starts_with("midi-port:")) {
            if (requestMidi) {
                requestMidi(size_t(std::stoul(act.substr(10))));
            }
            midiMenu_ = false;
            return;
        }
        if (act == "undo" || act == "redo") {
            auto &from = act == "undo" ? undo_ : redo_;
            auto &to = act == "undo" ? redo_ : undo_;
            if (!from.empty()) {
                to.push_back(project_);
                project_ = std::move(from.back());
                from.pop_back();
                selected_ = std::min(selected_, int(project_.tracks.size()) - 1);
                selectedNote_ = -1;
                selectedClip_ = 0;
                editing_.clear();
                changed();
                status = act == "undo" ? "Undo" : "Redo";
            }
            return;
        }
        if (act.starts_with("track")) {
            size_t i = std::stoul(act.substr(5));
            if (i < project_.tracks.size()) {
                release();
                if (selected_ != int(i)) {
                    editing_.clear();
                }
                selected_ = int(i);
                selectedClip_ = 0;
                selectedNote_ = -1;
                topKey_ = i == 1 ? 45 : i == 2 ? 48 : i == 4 ? 84 : 72;
                status = project_.tracks[i].name;
            }
            return;
        }
        if (act.starts_with("key")) {
            release();
            int key = std::stoi(act.substr(3));
            if (engine_.audition({uint8_t(key), uint8_t(selected_), 100})) {
                auditionKey_ = key;
                auditionTrack_ = selected_;
            } else {
                status = "Audition queue full";
            }
            return;
        }
        if (act.starts_with("gain")) {
            gainDrag_ = std::stoi(act.substr(4));
            before_ = project_;
            move(mouseX_, mouseY_);
            return;
        }
        if (act.starts_with("pan")) {
            editing_.clear();
            selectedNote_ = selectedClip_ = -1;
            selected_ = std::stoi(act.substr(3));
            knobDrag_ = 10;
            before_ = project_;
            dragY_ = mouseY_;
            knobOriginal_ = project_.tracks[size_t(selected_)].strip.pan;
            return;
        }
        if (act.starts_with("route") || act.starts_with("send")) {
            if (act.size() > (act.starts_with("route") ? 5u : 4u)) {
                editing_.clear();
                selectedNote_ = selectedClip_ = -1;
                selected_ = std::stoi(act.substr(act.starts_with("route") ? 5 : 4));
            }
        }
        if (act.starts_with("knob")) {
            knobDrag_ = std::stoi(act.substr(4));
            before_ = project_;
            dragY_ = mouseY_;
            const auto &t = project_.tracks[size_t(selected_)];
            knobOriginal_ = knobDrag_ == 0    ? t.cutoff
                            : knobDrag_ == 2  ? t.attack
                            : knobDrag_ == 3  ? t.release
                            : knobDrag_ == 4  ? t.strip.lowDb
                            : knobDrag_ == 5  ? t.strip.midDb
                            : knobDrag_ == 6  ? t.strip.highDb
                            : knobDrag_ == 7  ? t.strip.thresholdDb
                            : knobDrag_ == 8  ? t.strip.delayWet
                            : knobDrag_ == 11 ? float(t.rootKey)
                                              : t.strip.delaySeconds;
            return;
        }
        before_ = project_;
        if (act == "new-project") {
            project_ = Project::empty();
            selected_ = 0;
            selectedClip_ = selectedNote_ = -1;
            editing_.clear();
            arrangementScroll_ = 0;
            document_.opened(project_, {});
            status = "New project";
        } else if (act == "record-latency-auto") {
            project_.automaticRecordLatency = !project_.automaticRecordLatency;
        } else if (act.starts_with("record-offset:")) {
            project_.recordOffsetFrames =
                std::clamp(project_.recordOffsetFrames + std::stoi(act.substr(14)), -48000, 48000);
        } else if (act == "record-offset-reset") {
            project_.recordOffsetFrames = 0;
        } else if (act == "lookahead") {
            auto &frames = project_.tracks[size_t(selected_)].strip.lookaheadFrames;
            frames = frames == 0 ? 96 : frames == 96 ? 240 : frames == 240 ? 480 : 0;
        } else if (act == "extend-timeline") {
            project_.timelineBeats = std::min(Project::maxTimeline, project_.timelineBeats + 32);
        } else if (act == "loop-to-end") {
            project_.loopBeats = project_.timelineBeats;
        } else if (act == "paste" || act == "duplicate" || act == "delete" || act == "cut") {
            if (act == "cut") {
                editing_.copy(project_, size_t(selected_));
            }
            if (act == "paste") {
                editing_.paste(project_, size_t(selected_), pasteBeat_);
            } else if (act == "duplicate") {
                editing_.duplicate(project_, size_t(selected_));
            } else {
                editing_.erase(project_, size_t(selected_));
                selectedNote_ = selectedClip_ = -1;
            }
        } else if (act.starts_with("nudge:")) {
            int direction = std::stoi(act.substr(6));
            editing_.transform(project_, size_t(selected_),
                               direction == 1    ? snap_
                               : direction == -1 ? -snap_
                                                 : 0,
                               direction == 2    ? 1
                               : direction == -2 ? -1
                                                 : 0,
                               false);
        } else if (act.starts_with("resize:")) {
            editing_.transform(project_, size_t(selected_), std::stoi(act.substr(7)) * snap_, 0, true);
        } else if (act.starts_with("arm")) {
            auto &track = project_.tracks.at(size_t(std::stoul(act.substr(3))));
            track.armed = !track.armed;
        } else if (act == "input-channel") {
            auto &track = project_.tracks[size_t(selected_)];
            track.inputChannel = (track.inputChannel + 1) % 3;
        } else if (act == "instrument") {
            auto &track = project_.tracks[size_t(selected_)];
            track.instrument = audio::Instrument((int(track.instrument) + 1) % 3);
            if (track.instrument != audio::Instrument::Synth) {
                const auto *source = project_.source(track.instrumentSource);
                if (!source || source->stream) {
                    throw std::runtime_error("Choose a resident sample for this instrument");
                }
            }
        } else if (act == "instrument-source") {
            auto &track = project_.tracks[size_t(selected_)];
            uint32_t current =
                track.instrument == audio::Instrument::Drum && track.drumSources[size_t(drumPad_)]
                    ? track.drumSources[size_t(drumPad_)]
                    : track.instrumentSource;
            size_t at = 0;
            for (size_t i = 0; i < project_.sources.size(); ++i) {
                if (project_.sources[i].id == current) {
                    at = i;
                }
            }
            for (size_t i = 1; i <= project_.sources.size(); ++i) {
                const auto &source = project_.sources[(at + i) % project_.sources.size()];
                if (source.pcm) {
                    if (track.instrument == audio::Instrument::Drum) {
                        track.drumSources[size_t(drumPad_)] = source.id;
                    } else {
                        track.instrumentSource = source.id;
                    }
                    break;
                }
            }
        } else if (act == "waveform") {
            auto &track = project_.tracks[size_t(selected_)];
            track.waveform = uint8_t((track.waveform + 1) % 3);
        } else if (act.starts_with("route")) {
            auto &strip = project_.tracks[size_t(selected_)].strip;
            strip.route = strip.route == 3 ? -1 : strip.route + 1;
        } else if (act.starts_with("send")) {
            auto &strip = project_.tracks[size_t(selected_)].strip;
            strip.sends[0] = strip.sends[0] == 0 ? .25f : 0;
        } else if (act.starts_with("bus-route:")) {
            auto bus = size_t(std::stoul(act.substr(10)));
            auto &strip = project_.buses.at(bus);
            strip.route = strip.route == 3 ? -1 : strip.route < 0 ? int(bus + 1) : strip.route + 1;
            if (bus == 3) {
                strip.route = -1;
            }
        } else if (act.starts_with("bus-fx:")) {
            auto &strip = project_.buses.at(size_t(std::stoul(act.substr(7))));
            strip.delayWet = strip.delayWet == 0 ? .4f : 0;
        } else if (act == "bus-delay") {
            project_.buses[0].delayWet = project_.buses[0].delayWet == 0 ? .4f : 0;
        } else if (act == "new-pattern" || act == "duplicate-pattern") {
            editing_.clear();
            project_.tracks[size_t(selected_)].createPattern(act == "duplicate-pattern");
            selectedNote_ = -1;
            if (selectedClip_ >= 0 && selectedClip_ < int(project_.tracks[size_t(selected_)].clips.size()) &&
                project_.tracks[size_t(selected_)].clips[size_t(selectedClip_)].audio) {
                selectedClip_ = -1;
            }
            patternMenu_ = false;
            status = project_.tracks[size_t(selected_)].pattern().name;
        } else if (act.starts_with("select-pattern:")) {
            editing_.clear();
            auto id = uint32_t(std::stoul(act.substr(15)));
            auto &track = project_.tracks[size_t(selected_)];
            if (const auto *pattern = track.findPattern(id)) {
                track.activePattern = id;
                if (!pattern->notes.empty()) {
                    topKey_ = std::clamp(
                        std::max_element(pattern->notes.begin(), pattern->notes.end(),
                                         [](const auto &a, const auto &b) { return a.key < b.key; })
                            ->key,
                        12, 127);
                }
                if (selectedClip_ >= 0 && selectedClip_ < int(track.clips.size()) &&
                    track.clips[size_t(selectedClip_)].audio) {
                    selectedClip_ = -1;
                }
            }
            selectedNote_ = -1;
            patternMenu_ = false;
        } else if (act == "assign-pattern") {
            auto &track = project_.tracks[size_t(selected_)];
            if (selectedClip_ >= 0 && selectedClip_ < int(track.clips.size()) &&
                !track.clips[size_t(selectedClip_)].audio) {
                auto &clip = track.clips[size_t(selectedClip_)];
                clip.patternId = track.activePattern;
                clip.name = track.pattern().name;
            }
            patternMenu_ = false;
        } else if (act == "source-loop") {
            auto &clips = project_.tracks[size_t(selected_)].clips;
            if (selectedClip_ >= 0 && selectedClip_ < int(clips.size()) &&
                clips[size_t(selectedClip_)].audio) {
                clips[size_t(selectedClip_)].sourceLoop = !clips[size_t(selectedClip_)].sourceLoop;
            }
        } else if (act == "addtrack" && project_.tracks.size() < audio::Plan::maxTracks) {
            Track t;
            t.name = "Instrument " + std::to_string(project_.tracks.size() + 1);
            project_.tracks.push_back(t);
            selected_ = int(project_.tracks.size() - 1);
            selectedClip_ = 0;
            selectedNote_ = -1;
            topKey_ = 72;
            editing_.clear();
        } else if (act == "loop") {
            project_.loop = !project_.loop;
        } else if (act == "metronome") {
            project_.metronome = !project_.metronome;
        } else if (act == "mastermute") {
            project_.masterMuted = !project_.masterMuted;
        } else if (act == "song" || act == "pattern") {
            project_.songMode = act == "song";
            engine_.rewind();
        } else if (act == "quantize") {
            for (auto &n : project_.tracks[size_t(selected_)].pattern().notes) {
                n.beat = std::clamp(std::round(n.beat / snap_) * snap_, 0.0, 4 - snap_);
                n.length = std::min(n.length, 4 - n.beat);
            }
        } else if (act.starts_with("mute") || act.starts_with("solo")) {
            size_t i = std::stoul(act.substr(4));
            if (i < project_.tracks.size()) {
                auto &t = project_.tracks[i];
                if (act.starts_with("mute")) {
                    t.muted = !t.muted;
                } else {
                    t.solo = !t.solo;
                }
            }
        } else {
            status = "Not implemented: " + act;
        }
        commitEdit();
        changed();
    } catch (const std::exception &e) {
        if (before_) {
            project_ = *before_;
            before_.reset();
        }
        status = e.what();
    }
}

void Workspace::press(float x, float y, bool right, bool additive) {
    if (busy()) {
        return;
    }
    if (recordingAudio || recordingMidi) {
        for (const auto &b : buttons_) {
            const bool diagnostic = b.action == "audio-settings" || b.action == "input-settings" ||
                                    b.action == "input-page" || b.action == "device-page" ||
                                    b.action == "midi-settings" || b.action == "midi-page" ||
                                    b.action == "piano" || b.action == "devices" || b.action == "mixer";
            if (b.enabled && (b.action == "record" || b.action == "stop" || diagnostic) &&
                b.rect.contains(x, y)) {
                action(b.action);
                return;
            }
        }
        return;
    }
    release();
    mouseX_ = x;
    mouseY_ = y;
    if (focus_ == Focus::Tempo && !tempo_.contains(x, y)) {
        editKey("enter");
    }
    if ((focus_ == Focus::PatternName || focus_ == Focus::SourceOffset)) {
        editKey("enter");
    }
    if (focus_ == Focus::Search && !searchBox_.contains(x, y)) {
        focus_ = Focus::None;
    }
    if ((patternMenu_ || audioMenu_ || inputMenu_ || midiMenu_) && !menuRect_.contains(x, y)) {
        patternMenu_ = audioMenu_ = inputMenu_ = midiMenu_ = false;
        return;
    }
    if (divider_.contains(x, y)) {
        dividerDrag_ = true;
        folded_ = false;
        return;
    }
    for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
        const auto &b = *it;
        if (b.rect.contains(x, y)) {
            if (!b.enabled) {
                status = b.tooltip;
                return;
            }
            if (b.action == "seek") {
                double beat =
                    std::clamp(arrangementScroll_ + double(x - b.rect.x) / b.rect.w * (visibleBeats()), 0.0,
                               project_.timelineBeats - .001);
                engine_.seek(uint64_t(std::llround(beat * engine_.sampleRate() * 60 / project_.bpm)));
                return;
            }
            action(b.action);
            return;
        }
    }
    if (patternMenu_ || audioMenu_ || inputMenu_ || midiMenu_) {
        patternMenu_ = audioMenu_ = inputMenu_ = midiMenu_ = false;
        return;
    }
    if (lanes_.contains(x, y)) {
        int track = int((y - lanes_.y + trackScroll_) / 79);
        if (track < 0 || track >= int(project_.tracks.size())) {
            return;
        }
        if (!additive && selected_ != track) {
            editing_.clear();
        }
        selected_ = track;
        selectedNote_ = -1;
        if (editing_.notes) {
            editing_.clear();
        }
        editing_.notes = false;
        pasteBeat_ = std::clamp(arrangementScroll_ + double(x - lanes_.x) / lanes_.w * (visibleBeats()), 0.0,
                                project_.timelineBeats - .25);
        auto &clips = project_.tracks[size_t(track)].clips;
        for (auto it = clips_.rbegin(); it != clips_.rend(); ++it) {
            if (it->track == size_t(track) && it->rect.contains(x, y)) {
                selectedClip_ = int(it->clip);
                before_ = project_;
                ClipRef ref{size_t(track), it->clip};
                if (additive || !editing_.contains(ref)) {
                    editing_.selectClip(ref, additive);
                }
                if (additive) {
                    before_.reset();
                    return;
                }
                if (!clips[it->clip].audio) {
                    project_.tracks[size_t(track)].activePattern = clips[it->clip].patternId;
                    const auto &notes = project_.tracks[size_t(track)].pattern().notes;
                    if (!notes.empty()) {
                        const int highest =
                            std::max_element(notes.begin(), notes.end(), [](const auto &a, const auto &b) {
                                return a.key < b.key;
                            })->key;
                        if (highest > topKey_ || highest < topKey_ - 12) {
                            topKey_ = std::clamp(highest, 12, 127);
                        }
                    }
                    dirty_ = true;
                }
                if (right || erase_) {
                    editing_.erase(project_, size_t(selected_));
                    selectedClip_ = -1;
                    commitEdit();
                    changed();
                    return;
                }
                clipDrag_ = selectedClip_;
                originalClip_ = clips[it->clip];
                dragX_ = x;
                float end =
                    lanes_.x + float((originalClip_.beat + originalClip_.length - arrangementScroll_) /
                                     (visibleBeats())) *
                                   lanes_.w;
                clipResize_ = x >= end - 9;
                return;
            }
        }
        if (additive && !right) {
            marquee_ = true;
            additiveMarquee_ = additive;
            selectionBox_ = {x, y, 0, 0};
            dragX_ = x;
            dragY_ = y;
            return;
        }
        if (!right && !erase_) {
            before_ = project_;
            double beat =
                std::floor((arrangementScroll_ + (x - lanes_.x) / lanes_.w * (visibleBeats())) / 4) * 4;
            Clip clip;
            clip.name = project_.tracks[size_t(track)].pattern().name;
            clip.patternId = project_.tracks[size_t(track)].activePattern;
            clip.beat = beat;
            clip.length = std::min(4.0, project_.timelineBeats - beat);
            clips.push_back(clip);
            selectedClip_ = int(clips.size() - 1);
            editing_.selectClip({size_t(track), size_t(selectedClip_)}, false);
            commitEdit();
            changed();
        }
        return;
    }
    const auto &selectedClips = project_.tracks[size_t(selected_)].clips;
    if (detail_ != Detail::Piano || (selectedClip_ >= 0 && selectedClip_ < int(selectedClips.size()) &&
                                     selectedClips[size_t(selectedClip_)].audio)) {
        return;
    }
    auto &notes = project_.tracks[size_t(selected_)].pattern().notes;
    float bw = grid_.w / 4 * float(zoom_), row = grid_.h / 13;
    if (velocity_.contains(x, y)) {
        int closest = -1;
        float distance = 9;
        for (size_t i = 0; i < notes.size(); ++i) {
            float d = std::abs(x - (grid_.x + float(notes[i].beat - scrollBeat_) * bw + 3));
            if (d < distance) {
                closest = int(i);
                distance = d;
            }
        }
        if (closest >= 0) {
            before_ = project_;
            notes[size_t(closest)].velocity =
                std::clamp((velocity_.y + velocity_.h - 5 - y) / (velocity_.h - 10), .01f, 1.0f);
            commitEdit();
            changed();
        }
        return;
    }
    if (!grid_.contains(x, y)) {
        return;
    }
    if (!editing_.notes) {
        editing_.clear();
    }
    editing_.notes = true;
    int key = std::clamp(topKey_ - int((y - grid_.y) / row), 0, 127);
    double beat = std::clamp(scrollBeat_ + (x - grid_.x) / bw, 0.0, 3.999);
    pasteBeat_ = std::floor(beat / snap_) * snap_;
    for (size_t i = notes.size(); i > 0; --i) {
        auto &n = notes[i - 1];
        if (n.key != key || beat < n.beat || beat > n.beat + n.length) {
            continue;
        }
        selectedNote_ = int(i - 1);
        if (additive || !editing_.contains(i - 1)) {
            editing_.selectNote(i - 1, additive);
        }
        if (additive) {
            return;
        }
        before_ = project_;
        if (right || erase_) {
            editing_.erase(project_, size_t(selected_));
            selectedNote_ = -1;
            commitEdit();
            changed();
            return;
        }
        noteDrag_ = selectedNote_;
        noteResize_ = x >= grid_.x + float(n.beat + n.length - scrollBeat_) * bw - 9;
        originalNote_ = n;
        dragX_ = x;
        dragY_ = y;
        return;
    }
    if (additive && !right) {
        marquee_ = true;
        additiveMarquee_ = additive;
        selectionBox_ = {x, y, 0, 0};
        dragX_ = x;
        dragY_ = y;
        return;
    }
    if (!right && !erase_) {
        before_ = project_;
        beat = std::floor(beat / snap_) * snap_;
        notes.push_back({key, beat, std::min(snap_, 4 - beat), .75f});
        selectedNote_ = int(notes.size() - 1);
        editing_.selectNote(size_t(selectedNote_), false);
        commitEdit();
        changed();
    }
}

void Workspace::move(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    if (marquee_) {
        selectionBox_ = {std::min(x, dragX_), std::min(y, dragY_), std::abs(x - dragX_),
                         std::abs(y - dragY_)};
    }
    if (dividerDrag_) {
        detailHeight_ = std::clamp(height_ - 23 - y - 5, 155.0f, height_ - 269);
    }
    if (gainDrag_ >= 0 && gainDrag_ < int(project_.tracks.size() + 1 + audio::Plan::maxBuses)) {
        const auto &r = faders_[size_t(gainDrag_)];
        float gain = std::clamp(1 - (y - r.y) / r.h, 0.0f, 1.0f);
        if (gainDrag_ == int(project_.tracks.size())) {
            project_.masterGain = gain;
        } else if (gainDrag_ > int(project_.tracks.size())) {
            project_.busGains[size_t(gainDrag_) - project_.tracks.size() - 1] = gain;
        } else {
            project_.tracks[size_t(gainDrag_)].gain = gain;
        }
        changed();
    }
    if (knobDrag_ >= 0) {
        auto &t = project_.tracks[size_t(selected_)];
        float d = (dragY_ - y) / 120;
        if (knobDrag_ == 0) {
            t.cutoff = std::clamp(knobOriginal_ * std::exp2(d * 5), 20.0f, 20000.0f);
        } else if (knobDrag_ == 2) {
            t.attack = std::clamp(knobOriginal_ + d * 2, .001f, 2.0f);
        } else if (knobDrag_ == 3) {
            t.release = std::clamp(knobOriginal_ + d * 2, .001f, 2.0f);
        } else if (knobDrag_ >= 4 && knobDrag_ <= 6) {
            float value = std::clamp(knobOriginal_ + d * 24, -24.0f, 24.0f);
            if (knobDrag_ == 4) {
                t.strip.lowDb = value;
            } else if (knobDrag_ == 5) {
                t.strip.midDb = value;
            } else {
                t.strip.highDb = value;
            }
        } else if (knobDrag_ == 7) {
            t.strip.thresholdDb = std::clamp(knobOriginal_ + d * 60, -60.0f, 0.0f);
        } else if (knobDrag_ == 8) {
            t.strip.delayWet = std::clamp(knobOriginal_ + d, 0.0f, 1.0f);
        } else if (knobDrag_ == 9) {
            t.strip.delaySeconds = std::clamp(knobOriginal_ + d, .01f, 1.0f);
        } else if (knobDrag_ == 11) {
            t.rootKey = uint8_t(std::clamp(int(std::lround(knobOriginal_ + d * 48)), 0, 127));
        } else if (knobDrag_ == 10) {
            t.strip.pan = std::clamp(knobOriginal_ + d * 2, -1.0f, 1.0f);
        }
        changed();
    }
    if (clipDrag_ >= 0 && before_) {
        project_ = *before_;
        double delta = std::round((x - dragX_) / lanes_.w * (visibleBeats()) / snap_) * snap_;
        editing_.transform(project_, size_t(selected_), delta, 0, clipResize_);
        changed();
    }
    if (noteDrag_ >= 0 && before_) {
        project_ = *before_;
        double delta = std::round((x - dragX_) / (grid_.w / 4 * zoom_) / snap_) * snap_;
        editing_.transform(project_, size_t(selected_), delta,
                           -int(std::round((y - dragY_) / (grid_.h / 13))), noteResize_);
        changed();
    }
}

void Workspace::release() {
    if (marquee_) {
        if (!additiveMarquee_) {
            editing_.clear();
        }
        if (editing_.notes) {
            const auto &list = project_.tracks[size_t(selected_)].pattern().notes;
            for (size_t i = 0; i < list.size(); ++i) {
                const auto &n = list[i];
                Rect rect{grid_.x + float(n.beat - scrollBeat_) * grid_.w / 4 * float(zoom_),
                          grid_.y + (topKey_ - n.key) * grid_.h / 13,
                          float(n.length) * grid_.w / 4 * float(zoom_), grid_.h / 13};
                auto hit = intersect(rect, selectionBox_);
                if (hit.w > 0 && hit.h > 0 && !editing_.contains(i)) {
                    editing_.selectedNotes.push_back(i);
                }
            }
        } else {
            for (auto hit : clips_) {
                auto rect = intersect(hit.rect, selectionBox_);
                if (rect.w > 0 && rect.h > 0 && !editing_.contains(ClipRef{hit.track, hit.clip})) {
                    editing_.selectedClips.push_back({hit.track, hit.clip});
                }
            }
        }
        marquee_ = false;
    }
    if (auditionKey_ >= 0) {
        releaseKey_ = auditionKey_;
        releaseTrack_ = auditionTrack_;
        auditionKey_ = -1;
        sync();
    }
    commitEdit();
    dividerDrag_ = false;
    gainDrag_ = knobDrag_ = clipDrag_ = noteDrag_ = -1;
}

void Workspace::wheel(float dy, bool pitch, bool pan) {
    if (focus_ != Focus::None) {
        return;
    }
    if (pitch) {
        topKey_ = std::clamp(topKey_ + int(dy * 2), 12, 127);
    } else if (pan && mouseY_ < divider_.y) {
        arrangementScroll_ = std::clamp(arrangementScroll_ - dy * 2, 0.0,
                                        std::max(0.0, project_.timelineBeats - visibleBeats()));
    } else if (pan) {
        scrollBeat_ = std::clamp(scrollBeat_ + dy * .25, 0.0, 4 - 4 / zoom_);
    } else if (detail_ == Detail::Mixer && mouseY_ >= divider_.y) {
        mixerScroll_ -= dy * 40;
    } else if (mouseY_ < divider_.y && mouseY_ >= lanes_.y) {
        trackScroll_ -= dy * 40;
    } else {
        zoom_ = std::clamp(zoom_ * std::pow(1.2, double(dy)), 1.0, 8.0);
    }
    scrollBeat_ = std::clamp(scrollBeat_, 0.0, 4 - 4 / zoom_);
}

void Workspace::textInput(const std::string &value) {
    if (busy()) {
        return;
    }
    if (focus_ == Focus::Search) {
        if (search_.size() + value.size() <= 64) {
            search_ += value;
        }
    } else if (focus_ == Focus::PatternName) {
        if (editText_.size() + value.size() <= 80 && value.find_first_of("\r\n") == std::string::npos) {
            editText_ += value;
        }
    } else if (focus_ == Focus::SourceOffset) {
        for (char c : value) {
            if ((std::isdigit(static_cast<unsigned char>(c)) || c == '.') && editText_.size() < 12) {
                editText_ += c;
            }
        }
    } else if (focus_ == Focus::Tempo) {
        for (char c : value) {
            if ((std::isdigit(static_cast<unsigned char>(c)) || c == '.') && tempoText_.size() < 6) {
                tempoText_ += c;
            }
        }
    }
}

void Workspace::editKey(const std::string &key) {
    if (busy()) {
        return;
    }
    if (key == "escape") {
        focus_ = Focus::None;
        return;
    }
    if (key == "backspace") {
        auto &s = focus_ == Focus::Search ? search_ : focus_ == Focus::Tempo ? tempoText_ : editText_;
        if (!s.empty()) {
            size_t i = s.size() - 1;
            while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) {
                --i;
            }
            s.erase(i);
        }
        return;
    }
    if (key == "enter") {
        if (focus_ == Focus::PatternName || focus_ == Focus::SourceOffset) {
            before_ = project_;
            try {
                auto &track = project_.tracks[size_t(selected_)];
                if (focus_ == Focus::PatternName) {
                    if (editText_.empty()) {
                        throw std::runtime_error("Pattern name cannot be empty");
                    }
                    track.pattern().name = editText_;
                    for (auto &clip : track.clips) {
                        if (!clip.audio && clip.patternId == track.activePattern) {
                            clip.name = editText_;
                        }
                    }
                } else {
                    size_t used = 0;
                    const double offset = std::stod(editText_, &used);
                    if (used != editText_.size() || !std::isfinite(offset) || offset < 0 ||
                        selectedClip_ < 0 || selectedClip_ >= int(track.clips.size())) {
                        throw std::runtime_error("Invalid source start");
                    }
                    track.clips[size_t(selectedClip_)].sourceOffset = offset;
                }
                commitEdit();
                changed();
            } catch (const std::exception &error) {
                if (before_) {
                    project_ = *before_;
                    before_.reset();
                }
                status = error.what();
            }
        }
        if (focus_ == Focus::Tempo && !tempoText_.empty()) {
            try {
                size_t n = 0;
                double bpm = std::stod(tempoText_, &n);
                if (n != tempoText_.size() || !std::isfinite(bpm) || bpm < 20 || bpm > 300) {
                    throw std::invalid_argument("Tempo must be 20–300 BPM");
                }
                before_ = project_;
                project_.bpm = bpm;
                commitEdit();
                changed();
                status = "Tempo: " + number(bpm) + " BPM";
            } catch (const std::exception &e) {
                status = e.what();
            }
        }
        focus_ = Focus::None;
    }
}

void Workspace::verifyInteractions(SkCanvas &c, float w, float h) {
    const Project baseline = project_;
    auto check = [](bool ok, const char *message) {
        if (!ok) {
            throw std::runtime_error(message);
        }
    };
    auto redraw = [&] { draw(c, w, h); };
    auto click = [&](const std::string &a) {
        redraw();
        auto it = std::find_if(buttons_.begin(), buttons_.end(),
                               [&](const Button &b) { return b.action == a && b.enabled; });
        check(it != buttons_.end(), ("Missing control: " + a).c_str());
        Rect r = it->rect;
        press(r.x + r.w / 2, r.y + r.h / 2, false);
        release();
    };
    action("track0");
    action("piano");
    redraw();
    float testBw = grid_.w / 4, testRow = grid_.h / 13;
    auto firstNote = project_.tracks[0].pattern().notes[0],
         secondNote = project_.tracks[0].pattern().notes[1];
    float firstX = grid_.x + float(firstNote.beat + .1) * testBw,
          firstY = grid_.y + (topKey_ - firstNote.key + .5f) * testRow;
    press(firstX, firstY, false);
    release();
    press(grid_.x + float(secondNote.beat + .1) * testBw,
          grid_.y + (topKey_ - secondNote.key + .5f) * testRow, false, true);
    release();
    check(editing_.selectedNotes.size() == 2, "Shift multi-note selection failed");
    press(firstX, firstY, false);
    move(firstX + testBw * .25f, firstY);
    release();
    check(project_.tracks[0].pattern().notes[0].beat == firstNote.beat + .25 &&
              project_.tracks[0].pattern().notes[1].beat == secondNote.beat + .25,
          "Group note move failed");
    action("undo");
    press(firstX, firstY, false);
    release();
    action("copy");
    action("duplicate");
    check(project_.tracks[0].pattern().notes.size() == baseline.tracks[0].pattern().notes.size() + 1,
          "UI clipboard duplicate failed");
    action("delete");
    action("undo");
    action("undo");
    check(project_.tracks[0].pattern().notes == baseline.tracks[0].pattern().notes,
          "Grouped clipboard Undo failed");
    click("extend-timeline");
    check(project_.timelineBeats == 64, "Timeline extension failed");
    action("undo");
    click("devices");
    click("instrument");
    check(project_.tracks[0].instrument == audio::Instrument::Sampler, "Sampler selection failed");
    action("undo");
    auto eqKnob =
        std::find_if(buttons_.begin(), buttons_.end(), [](const auto &b) { return b.action == "knob4"; });
    redraw();
    eqKnob =
        std::find_if(buttons_.begin(), buttons_.end(), [](const auto &b) { return b.action == "knob4"; });
    auto eqRect = eqKnob->rect;
    press(eqRect.x + 10, eqRect.y + 10, false);
    move(eqRect.x + 10, eqRect.y - 20);
    release();
    check(project_.tracks[0].strip.lowDb > 0, "EQ control failed");
    action("undo");
    click("lookahead");
    check(project_.tracks[0].strip.lookaheadFrames == 96, "Lookahead control failed");
    action("undo");
    click("audio-settings");
    click("input-settings");
    click("record-latency-auto");
    check(!project_.automaticRecordLatency, "Record compensation toggle failed");
    action("undo");
    click("record-offset:48");
    check(project_.recordOffsetFrames == 48, "Record offset failed");
    action("undo");
    click("audio-settings");
    click("bus-delay");
    check(project_.buses[0].delayWet > 0, "Bus effect failed");
    action("undo");
    click("arm0");
    check(project_.tracks[0].armed, "Record arm failed");
    action("undo");
    // MIDI delivery fixture exercises the actual Workspace document take transaction.
    action("record-midi");
    const auto timestamp = engine_.timestamp();
    midiMessage({timestamp, 0x90, 60, 100});
    midiMessage({timestamp + 12000, 0x80, 60, 0});
    finishMidi();
    check(project_.tracks[0].patterns.size() > baseline.tracks[0].patterns.size(),
          "Workspace MIDI take failed");
    action("undo");
    engine_.play(false);
    engine_.rewind();
    click("piano");
    action("track0");
    action("piano");
    redraw();
    click("patterns");
    click("duplicate-pattern");
    const auto duplicated = project_.tracks[0].activePattern;
    check(duplicated != 1 && project_.tracks[0].pattern().notes == baseline.tracks[0].pattern().notes,
          "Duplicate pattern failed");
    project_.tracks[0].pattern().notes[0].key = 61;
    check(project_.tracks[0].findPattern(1)->notes[0].key == 60, "Patterns share edited notes");
    project_.tracks[0].pattern().notes[0].key = 60;
    click("patterns");
    click("rename-pattern");
    while (!editText_.empty()) {
        editKey("backspace");
    }
    textInput("Chorus");
    editKey("enter");
    check(project_.tracks[0].pattern().name == "Chorus", "Rename pattern failed");
    click("undo");
    click("patterns");
    click("assign-pattern");
    check(project_.tracks[0].clips[0].patternId == duplicated, "Pattern assignment failed");
    click("undo");
    click("undo");
    click("patterns");
    click("new-pattern");
    check(project_.tracks[0].pattern().notes.empty(), "New pattern must be empty");
    click("undo");
    action("select-pattern:1");
    topKey_ = 72;
    redraw();
    const auto count = project_.tracks[0].pattern().notes.size();
    float bw = grid_.w / 4, row = grid_.h / 13;
    float nx = grid_.x + bw * 1.26, ny = grid_.y + row * 1.5;
    press(nx, ny, false);
    release();
    check(project_.tracks[0].pattern().notes.size() == count + 1, "Add note failed");
    redraw();
    press(nx, ny, false);
    move(nx + bw * .25, ny + row);
    release();
    check(project_.tracks[0].pattern().notes.back().beat == 1.5 &&
              project_.tracks[0].pattern().notes.back().key == 70,
          "Move note failed");
    redraw();
    auto n = project_.tracks[0].pattern().notes.back();
    nx = grid_.x + float(n.beat + n.length) * bw - 2;
    ny = grid_.y + row * 2.5;
    press(nx, ny, false);
    move(nx + bw * .25, ny);
    release();
    check(project_.tracks[0].pattern().notes.back().length == .5, "Resize note failed");
    redraw();
    press(grid_.x + bw * 1.5 + 3, velocity_.y + velocity_.h - 8, false);
    release();
    check(project_.tracks[0].pattern().notes.back().velocity < .2f, "Velocity edit failed");
    redraw();
    press(grid_.x + bw * 1.51, ny, true);
    release();
    check(project_.tracks[0].pattern().notes.size() == count, "Delete note failed");
    click("undo");
    check(project_.tracks[0].pattern().notes.size() == count + 1, "Undo failed");
    click("redo");
    check(project_.tracks[0].pattern().notes.size() == count, "Redo failed");
    click("tempo");
    textInput("137");
    editKey("enter");
    check(project_.bpm == 137, "Tempo input failed");
    click("undo");
    click("metronome");
    check(project_.metronome != baseline.metronome, "Metronome failed");
    click("undo");
    click("loop");
    check(project_.loop != baseline.loop, "Loop failed");
    click("undo");
    click("mute0");
    check(project_.tracks[0].muted, "Mute failed");
    click("undo");
    click("solo1");
    check(project_.compile(48000).gains[0] == 0, "Solo failed");
    click("undo");
    redraw();
    const auto clipCount = project_.tracks[0].clips.size();
    nx = lanes_.x + lanes_.w * 28 / 32;
    ny = lanes_.y + 35;
    press(nx, ny, false);
    release();
    check(project_.tracks[0].clips.size() == clipCount + 1, "Add clip failed");
    redraw();
    nx = lanes_.x + lanes_.w * 28.3f / 32;
    press(nx, ny, false);
    move(nx - lanes_.w * 2 / 32, ny);
    release();
    check(project_.tracks[0].clips.back().beat == 26, "Move clip failed");
    redraw();
    nx = lanes_.x + lanes_.w * 30 / 32 - 2;
    press(nx, ny, false);
    move(nx - lanes_.w / 32, ny);
    release();
    check(project_.tracks[0].clips.back().length == 3, "Resize clip failed");
    redraw();
    press(lanes_.x + lanes_.w * 26.3f / 32, ny, true);
    release();
    check(project_.tracks[0].clips.size() == clipCount, "Delete clip failed");
    click("mixer");
    redraw();
    Rect f = faders_[0];
    press(f.x + 10, f.y + f.h * .7f, false);
    move(f.x + 10, f.y + f.h * .6f);
    release();
    check(std::abs(project_.tracks[0].gain - .4f) < .01f, "Fader failed");
    click("undo");
    redraw();
    f = faders_[project_.tracks.size()];
    press(f.x + 10, f.y + f.h * .5f, false);
    release();
    check(std::abs(project_.masterGain - .5f) < .01f, "Master fader failed");
    click("undo");
    click("devices");
    redraw();
    auto it =
        std::find_if(buttons_.begin(), buttons_.end(), [](const Button &b) { return b.action == "knob0"; });
    Rect knob = it->rect;
    press(knob.x + 18, knob.y + 18, false);
    move(knob.x + 18, knob.y + 30);
    release();
    check(project_.tracks[0].cutoff < baseline.tracks[0].cutoff, "Device cutoff failed");
    click("undo");
    click("search");
    textInput("Bass");
    redraw();
    auto inBrowser = [&](const std::string &name) {
        return std::any_of(buttons_.begin(), buttons_.end(), [&](const Button &b) {
            return b.action == name && b.rect.x < lanes_.x - (width_ <= 1100 ? 148 : 174);
        });
    };
    check(inBrowser("track1") && !inBrowser("track0"), "Browser filtering failed");
    editKey("escape");
    search_.clear();
    click("addtrack");
    check(project_.tracks.size() == baseline.tracks.size() + 1, "Add track failed");
    click("undo");
    action("track0");
    click("piano");
    click("key60");
    check(auditionKey_ == -1, "Key release failed");
    const auto history = undo_.size();
    click("snap");
    check(snap_ == .5, "Snap failed");
    click("snap");
    click("fold");
    check(folded_, "Fold failed");
    click("fold");
    click("browser");
    check(!browser_, "Browser hide failed");
    click("browser");
    redraw();
    auto disabled = std::find_if(buttons_.begin(), buttons_.end(),
                                 [](const Button &b) { return b.action == "signature"; });
    Rect disabledRect = disabled->rect;
    const auto unmodified = project_;
    press(disabledRect.x + 10, disabledRect.y + 10, false);
    release();
    check(project_ == unmodified && undo_.size() == history, "Disabled/view control changed document");
    const auto testRoot =
        std::filesystem::temp_directory_path() /
        ("ally-ui-workflow-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(testRoot);
    const auto oldRequest = requestFile;
    const auto oldDocument = document_;
    try {
        media::Pcm sample{44100, 1, std::vector<float>(4410, .2f)};
        media::writeWav(testRoot / "import.wav", sample);
        requestFile = [&](const std::string &act) {
            fileSelected(act, testRoot / (act == "import"   ? "import.wav"
                                          : act == "export" ? "render.wav"
                                                            : "session.ally"));
        };
        auto waitFile = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            std::array<float, 512> scratch{};
            while (busy() && std::chrono::steady_clock::now() < deadline) {
                tick(0);
                engine_.process(scratch);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(!busy(), "File worker did not finish");
        };
        click("addtrack");
        click("import");
        waitFile();
        check(project_.tracks.back().clips.size() == 1 && project_.tracks.back().clips[0].audio,
              "UI WAV import failed");
        check(project_.sources.size() == baseline.sources.size() + 1, "UI source registration failed");
        click("source-loop");
        check(project_.tracks.back().clips[0].sourceLoop, "UI audio loop failed");
        click("source-offset");
        textInput("0.025");
        editKey("enter");
        check(project_.tracks.back().clips[0].sourceOffset == .025, "UI source trim failed");
        click("save");
        waitFile();
        check(!modified() && Project::load(testRoot / "session.ally") == project_, "UI save failed");
        click("export");
        waitFile();
        check(media::readWav(testRoot / "render.wav").frames() == project_.compile(48000).loopFrames,
              "UI offline export failed");
        click("load");
        waitFile();
        check(project_.tracks.back().clips.size() == 1, "UI open failed");
        click("audio-settings");
        check(audioMenu_, "Audio settings menu failed");
        click("buffer:512");
        check(audioConfig.bufferFrames == 512, "Audio buffer request failed");
        click("audio-settings");
        document_ = oldDocument;
        requestFile = oldRequest;
        std::filesystem::remove_all(testRoot);
    } catch (...) {
        document_ = oldDocument;
        requestFile = oldRequest;
        std::filesystem::remove_all(testRoot);
        throw;
    }
    project_ = baseline;
    editing_ = {};
    before_.reset();
    undo_.clear();
    redo_.clear();
    selected_ = selectedClip_ = 0;
    selectedNote_ = -1;
    topKey_ = 72;
    status = "Ready";
    mouseX_ = mouseY_ = -1;
    changed();
    redraw();
}
} // namespace ally::ui
