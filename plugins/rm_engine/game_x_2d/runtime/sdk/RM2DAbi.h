/* RM Engine - the 2D part of the scripts' API (ゲーム制作 × 2D gives it). Plain C, like RmAbi.h.
 * Scripts include RM2D.h, not this. Change anything here -> raise RM_PART_TRANSFORM2D_VERSION. */
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

#ifdef __cplusplus
}
#endif
