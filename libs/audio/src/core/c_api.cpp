#include <ally/audio/c_api.h>
#include <ally/audio/engine.hpp>
#include <limits>
struct ally_audio_engine {
    ally::audio::Engine value;
    explicit ally_audio_engine(uint32_t rate) : value(rate) {}
};
extern "C" {
ally_audio_engine *ally_audio_create(uint32_t rate) {
    try {
        return new ally_audio_engine(rate);
    } catch (...) {
        return nullptr;
    }
}

void ally_audio_destroy(ally_audio_engine *e) {
    delete e;
}

void ally_audio_play(ally_audio_engine *e, int p) {
    if (e) {
        e->value.play(p != 0);
    }
}

int ally_audio_note(ally_audio_engine *e, uint8_t k, uint8_t v) {
    return e && e->value.midi({k, v});
}

void ally_audio_process(ally_audio_engine *e, float *b, size_t frames) {
    if (e && b && frames <= std::numeric_limits<size_t>::max() / 2) {
        e->value.process({b, frames * 2});
    }
}
}
