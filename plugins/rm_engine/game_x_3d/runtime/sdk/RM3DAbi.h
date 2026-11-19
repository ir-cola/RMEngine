/* RM Engine - the 3D parts of the scripts' API (ゲーム制作 × 3D gives them). Plain C, like RmAbi.h.
 * Scripts include RM3D.h, not this. Each part has its own version: change a part -> raise its version. */
#pragma once

#include <stdint.h>

#define RM_PART_TRANSFORM3D "rm_engine.game_x_3d.transform"
#define RM_PART_TRANSFORM3D_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* which: 0 = position, 1 = rotation (degrees; X -> Y -> Z), 2 = scale. In the parent (as the
   inspector's ローカル shows them). Each returns 0 when the entity has no Transform. */
typedef struct RmTransform3DApi {
    int (*get)(void* ctx, uint64_t entity, int which, float out[3]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[3]);
    /* Where it is in the world (its parents' Transforms included). */
    int (*worldPosition)(void* ctx, uint64_t entity, float out[3]);
} RmTransform3DApi;

/* Version 2: the same, and the world. A world value set changes the Transform so the thing is there in
   the world under its parents (as the inspector's ワールド does). */
#define RM_PART_TRANSFORM3D_VERSION2 2u
typedef struct RmTransform3DApi2 {
    int (*get)(void* ctx, uint64_t entity, int which, float out[3]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[3]);
    int (*worldPosition)(void* ctx, uint64_t entity, float out[3]);
    /* The world matrix: column-major 4x4 (out[col * 4 + row]; the position is out[12..14]). */
    int (*worldMatrix)(void* ctx, uint64_t entity, float out[16]);
    int (*setWorldPosition)(void* ctx, uint64_t entity, const float position[3]);
    int (*worldRotation)(void* ctx, uint64_t entity, float outDegrees[3]);
    int (*setWorldRotation)(void* ctx, uint64_t entity, const float degrees[3]);
    /* Turns it so its -Z (where a camera looks) points at `target` (world), its Y as near `up` as it can be. */
    int (*lookAt)(void* ctx, uint64_t entity, const float target[3], const float up[3]);
} RmTransform3DApi2;

/* Debug drawing (rm::Debug in RM3D.h): lines to see what a script does, shown by the editor while it
   plays; an exported game does nothing with them. seconds <= 0: this frame only. color: r g b a. */
#define RM_PART_DEBUG3D "rm_engine.game_x_3d.debug"
#define RM_PART_DEBUG3D_VERSION 1u

typedef struct RmDebug3DApi {
    void (*line)(void* ctx, const float a[3], const float b[3], const float color[4], float seconds);
    void (*box)(void* ctx, const float center[3], const float size[3], const float color[4], float seconds);
    void (*sphere)(void* ctx, const float center[3], float radius, const float color[4], float seconds);
    void (*ray)(void* ctx, const float origin[3], const float direction[3], float length, const float color[4], float seconds);
} RmDebug3DApi;

/* The scene's 空 (地球の空): the weather and the wind, at once (seconds <= 0) or faded over `seconds`
   (from the weather of that moment: a fade begun during another starts from where that one has come).
   weather: 0 晴れ, 1 くもり, 2 雨, 3 雪.
   which: 0 雲の量 of the weather (晴れ: its own), 1 雨の量, 2 雪の量, 3 風の向き (degrees), 4 風の強さ (m/s),
          5 時刻 (0 .. 24; never faded), 6 雷 (0 / 1; never faded),
          and only to read (now = 1): 7 濡れ (0 .. 1).
   get: now = 0 the value set (where a fade goes); now = 1 the value this moment (雨の量 / 雪の量: what
   falls - nothing while the weather is not rain / snow).
   Each returns 0 when the scene has no sky (or `which` is not known). */
#define RM_PART_SKY3D "rm_engine.game_x_3d.sky"
#define RM_PART_SKY3D_VERSION 1u

typedef struct RmSky3DApi {
    int (*setWeather)(void* ctx, int weather, float seconds);
    int (*weather)(void* ctx); /* the weather set; -1: no sky */
    int (*set)(void* ctx, int which, float value, float seconds);
    int (*get)(void* ctx, int which, int now, float* out);
    /* 0 .. 1: how far the fade has come (1: none going on). */
    float (*fading)(void* ctx);
} RmSky3DApi;

#ifdef __cplusplus
}
#endif
