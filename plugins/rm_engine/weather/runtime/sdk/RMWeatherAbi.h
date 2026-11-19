/* RM Engine - the weather part of the scripts' API (the plugin 天候 gives it). Plain C, like RmAbi.h.
 * Scripts include RMWeather.h, not this. Change the part -> raise its version. */
#pragma once

#include <stdint.h>

/* The scene's 天候: the weather and the wind, at once (seconds <= 0) or faded over `seconds` (from the
   weather of that moment: a fade begun during another starts from where that one has come).
   weather: 0 晴れ, 1 くもり, 2 雨, 3 雪.
   which: 0 雲の量 of the weather (晴れ: its own), 1 雨の量, 2 雪の量, 3 風の向き (degrees), 4 風の強さ (m/s),
          6 雷 (0 / 1; never faded), and only to read (now = 1): 7 濡れ (0 .. 1).
   get: now = 0 the value set (where a fade goes); now = 1 the value this moment (雨の量 / 雪の量: what
   falls - nothing while the weather is not rain / snow).
   Each returns 0 when the scene has no 天候 (or `which` is not known). */
#define RM_PART_WEATHER "rm_engine.weather.weather"
#define RM_PART_WEATHER_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RmWeatherApi {
    int (*setWeather)(void* ctx, int weather, float seconds);
    int (*weather)(void* ctx); /* the weather set; -1: no 天候 */
    int (*set)(void* ctx, int which, float value, float seconds);
    int (*get)(void* ctx, int which, int now, float* out);
    /* 0 .. 1: how far the fade has come (1: none going on). */
    float (*fading)(void* ctx);
} RmWeatherApi;

#ifdef __cplusplus
}
#endif
