/* RM Engine - スクリプトの API の天候の部分 (プラグイン 天候 が与える)。RmAbi.h と同じ素の C。
 * スクリプトが include するのは RMWeather.h で、これではない。部分を変えたら版を上げる。 */
#pragma once

#include <stdint.h>

/* シーンの 天候: 天候と風を、すぐに (seconds <= 0)、または `seconds` 秒かけてなめらかに変える
   (その瞬間の天候から: フェード中に始めたフェードは、そのフェードが来たところから始まる)。
   weather (テンプレート): 0 快晴, 1 晴れ, 2 くもり, 3 霧, 4 ぽつぽつ雨, 5 小雨, 6 雨, 7 どしゃぶり, 8 嵐,
          9 小雪, 10 雪, 11 大雪, 12 吹雪.
   which: 0 雲の量, 1 降水量, 2 霧の量 (0 .. 100), 3 風の向き (度), 4 風の強さ (m/s) - なめらかに変わる;
          5 雪にする (0 / 1: 雨が弱まって、約 5 秒で自然に雪に変わる), 6 雷 (0 / 1),
          8 積もる高さ (cm、整数, 0 .. 1000) - すぐに変わる; 読むだけ (now = 1): 7 濡れ (0 .. 1), 9 雪 (0 .. 100).
   get: now = 0 は設定した値 (フェードの行き先); now = 1 は今この瞬間の値 (1: 雨として降っているもの;
   5: 雪にどこまで変わったか、0 .. 1; 8: 雪の深さ、cm).
   シーンに 天候 がない (または `which` が知らない値) ときは、どれも 0 を返す。
   版 2 (2026-11-20): 量は 0 .. 100 (前は 0 .. 1)、雨と雪で 降水量 を 1 つに、天候はテンプレート、
   5 / 8 / 9 が新しい。 */
#define RM_PART_WEATHER "rm_engine.weather.weather"
#define RM_PART_WEATHER_VERSION 2u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmWeatherApi {
    int (*setWeather)(void* ctx, int weather, float seconds);
    int (*weather)(void* ctx); /* 0: シーンに 天候 がある; -1: ない */
    int (*set)(void* ctx, int which, float value, float seconds);
    int (*get)(void* ctx, int which, int now, float* out);
    /* 0 .. 1: フェードがどこまで来たか (1: 進んでいない)。 */
    float (*fading)(void* ctx);
} RmWeatherApi;

#ifdef __cplusplus
}
#endif
