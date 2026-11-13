/* RM Engine - the sound part of the scripts' API (ゲーム制作 × 音 gives it). Plain C, like RmAbi.h.
 * Scripts include RMAudio.h, not this. Change it -> raise RM_PART_AUDIO_VERSION. */
#pragma once

#include <stdint.h>

#define RM_PART_AUDIO "rm_engine.game_x_audio.audio"
#define RM_PART_AUDIO_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* guid: a sound asset's id (rm::SoundRef::guid). A voice: one sound playing (0: none - the sound is not
   there, or the game has no sound). bus: 0 = 全体, 1 = BGM, 2 = 効果音. */
typedef struct RmAudioApi {
    /* An effect, once. position null: heard the same everywhere; else from there (smaller farther away). */
    uint64_t (*play)(void* ctx, const char* guid, float volume, float pitch, const float* position);
    void (*stop)(void* ctx, uint64_t voice, float fadeSeconds);
    int (*playing)(void* ctx, uint64_t voice);
    /* The game's music (one at a time, looped): the one before fades out while this fades in. The same
       sound already playing as the music goes on as it is. */
    void (*playMusic)(void* ctx, const char* guid, float volume, float fadeSeconds);
    void (*stopMusic)(void* ctx, float fadeSeconds);
    void (*setVolume)(void* ctx, int bus, float volume);
    float (*volume)(void* ctx, int bus);
    /* The entity's (first) 音源. op: 0 play (from the start), 1 stop, 2 pause, 3 resume. 0: it has none. */
    int (*source)(void* ctx, uint64_t entity, int op);
    /* what: 0 = 音量, 1 = ピッチ. 0: it has none. */
    int (*sourceSet)(void* ctx, uint64_t entity, int what, float value);
    int (*sourcePlaying)(void* ctx, uint64_t entity);
} RmAudioApi;

#ifdef __cplusplus
}
#endif
