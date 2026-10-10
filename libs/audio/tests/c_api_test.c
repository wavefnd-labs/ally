#include <ally/audio/c_api.h>
#include <math.h>
int main(void) {
    ally_audio_engine *e = ally_audio_create(48000);
    if (!e || ally_audio_create(0)) {
        return 1;
    }
    float b[512] = {0};
    if (!ally_audio_note(e, 69, 100)) {
        return 2;
    }
    ally_audio_process(e, b, 256);
    float energy = 0;
    for (int i = 0; i < 512; ++i) {
        energy += fabsf(b[i]);
    }
    ally_audio_destroy(e);
    return energy > 0 ? 0 : 3;
}
