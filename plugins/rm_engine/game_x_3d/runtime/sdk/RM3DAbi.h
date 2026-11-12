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

#ifdef __cplusplus
}
#endif
