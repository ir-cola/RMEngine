/* RM Engine - the weather part of the scripts' API (the plugin 天候 gives it). Plain C, like RmAbi.h.
 * Scripts include RMWeather.h, not this. Change the part -> raise its version. */
#pragma once

#include <stdint.h>

/* The scene's 天候: the weather and the wind, at once (seconds <= 0) or faded over `seconds` (from the
   weather of that moment: a fade begun during another starts from where that one has come).
   weather (a テンプレート): 0 快晴, 1 晴れ, 2 くもり, 3 霧, 4 ぽつぽつ雨, 5 小雨, 6 雨, 7 どしゃぶり, 8 嵐,
          9 小雪, 10 雪, 11 大雪, 12 吹雪.
   which: 0 雲の量, 1 降水量, 2 霧の量 (0 .. 100), 3 風の向き (degrees), 4 風の強さ (m/s) - faded;
          5 雪にする (0 / 1: rain weakens and turns to snow over about 5 s by itself), 6 雷 (0 / 1),
          8 積もる高さ (cm, 0 .. 5) - at once; and only to read (now = 1): 7 濡れ (0 .. 1), 9 雪 (0 .. 100).
   get: now = 0 the value set (where a fade goes); now = 1 the value this moment (1: what falls as rain;
   5: how far it has turned to snow, 0 .. 1; 8: how deep the snow lies, cm).
   Each returns 0 when the scene has no 天候 (or `which` is not known).
   Version 2 (2026-11-20): amounts 0 .. 100 (were 0 .. 1), one 降水量 for rain and snow, the weathers are
   the テンプレート, 5 / 8 / 9 new. */
#define RM_PART_WEATHER "rm_engine.weather.weather"
#define RM_PART_WEATHER_VERSION 2u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmWeatherApi {
    int (*setWeather)(void* ctx, int weather, float seconds);
    int (*weather)(void* ctx); /* 0: the scene has 天候; -1: none */
    int (*set)(void* ctx, int which, float value, float seconds);
    int (*get)(void* ctx, int which, int now, float* out);
    /* 0 .. 1: how far the fade has come (1: none going on). */
    float (*fading)(void* ctx);
} RmWeatherApi;

#ifdef __cplusplus
}
#endif
