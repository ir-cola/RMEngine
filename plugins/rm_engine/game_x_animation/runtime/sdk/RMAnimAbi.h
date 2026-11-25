/* RM Engine - スクリプト API のアニメーション部分 (ゲーム制作 × アニメーション が提供する)。RmAbi.h と同じ
 * 素の C。スクリプトが include するのは RMAnim.h で、これではない。変えたらバージョンを上げる。 */
#pragma once

#include <stdint.h>

#define RM_PART_ANIM "rm_engine.game_x_animation.animator"
#define RM_PART_ANIM_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

/* エンティティのアニメーター (部品「アニメーター」)。持たないエンティティに対しては何もしない (0 を返す)。
   文字はすべて UTF-8。 */
typedef struct RmAnimApi {
    int (*has)(void* ctx, uint64_t entity);
    /* クリップ `clip` (モデルのクリップの名前) を始めから流す。`fade` 秒かけて今の姿勢から移る。
       モデルにそのクリップが無いと 0 (休みの姿勢になる)。 */
    int (*playClip)(void* ctx, uint64_t entity, const char* clip, float fade);
    int (*hasClip)(void* ctx, uint64_t entity, const char* clip);
    /* 今のクリップの名前 (部品の欄の値)。長さを返す (cap を超えたら書かない分もある)。 */
    uint32_t (*clip)(void* ctx, uint64_t entity, char* buf, uint32_t cap);
    void (*setPaused)(void* ctx, uint64_t entity, int paused);
    int (*paused)(void* ctx, uint64_t entity);
    float (*speed)(void* ctx, uint64_t entity);
    void (*setSpeed)(void* ctx, uint64_t entity, float speed);
    int (*loop)(void* ctx, uint64_t entity);
    void (*setLoop)(void* ctx, uint64_t entity, int loop);
    float (*time)(void* ctx, uint64_t entity);           /* 秒 */
    void (*setTime)(void* ctx, uint64_t entity, float seconds);
    float (*duration)(void* ctx, uint64_t entity);       /* 今のクリップの長さ (秒。無ければ 0) */
    int (*ended)(void* ctx, uint64_t entity);            /* ループでないクリップが端まで流れた */
    /* 骨のワールドの位置 (float 3) と回転 (四元数 x y z w: float 4)。その骨が無い・まだ動いていないと 0
       (そのときは、エンティティのワールドの位置と回転を入れる)。 */
    int (*bone)(void* ctx, uint64_t entity, const char* name, float* position, float* rotation);
} RmAnimApi;

#ifdef __cplusplus
}
#endif
