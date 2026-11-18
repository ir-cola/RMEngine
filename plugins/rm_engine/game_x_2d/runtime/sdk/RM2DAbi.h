/* RM Engine - the 2D parts of the scripts' API (ゲーム制作 × 2D gives them). Plain C, like RmAbi.h.
 * Scripts include RM2D.h, not this. Each part has its own version: change a part -> raise its version. */
#pragma once

#include <stdint.h>

#define RM_PART_TRANSFORM2D "rm_engine.game_x_2d.transform"
#define RM_PART_TRANSFORM2D_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* which: 0 = position (2 floats), 1 = rotation (1 float, degrees, counter-clockwise +), 2 = scale
   (2 floats). In the parent. Each returns 0 when the entity has no Transform 2D. */
typedef struct RmTransform2DApi {
    int (*get)(void* ctx, uint64_t entity, int which, float out[2]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[2]);
} RmTransform2DApi;

/* Version 2: the same, and the world (which: 0 position, 1 rotation, 2 scale - its parents included). A
   world value set changes the Transform 2D so the thing is there under its parents. */
#define RM_PART_TRANSFORM2D_VERSION2 2u
typedef struct RmTransform2DApi2 {
    int (*get)(void* ctx, uint64_t entity, int which, float out[2]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[2]);
    int (*worldGet)(void* ctx, uint64_t entity, int which, float out[2]);
    int (*worldSet)(void* ctx, uint64_t entity, int which, const float value[2]);
    /* Turns it so its +X (its right) points at `target` (world). */
    int (*lookAt)(void* ctx, uint64_t entity, const float target[2]);
} RmTransform2DApi2;

/* A sprite's animation (スプライトのアニメーション: rm::SpriteAnimation in RM2D.h). A step is a place in its
   コマの並び (0 = the first). Each does nothing on an entity without one (has: 0). */
#define RM_PART_SPRITE_ANIMATION "rm_engine.game_x_2d.sprite_animation"
#define RM_PART_SPRITE_ANIMATION_VERSION 1u

typedef struct RmSpriteAnimationApi {
    int (*has)(void* ctx, uint64_t entity);
    void (*play)(void* ctx, uint64_t entity, int fromStart); /* (paused / stopped: on again) */
    void (*pause)(void* ctx, uint64_t entity, int paused);
    void (*stop)(void* ctx, uint64_t entity); /* the first step, still */
    float (*speed)(void* ctx, uint64_t entity); /* 1 秒のコマ数 */
    void (*setSpeed)(void* ctx, uint64_t entity, float framesPerSecond);
    int (*loop)(void* ctx, uint64_t entity);
    void (*setLoop)(void* ctx, uint64_t entity, int loop);
    void (*setFrames)(void* ctx, uint64_t entity, const int32_t* cells, uint32_t count); /* コマの並び (from its start) */
    int32_t (*step)(void* ctx, uint64_t entity);
    void (*setStep)(void* ctx, uint64_t entity, int32_t step);
    int (*playing)(void* ctx, uint64_t entity); /* moving (not paused / stopped, not on the last of one that does not loop) */
} RmSpriteAnimationApi;

/* Debug drawing (rm::Debug in RM2D.h): lines in the plane to see what a script does, shown by the
   editor while it plays; an exported game does nothing with them. seconds <= 0: this frame only. */
#define RM_PART_DEBUG2D "rm_engine.game_x_2d.debug"
#define RM_PART_DEBUG2D_VERSION 1u

typedef struct RmDebug2DApi {
    void (*line)(void* ctx, const float a[2], const float b[2], const float color[4], float seconds);
    void (*rect)(void* ctx, const float center[2], const float size[2], const float color[4], float seconds);
    void (*circle)(void* ctx, const float center[2], float radius, const float color[4], float seconds);
} RmDebug2DApi;

#ifdef __cplusplus
}
#endif
