/* RM Engine - the game UI's part of the scripts' API (the plugin ゲーム UI gives it). Plain C, like
 * RmAbi.h. Scripts include RMUI.h, not this. The part has its own version: change it -> raise the version
 * (RM_ABI_VERSION stays: the hook OnClick is in RmAbi.h). */
#pragma once

#include <stdint.h>

#define RM_PART_UI "rm_engine.game_ui.ui"
#define RM_PART_UI_VERSION 2u /* 2: the tweens and the paths (1 is the same table without them: still given) */

#ifdef __cplusplus
extern "C" {
#endif

/* Each works on the entity's own UI part (文字 / 画像 / バー / ボタン) and returns 0 when it has none.
   A UI window is a キャンバス entity, found by its name (the first in the 階層). */
typedef struct RmUiApi {
    int (*setText)(void* ctx, uint64_t entity, const char* utf8);
    /* Copies the text (UTF-8, 0-terminated, cut to cap) and returns its full length in bytes. */
    uint32_t (*getText)(void* ctx, uint64_t entity, char* buf, uint32_t cap);
    int (*setTextColor)(void* ctx, uint64_t entity, const float rgba[4]);
    int (*setImage)(void* ctx, uint64_t entity, const char* assetId); /* "" = none (a rectangle of its colour) */
    int (*setImageColor)(void* ctx, uint64_t entity, const float rgba[4]);
    int (*setBar)(void* ctx, uint64_t entity, float value);          /* 0..1 */
    float (*getBar)(void* ctx, uint64_t entity);                       /* -1: no bar */
    int (*setInteractable)(void* ctx, uint64_t entity, int on);
    /* Windows: 1 = done, 0 = no canvas of that name (told once). */
    int (*showWindow)(void* ctx, const char* nameUtf8, int shown);
    int (*switchWindow)(void* ctx, const char* nameUtf8); /* that one shown, every other window hidden */
    int (*windowShown)(void* ctx, const char* nameUtf8);
    /* The keys' focus on a button (its window must take the keys). */
    int (*focus)(void* ctx, uint64_t entity);
    /* --- version 2 --- Tweens: from where it is now to there over `seconds` (0: at once). ease: 0 一定,
       1 ゆっくり始まる, 2 ゆっくり終わる, 3 両方, 4 はねる, 5 行き過ぎて戻る. 0: no UI の配置. */
    int (*moveTo)(void* ctx, uint64_t entity, float x, float y, float seconds, int ease); /* its 位置 */
    int (*scaleTo)(void* ctx, uint64_t entity, float scale, float seconds, int ease);     /* 1: as it is */
    int (*rotateTo)(void* ctx, uint64_t entity, float degrees, float seconds, int ease);  /* its 回転 */
    int (*fadeTo)(void* ctx, uint64_t entity, float alpha, float seconds, int ease);      /* 0 unseen .. 1 */
    /* Its 道に沿って動く: 1 plays on, 0 stops, 2 plays from the start. 0: it has none. */
    int (*playPath)(void* ctx, uint64_t entity, int how);
} RmUiApi;

#ifdef __cplusplus
}
#endif
