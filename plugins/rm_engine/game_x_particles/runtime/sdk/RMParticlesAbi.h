/* RM Engine - スクリプト API のパーティクル部分 (ゲーム制作 × パーティクル が提供する)。RmAbi.h と同じ
 * 素の C。スクリプトが include するのは RMParticles.h で、これではない。変えたらバージョンを上げる。 */
#pragma once

#include <stdint.h>

#define RM_PART_PARTICLES "rm_engine.game_x_particles.particles"
#define RM_PART_PARTICLES_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* エンティティのパーティクル (nth: そのうちどれか。0 = 最初のもの; RM_PARTICLES_ALL = すべて)。
   持たないエンティティに対しては何もしない。 */
#define RM_PARTICLES_ALL 0xFFFFFFFFu

typedef struct RmParticlesApi {
    uint32_t (*count)(void* ctx, uint64_t entity);
    /* またパーティクルを出す (fromStart: 出ているものは消え、時計は 0 から)。 */
    void (*play)(void* ctx, uint64_t entity, uint32_t nth, int fromStart);
    /* 新しく出さない: 出ているものは生き続けて消えていく (clear: すぐ消す)。 */
    void (*stop)(void* ctx, uint64_t entity, uint32_t nth, int clear);
    void (*pause)(void* ctx, uint64_t entity, uint32_t nth, int paused);
    /* 各エミッターから `count` 個を一度に出す (止まっていてもいなくても)。 */
    void (*emit)(void* ctx, uint64_t entity, uint32_t nth, uint32_t count);
    int (*playing)(void* ctx, uint64_t entity, uint32_t nth); /* パーティクルを出す (止まっていない時) */
    float (*speed)(void* ctx, uint64_t entity, uint32_t nth);
    void (*setSpeed)(void* ctx, uint64_t entity, uint32_t nth, float speed);
} RmParticlesApi;

#ifdef __cplusplus
}
#endif
