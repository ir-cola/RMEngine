/* RM Engine - the weather part of the scripts' API (the plugin 天候 gives it). Plain C, like RmAbi.h.
 * Scripts include RMWeather.h, not this. Change the part -> raise its version. */
#pragma once

#include <stdint.h>

/* The scene's 天候: the weather and the wind, at once (seconds <= 0) or faded over `seconds` (from the
   weather of that moment: a fade begun during another starts from where that one has come).
   weather (a テンプレート): 0 快晴, 1 晴れ, 2 くもり, 3 ぽつぽつ雨, 4 小雨, 5 雨, 6 どしゃぶり, 7 嵐, 8 小雪,
          9 雪, 10 大雪, 11 吹雪.
   which: 0 雲の量, 1 雨の量, 2 雪の量 (0 .. 100), 3 風の向き (degrees), 4 風の強さ (m/s) - faded;
          5 雪を降らせる (0 / 1: the snow comes in over about 10 s by itself), 6 雷 (0 / 1), 8 積もる高さ (m) -
          at once; and only to read (now = 1): 7 濡れ (0 .. 1).
   get: now = 0 the value set (where a fade goes); now = 1 the value this moment (雨の量 / 雪の量: what
   falls; 5: how far the snow has come in, 0 .. 1; 8: how deep the snow lies, m).
   Each returns 0 when the scene has no 天候 (or `which` is not known).
   Version 2 (2026-11-20): amounts 0 .. 100 (were 0 .. 1), the weathers are the テンプレート, 5 and 8 new. */
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
