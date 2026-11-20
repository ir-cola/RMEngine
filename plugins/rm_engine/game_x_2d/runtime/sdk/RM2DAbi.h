/* RM Engine - スクリプト API の 2D の部分 (ゲーム制作 × 2D が提供する)。RmAbi.h と同じ素の C。
 * スクリプトはこれでなく RM2D.h を include する。部分ごとに版があり、部分を変えたら版を上げる。 */
#pragma once

#include <stdint.h>

#define RM_PART_TRANSFORM2D "rm_engine.game_x_2d.transform"
#define RM_PART_TRANSFORM2D_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* which: 0 = 位置 (float 2 個)、1 = 回転 (float 1 個、度、反時計回りが +)、2 = 拡大縮小
   (float 2 個)。親の中での値。エンティティに Transform 2D がなければどれも 0 を返す。 */
typedef struct RmTransform2DApi {
    int (*get)(void* ctx, uint64_t entity, int which, float out[2]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[2]);
} RmTransform2DApi;

/* 版 2: 同じものと、ワールド (which: 0 位置、1 回転、2 拡大縮小 - 親を含む)。ワールドの
   値を設定すると、親の下でその物がそこに来るように Transform 2D を変える。 */
#define RM_PART_TRANSFORM2D_VERSION2 2u
typedef struct RmTransform2DApi2 {
    int (*get)(void* ctx, uint64_t entity, int which, float out[2]);
    int (*set)(void* ctx, uint64_t entity, int which, const float value[2]);
    int (*worldGet)(void* ctx, uint64_t entity, int which, float out[2]);
    int (*worldSet)(void* ctx, uint64_t entity, int which, const float value[2]);
    /* +X (右) が `target` (ワールド) を向くように回す。 */
    int (*lookAt)(void* ctx, uint64_t entity, const float target[2]);
} RmTransform2DApi2;

/* スプライトのアニメーション (スプライトのアニメーション: RM2D.h の rm::SpriteAnimation)。ステップは
   コマの並び での位置 (0 = 最初)。持たないエンティティでは、どれも何もしない (has: 0)。 */
#define RM_PART_SPRITE_ANIMATION "rm_engine.game_x_2d.sprite_animation"
#define RM_PART_SPRITE_ANIMATION_VERSION 1u

typedef struct RmSpriteAnimationApi {
    int (*has)(void* ctx, uint64_t entity);
    void (*play)(void* ctx, uint64_t entity, int fromStart); /* (一時停止 / 停止していたら、また動く) */
    void (*pause)(void* ctx, uint64_t entity, int paused);
    void (*stop)(void* ctx, uint64_t entity); /* 最初のステップで、止まったまま */
    float (*speed)(void* ctx, uint64_t entity); /* 1 秒のコマ数 */
    void (*setSpeed)(void* ctx, uint64_t entity, float framesPerSecond);
    int (*loop)(void* ctx, uint64_t entity);
    void (*setLoop)(void* ctx, uint64_t entity, int loop);
    void (*setFrames)(void* ctx, uint64_t entity, const int32_t* cells, uint32_t count); /* コマの並び (その始めから) */
    int32_t (*step)(void* ctx, uint64_t entity);
    void (*setStep)(void* ctx, uint64_t entity, int32_t step);
    int (*playing)(void* ctx, uint64_t entity); /* 動いている (一時停止も停止もしておらず、ループしないものの最後でもない) */
} RmSpriteAnimationApi;

/* デバッグ描画 (RM2D.h の rm::Debug): スクリプトが何をしているか見るための平面上の線。Play 中は
   エディターが出す。書き出したゲームは何もしない。seconds <= 0: このフレームだけ。 */
#define RM_PART_DEBUG2D "rm_engine.game_x_2d.debug"
#define RM_PART_DEBUG2D_VERSION 1u

typedef struct RmDebug2DApi {
    void (*line)(void* ctx, const float a[2], const float b[2], const float color[4], float seconds);
    void (*rect)(void* ctx, const float center[2], const float size[2], const float color[4], float seconds);
    void (*circle)(void* ctx, const float center[2], float radius, const float color[4], float seconds);
} RmDebug2DApi;

#ifdef __cplusplus
}
#endif
