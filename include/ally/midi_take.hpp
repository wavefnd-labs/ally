#pragma once
#include <ally/midi.hpp>
#include <ally/project.hpp>
namespace ally::midi {
// Main-thread document recorder. Absolute engine timestamps survive transport wrapping.
class Take {
  public:
    void begin(uint64_t clock, double startBeat, double framesPerBeat);
    void message(const Message &);
    void finish(uint64_t clock);
    void apply(Project &, size_t track) const;
    bool active() const {
        return active_;
    }

  private:
    struct Held {
        uint64_t at = 0;
        uint8_t velocity = 0;
    };
    std::array<std::array<Held, 128>, 16> held_{};
    std::vector<Note> notes_;
    uint64_t first_ = 0;
    double start_ = 0, fpb_ = 24000;
    bool active_ = false;
};
} // namespace ally::midi
