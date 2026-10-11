#pragma once
#include <ally/project.hpp>
#include <optional>
namespace ally {
struct ClipRef {
    size_t track = 0, clip = 0;
    bool operator==(const ClipRef &) const = default;
};
class Editing {
  public:
    bool notes = true;
    std::vector<size_t> selectedNotes;
    std::vector<ClipRef> selectedClips;
    void clear();
    void selectNote(size_t, bool additive);
    void selectClip(ClipRef, bool additive);
    bool contains(size_t) const;
    bool contains(ClipRef) const;
    void copy(const Project &, size_t track);
    void paste(Project &, size_t track, double beat);
    void erase(Project &, size_t track);
    void transform(Project &, size_t track, double beatDelta, int pitchDelta, bool resize) const;
    void duplicate(Project &, size_t track);

  private:
    struct CopiedClip {
        Clip clip;
        size_t track = 0;
        Pattern pattern;
        std::optional<AudioSource> source;
    };
    std::vector<Note> noteClipboard_;
    std::vector<CopiedClip> clipClipboard_;
    bool clipboardNotes_ = true;
};
} // namespace ally
