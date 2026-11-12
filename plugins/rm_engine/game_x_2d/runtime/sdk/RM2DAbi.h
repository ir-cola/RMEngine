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
