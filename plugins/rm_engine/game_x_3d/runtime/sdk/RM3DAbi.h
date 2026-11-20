/* RM Engine - スクリプトの API の 3D の部分 (ゲーム制作 × 3D が与える)。RmAbi.h と同じ素の C。
 * スクリプトが include するのは RM3D.h で、これではない。部分ごとに版を持つ: 部分を変えたら版を上げる。 */
#pragma once

#include <stdint.h>

#define RM_PART_TRANSFORM3D "rm_engine.game_x_3d.transform"
#define RM_PART_TRANSFORM3D_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* which: 0 = 位置, 1 = 回転 (度。X -> Y -> Z), 2 = スケール。親の中での値 (インスペクターの
   ローカル が見せるもの)。どれもエンティティに Transform が無ければ 0 を返す。 */
typedef struct RmTransform3DApi {
    int (*get)(void* ctx, uint64_t entity, int which, float out[3]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[3]);
    /* ワールドでの位置 (親の Transform も含める)。 */
    int (*worldPosition)(void* ctx, uint64_t entity, float out[3]);
} RmTransform3DApi;

/* 版 2: 同じものと、ワールド。ワールドの値を設定すると、親の下でその物がワールドのそこに来るように
   Transform を変える (インスペクターの ワールド と同じ)。 */
#define RM_PART_TRANSFORM3D_VERSION2 2u
typedef struct RmTransform3DApi2 {
    int (*get)(void* ctx, uint64_t entity, int which, float out[3]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[3]);
    int (*worldPosition)(void* ctx, uint64_t entity, float out[3]);
    /* ワールド行列: 列優先の 4x4 (out[col * 4 + row]。位置は out[12..14])。 */
    int (*worldMatrix)(void* ctx, uint64_t entity, float out[16]);
    int (*setWorldPosition)(void* ctx, uint64_t entity, const float position[3]);
    int (*worldRotation)(void* ctx, uint64_t entity, float outDegrees[3]);
    int (*setWorldRotation)(void* ctx, uint64_t entity, const float degrees[3]);
    /* その -Z (カメラが見る向き) が `target` (ワールド) を向くように回し、Y をできるだけ `up` に近づける。 */
    int (*lookAt)(void* ctx, uint64_t entity, const float target[3], const float up[3]);
} RmTransform3DApi2;

/* デバッグ描画 (RM3D.h の rm::Debug): スクリプトが何をしているか見るための線。動いている間は
   エディターが出し、書き出したゲームでは何もしない。seconds <= 0: このフレームだけ。color: r g b a。 */
#define RM_PART_DEBUG3D "rm_engine.game_x_3d.debug"
#define RM_PART_DEBUG3D_VERSION 1u

typedef struct RmDebug3DApi {
    void (*line)(void* ctx, const float a[3], const float b[3], const float color[4], float seconds);
    void (*box)(void* ctx, const float center[3], const float size[3], const float color[4], float seconds);
    void (*sphere)(void* ctx, const float center[3], float radius, const float color[4], float seconds);
    void (*ray)(void* ctx, const float origin[3], const float direction[3], float length, const float color[4], float seconds);
} RmDebug3DApi;

/* シーンの 空 (地球の空): その 時刻 (0 .. 24)。シーンに空が無ければどれも 0 を返す。(天候も持っていた
   版 1 は無くなった: 天候はプラグイン 天候のもの - RMWeather.h。) */
#define RM_PART_SKY3D "rm_engine.game_x_3d.sky"
#define RM_PART_SKY3D_VERSION2 2u

typedef struct RmSky3DApi2 {
    int (*setHour)(void* ctx, float hour);
    int (*hour)(void* ctx, float* out);
} RmSky3DApi2;

#ifdef __cplusplus
}
#endif
