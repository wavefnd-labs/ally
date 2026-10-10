#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ally_audio_engine ally_audio_engine;
ally_audio_engine *ally_audio_create(uint32_t sample_rate);
void ally_audio_destroy(ally_audio_engine *engine);
void ally_audio_play(ally_audio_engine *engine, int playing);
int ally_audio_note(ally_audio_engine *engine, uint8_t key, uint8_t velocity);
void ally_audio_process(ally_audio_engine *engine, float *stereo, size_t frames);
#ifdef __cplusplus
}
#endif
