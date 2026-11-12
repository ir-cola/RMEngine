/* RM Engine - the 3D part of the scripts' API (ゲーム制作 × 3D gives it). Plain C, like RmAbi.h.
 * Scripts include RM3D.h, not this. Change anything here -> raise RM_PART_TRANSFORM3D_VERSION. */
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

#ifdef __cplusplus
}
#endif
