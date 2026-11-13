/* RM Engine - the physics part of the scripts' API (the plugin 物理 gives it). Plain C, like RmAbi.h.
 * Scripts include RMPhysics.h, not this. The part has its own version: change it -> raise its version
 * (RM_ABI_VERSION stays: the hooks OnHit / OnTriggerEnter / OnTriggerExit are in RmAbi.h). */
#pragma once

#include <stdint.h>

#define RM_PART_PHYSICS "rm_engine.physics.physics"
#define RM_PART_PHYSICS_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmRayHit {
    uint64_t entity; /* what was hit (0: nothing) */
    float point[3];
    float normal[3];
    float distance;
} RmRayHit;

/* Vectors are 3 floats in the world (a 2D game: z = 0). Each returns 0 when the entity has no body
   (no collider, switched off) or the call does not fit it (a force on something that does not move). */
typedef struct RmPhysicsApi {
    int (*addForce)(void* ctx, uint64_t entity, const float force[3], int impulse); /* impulse: 1 = at once (N·s), 0 = over the next step (N) */
    int (*getVelocity)(void* ctx, uint64_t entity, float out[3]);
    int (*setVelocity)(void* ctx, uint64_t entity, const float velocity[3]);
    int (*raycast)(void* ctx, const float origin[3], const float direction[3], float maxDistance, RmRayHit* hit); /* 1: hit */
    /* キャラクター */
    int (*characterMove)(void* ctx, uint64_t entity, const float velocity[3]); /* the walk (its horizontal part), kept until changed */
    int (*characterJump)(void* ctx, uint64_t entity, float speed);              /* when on the ground (at the next step) */
    int (*characterGrounded)(void* ctx, uint64_t entity);
    int (*characterGroundNormal)(void* ctx, uint64_t entity, float out[3]);    /* 0: not standing on anything */
} RmPhysicsApi;

#ifdef __cplusplus
}
#endif
