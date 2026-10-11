#pragma once
#include <ally/audio/backend.hpp>
#include <ally/document.hpp>
#include <ally/project.hpp>
#include <functional>
#include <future>
#include <optional>
#include <span>
#include <string>
class SkCanvas;
namespace ally::ui {
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};
class Workspace {
  public:
    Workspace(Project &, audio::Engine &, std::span<const float> texture = {});
    void draw(SkCanvas &, float width, float height);
    void press(float x, float y, bool right);
    void move(float x, float y);
    void release();
    void wheel(float dy, bool pitch = false, bool pan = false);
    void action(const std::string &);
    void textInput(const std::string &);
    void editKey(const std::string &);
    bool textEditing() const {
        return focus_ != Focus::None;
    }
    bool sync();
    void verifyInteractions(SkCanvas &, float, float);
    void fileSelected(const std::string &action, const std::filesystem::path &path);
    void tick(double seconds);
    bool busy() const {
        return job_.valid() || dialogPending_;
    }
    bool modified() const {
        return document_.dirty(project_);
    }
    void setDocument(const std::filesystem::path &path, const std::filesystem::path &recovery,
                     bool recovered = false);
    void discardRecovery() {
        document_.discardRecovery();
    }
    std::string title() const;
    std::function<void(const std::string &)> requestFile;
    std::function<void(const audio::BackendConfig &)> requestAudio;
    std::vector<audio::Device> audioDevices;
    audio::BackendConfig audioConfig;
    audio::BackendStats audioStats;
    std::string status = "Ready", backendStatus = "Offline", midiStatus;
    int selectedTrack() const {
        return selected_;
    }
    bool pianoVisible() const {
        return detail_ == Detail::Piano;
    }

  private:
    enum class Detail { Piano, Devices, Mixer };
    enum class Focus { None, Search, Tempo, PatternName, SourceOffset };
    struct Button {
        Rect rect;
        std::string action, tooltip;
        bool enabled;
    };
    struct ClipHit {
        Rect rect;
        size_t track, clip;
    };
    void tool(SkCanvas &, Rect, const std::string &, const std::string &, bool active = false,
              bool enabled = true, const std::string &tooltip = "");
    void browser(SkCanvas &, Rect);
    void arrangement(SkCanvas &, Rect, double beat);
    void piano(SkCanvas &, Rect, double beat);
    void devices(SkCanvas &, Rect);
    void mixer(SkCanvas &, Rect);
    void changed();
    void remember(const Project &);
    void commitEdit();
    void menu(SkCanvas &);
    void audioClip(SkCanvas &, Rect);
    struct FileResult {
        std::string action;
        std::filesystem::path path;
        Project project;
    };
    Project &project_;
    DocumentSession document_;
    audio::Engine &engine_;
    std::span<const float> texture_;
    std::vector<Button> buttons_;
    std::vector<ClipHit> clips_;
    std::vector<Project> undo_, redo_;
    std::optional<Project> before_;
    Rect hitClip_{}, grid_{}, velocity_{}, lanes_{}, divider_{}, tempo_{}, searchBox_{}, menuRect_{};
    std::array<Rect, audio::Plan::maxTracks + 1> faders_{};
    Detail detail_ = Detail::Piano;
    Focus focus_ = Focus::None;
    std::string search_, tempoText_;
    std::string editText_;
    std::future<FileResult> job_;
    bool dialogPending_ = false, patternMenu_ = false, audioMenu_ = false;
    int patternPage_ = 0, devicePage_ = 0;
    uint32_t relinkId_ = 0;
    bool browser_ = true, folded_ = false, erase_ = false, dirty_ = true;
    bool dividerDrag_ = false, noteResize_ = false, clipResize_ = false;
    int selected_ = 0, selectedClip_ = 0, selectedNote_ = -1;
    int noteDrag_ = -1, clipDrag_ = -1, gainDrag_ = -1, knobDrag_ = -1;
    int auditionKey_ = -1, auditionTrack_ = 0, releaseKey_ = -1, releaseTrack_ = 0;
    int topKey_ = 72;
    float width_ = 1440, height_ = 900, detailHeight_ = 310, trackScroll_ = 0, mixerScroll_ = 0;
    float mouseX_ = -1, mouseY_ = -1, dragX_ = 0, dragY_ = 0, knobOriginal_ = 0;
    double snap_ = .25, zoom_ = 1, scrollBeat_ = 0;
    Note originalNote_{};
    Clip originalClip_{};
};
} // namespace ally::ui
