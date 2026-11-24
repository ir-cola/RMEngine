/* RM Engine - スクリプトの API のうち ゲーム UI の部分 (プラグイン ゲーム UI が提供する)。素の C で、
 * RmAbi.h と同じ。スクリプトが取り込むのは RMUI.h で、これではない。この部分には専用の版がある:
 * 変えたら版を上げる (RM_ABI_VERSION はそのまま: フック OnClick は RmAbi.h にある)。 */
#pragma once

#include <stdint.h>

#define RM_PART_UI "rm_engine.game_ui.ui"
#define RM_PART_UI_VERSION 3u /* 3: 入力の部品。2: トゥイーンと道 (1 と 2 は同じ表の頭: 今も渡す) */

#ifdef __cplusplus
extern "C" {
#endif

/* それぞれ、そのエンティティ自身の UI の部分 (文字 / 画像 / バー / ボタン) に働き、それがなければ 0 を返す。
   UI のウィンドウは キャンバス エンティティで、名前で探す (階層の最初のもの)。
   setText / getText: 入力欄のあるエンティティでは、その入っている文字 (版 3 のエンジン)。 */
typedef struct RmUiApi {
    int (*setText)(void* ctx, uint64_t entity, const char* utf8);
    /* テキスト (UTF-8、0 終端、cap で切る) をコピーし、全体の長さをバイトで返す。 */
    uint32_t (*getText)(void* ctx, uint64_t entity, char* buf, uint32_t cap);
    int (*setTextColor)(void* ctx, uint64_t entity, const float rgba[4]);
    int (*setImage)(void* ctx, uint64_t entity, const char* assetId); /* "" = なし (その色の長方形) */
    int (*setImageColor)(void* ctx, uint64_t entity, const float rgba[4]);
    int (*setBar)(void* ctx, uint64_t entity, float value);          /* 0..1 */
    float (*getBar)(void* ctx, uint64_t entity);                       /* -1: バーなし */
    int (*setInteractable)(void* ctx, uint64_t entity, int on);
    /* ウィンドウ: 1 = できた、0 = その名前のキャンバスがない (1 度だけ知らせる)。 */
    int (*showWindow)(void* ctx, const char* nameUtf8, int shown);
    int (*switchWindow)(void* ctx, const char* nameUtf8); /* それを表示し、ほかのウィンドウはすべて隠す */
    int (*windowShown)(void* ctx, const char* nameUtf8);
    /* ボタンへのキーのフォーカス (そのウィンドウがキーを受けること)。 */
    int (*focus)(void* ctx, uint64_t entity);
    /* --- 版 2 --- トゥイーン: いまの所からそこまで `seconds` かけて (0: すぐに)。ease: 0 一定,
       1 ゆっくり始まる, 2 ゆっくり終わる, 3 両方, 4 はねる, 5 行き過ぎて戻る。0: UI の配置がない。 */
    int (*moveTo)(void* ctx, uint64_t entity, float x, float y, float seconds, int ease); /* its 位置 */
    int (*scaleTo)(void* ctx, uint64_t entity, float scale, float seconds, int ease);     /* 1: as it is */
    int (*rotateTo)(void* ctx, uint64_t entity, float degrees, float seconds, int ease);  /* its 回転 */
    int (*fadeTo)(void* ctx, uint64_t entity, float alpha, float seconds, int ease);      /* 0 見えない .. 1 */
    /* その 道に沿って動く: 1 は続けて再生、0 は止める、2 は最初から再生。0: それを持たない。 */
    int (*playPath)(void* ctx, uint64_t entity, int how);
    /* --- 版 3 --- 入力の部品。値が変わるとスクリプトの OnEvent に "UIChanged"、入力欄の Enter で
       "UISubmitted" が届く (ここで入れた値では送らない)。 */
    /* 値: スライダーの値・トグルの 1 / 0 を *out に。0: どちらも持たない。 */
    int (*getValue)(void* ctx, uint64_t entity, float* out);
    /* スライダー (刻みに合わせる)・トグル (0 以外でオン。グループのほかはオフに)。0: どちらも持たない。 */
    int (*setValue)(void* ctx, uint64_t entity, float value);
    /* 文字を打っている入力欄 (0: なし)。 */
    uint64_t (*typing)(void* ctx);
    /* その入力欄に打ち始める (0: 打つのをやめる)。0: 押せる入力欄ではない。 */
    int (*startTyping)(void* ctx, uint64_t entity);
    /* スクロールの位置 (0..1。x 横・y 縦。動かない向きは 0)。0: スクロールを持たない。 */
    int (*getScroll)(void* ctx, uint64_t entity, float out[2]);
    int (*setScroll)(void* ctx, uint64_t entity, float x, float y);
} RmUiApi;

#ifdef __cplusplus
}
#endif
