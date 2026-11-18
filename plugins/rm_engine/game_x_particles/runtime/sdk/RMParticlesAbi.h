/* RM Engine - the particles' part of the scripts' API (ゲーム制作 × パーティクル gives it). Plain C, like
 * RmAbi.h. Scripts include RMParticles.h, not this. Change it -> raise its version. */
#pragma once

#include <stdint.h>

#define RM_PART_PARTICLES "rm_engine.game_x_particles.particles"
#define RM_PART_PARTICLES_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* An entity's パーティクル (nth: which of them, 0 = the first; RM_PARTICLES_ALL = every one). Each does nothing
   on an entity without one. */
#define RM_PARTICLES_ALL 0xFFFFFFFFu

typedef struct RmParticlesApi {
    uint32_t (*count)(void* ctx, uint64_t entity);
    /* Letting out particles again (fromStart: what is out goes, the clocks from 0). */
    void (*play)(void* ctx, uint64_t entity, uint32_t nth, int fromStart);
    /* No new particles: what is out lives on and dies away (clear: it goes at once). */
    void (*stop)(void* ctx, uint64_t entity, uint32_t nth, int clear);
    void (*pause)(void* ctx, uint64_t entity, uint32_t nth, int paused);
    /* `count` particles of each emitter at once (stopped or not). */
    void (*emit)(void* ctx, uint64_t entity, uint32_t nth, uint32_t count);
    int (*playing)(void* ctx, uint64_t entity, uint32_t nth); /* letting out particles (not stopped) */
    float (*speed)(void* ctx, uint64_t entity, uint32_t nth);
    void (*setSpeed)(void* ctx, uint64_t entity, uint32_t nth, float speed);
} RmParticlesApi;

#ifdef __cplusplus
}
#endif
