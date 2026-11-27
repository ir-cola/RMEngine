/* RM Engine - スクリプトの API のナビゲーションの部分 (つなぎ ゲーム制作 × ナビゲーション が提供する)。RmAbi.h と同じ素の C。
 * スクリプトは RMNav.h を include する。これは直接 include しない。この部分は自分のバージョンを持つ:
 * 変えたらそのバージョンを上げる (RM_ABI_VERSION は変えない)。 */
#pragma once

#include <stdint.h>

#define RM_PART_NAV "rm_engine.game_x_navigation.nav"
#define RM_PART_NAV_VERSION 1u
#define RM_PART_NAV_VERSION2 2u /* 版 2: 1 の後ろに、つなぎの線と扉 (RmNavApi2) */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmNavAgentState {
    int onMesh;       /* ナビメッシュの上にいる */
    int hasPath;      /* 行き先へ向かっている */
    int pending;      /* まだ道を探している */
    int partial;      /* 行き先まで行けない (一番近い所へ) */
    int arrived;      /* 最後の行き先に着いた */
    float remaining;  /* 通路に沿った残りの距離 (m) */
    float velocity[3];
    float desired[3];
} RmNavAgentState;

/* 点はワールドの float 3 つ。エンティティに「ナビの動く者」が無い・ナビメッシュを焼いていない (読む前: 最初の刻みの前) ときは、
   どれも 0 を返す。 */
typedef struct RmNavApi {
    int (*setDestination)(void* ctx, uint64_t entity, const float target[3]);
    int (*stop)(void* ctx, uint64_t entity);
    int (*resume)(void* ctx, uint64_t entity);
    int (*setSpeed)(void* ctx, uint64_t entity, float speed);
    int (*warp)(void* ctx, uint64_t entity, const float position[3]);
    int (*state)(void* ctx, uint64_t entity, RmNavAgentState* out);
    /* 道だけ (動く者なし)。out に点を多くて max 個 (x, y, z ずつ)。返す: 1 着く / 2 一番近い所まで / 0 道が無い */
    int (*findPath)(void* ctx, const float a[3], const float b[3], float* out, int max, int* count);
    int (*raycast)(void* ctx, const float a[3], const float b[3]); /* 1: まっすぐ歩ける */
    int (*closestPoint)(void* ctx, const float p[3], float out[3]);
    int (*randomPoint)(void* ctx, const float center[3], float radius, float out[3]);
    int (*setAreaCost)(void* ctx, const char* name, float cost);
    int (*setAreaOpen)(void* ctx, const char* name, int open);
} RmNavApi;

/* 版 2 (段階 4): 版 1 の表をそのまま先頭に持ち、後ろに足す。 */
typedef struct RmNavApi2 {
    RmNavApi v1;
    int (*completeLink)(void* ctx, uint64_t entity);                     /* 「スクリプトにまかせる」線を渡り終えた */
    int (*setLinkEnabled)(void* ctx, uint64_t linkEntity, int enabled);  /* 部品「つなぎの線」の「使える」 */
    int (*setAreaClosed)(void* ctx, uint64_t areaEntity, int closed);    /* 部品「ナビのエリア」の「閉じている」(扉) */
} RmNavApi2;

#ifdef __cplusplus
}
#endif
