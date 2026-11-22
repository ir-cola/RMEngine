/* RM Engine - スクリプトの API の気候の部分 (プラグイン 気候 が与える)。RmAbi.h と同じ素の C。
 * スクリプトが include するのは RMClimate.h で、これではない。部分を変えたら版を上げる。 */
#pragma once

#include <stdint.h>

/* 場所の気候: at (3D。flat = 0) / 2D (flat = 1。pos[2] は使わない) の場所の気温 (℃) と湿り気 (%)。
   気候エリアと、3D では高さで冷える分を含む。シーンに 気候 の設定が無くても既定の値 (15 ℃・50 %) で答える。
   set / get の which: 0 気温 (℃), 1 湿り気 (%), 2 高さで冷える (℃ / 100 m), 3 基準の高さ (m) -
   シーンの設定 (気候エリアの外の気候) を、すぐに。
   版 1 (2026-11-22)。 */
#define RM_PART_CLIMATE "rm_engine.climate.climate"
#define RM_PART_CLIMATE_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmClimateApi {
    int (*at)(void* ctx, const float pos[3], int flat, float* temperature, float* humidity);
    int (*set)(void* ctx, int which, float value);
    int (*get)(void* ctx, int which, float* out);
} RmClimateApi;

#ifdef __cplusplus
}
#endif
