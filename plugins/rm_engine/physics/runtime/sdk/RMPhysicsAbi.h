/* RM Engine - スクリプトの API の物理の部分 (プラグイン 物理 が提供する)。RmAbi.h と同じ素の C。
 * スクリプトは RMPhysics.h を include する。これは直接 include しない。この部分は自分のバージョンを持つ:
 * 変えたらそのバージョンを上げる (RM_ABI_VERSION は変えない: OnHit / OnTriggerEnter / OnTriggerExit のフックは RmAbi.h にある)。 */
#pragma once

#include <stdint.h>

#define RM_PART_PHYSICS "rm_engine.physics.physics"
#define RM_PART_PHYSICS_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmRayHit {
    uint64_t entity; /* 当たったもの (0: なし) */
    float point[3];
    float normal[3];
    float distance;
} RmRayHit;

/* ベクトルはワールドの float 3 つ (2D のゲーム: z = 0)。エンティティに体がない (コライダーがない、切ってある)
   か、呼び出しが合わない (動かないものに力をかける) ときは、どれも 0 を返す。 */
typedef struct RmPhysicsApi {
    int (*addForce)(void* ctx, uint64_t entity, const float force[3], int impulse); /* impulse: 1 = すぐ (N·s)、0 = 次のステップの間 (N) */
    int (*getVelocity)(void* ctx, uint64_t entity, float out[3]);
    int (*setVelocity)(void* ctx, uint64_t entity, const float velocity[3]);
    int (*raycast)(void* ctx, const float origin[3], const float direction[3], float maxDistance, RmRayHit* hit); /* 1: 当たった */
    /* キャラクター */
    int (*characterMove)(void* ctx, uint64_t entity, const float velocity[3]); /* 歩く速さ (水平の部分)。変えるまで続く */
    int (*characterJump)(void* ctx, uint64_t entity, float speed);              /* 地面にいるとき (次のステップで) */
    int (*characterGrounded)(void* ctx, uint64_t entity);
    int (*characterGroundNormal)(void* ctx, uint64_t entity, float out[3]);    /* 0: 何にも立っていない */
} RmPhysicsApi;

#ifdef __cplusplus
}
#endif
