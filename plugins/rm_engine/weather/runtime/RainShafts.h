#pragma once
// 天候 - 遠くの雨の幕 (2026-11-26): 雨粒はカメラのまわり (kRainBox の箱) にしか描かないので、少し離れると
// 描いた雨雲の下の雨が見えなかった。雨の降るマス (雲の地図のその場所だけ降る量) ごとに、雨雲の層 (カメラの
// kRainCloudHeight m 上) から地面の下まで、縦に流れる筋の入った半透明の板 (カメラの方へ縦の軸で回す) を立てる。
// 近く (雨粒の見える所) では消え、離れるほど見える。濃さは降る量に比例し、風下へ傾く。
//
// 純粋 (I/O も描画もしない)。描くのは WeatherDraw.cpp。tests/TestWeather.cpp の Weather_RainShafts。

#include "base/Math.h"
#include "rm_engine/weather/runtime/WeatherPlace.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rm::weather {

inline constexpr float kShaftReach = 1500.f;        // カメラからこの距離 (真上から見て) まで立てる (m)
inline constexpr float kShaftNear = 10.f;           // これより近いマスには立てない (雨粒が見える)
inline constexpr size_t kMostShafts = 2000;         // 板の数の上限 (近いものから)
inline constexpr float kLeastRainForShaft = 0.02f;  // これより少なく降る所には立てない
inline constexpr float kShaftFall = 9.f;            // 傾き: 風 (m/s) をこの落ちる速さ (m/s) で割った分だけ風下へ

// 雨の幕 1 枚: 地面の所 (x, z)、幅 (m)、降る量 (0 .. 1)、模様の種。
struct RainShaft {
    float x = 0, z = 0;
    float width = 1;
    float rain = 0;
    uint32_t seed = 0;
};
// 雲の地図 `map` (一辺 `cells` マス、1 マス `cell` m) の、その場所だけ降る量のあるマスに 1 枚ずつ。カメラ `eye` から
// kShaftNear .. kShaftReach m (真上から見て)、近いものから kMostShafts 枚まで、並びは遠い順 (後ろから描く)。
// 模様の種はワールドのマスの番号から (カメラが動いても同じ)。地図の数が合わない: 空。
void RainShaftsOf(const PlaceClouds& map, int cells, float cell, Vec3 eye, std::vector<RainShaft>& out);

// 描く三角形の頂点: 場所、板の上の場所 (u: -1 .. 1 横、v: 0 下 .. 1 上)、降る量・模様の種・カメラからの距離 (m)。
struct ShaftVertex {
    Vec3 position;
    float u = 0, v = 0;
    float rain = 0, seed = 0, away = 0;
};
// 板ごとに、カメラの方へ縦の軸で回した長方形 (三角形 2 つ・頂点 6 個): 下 `bottom` から上 `top` (高さ m)、
// 上の端は風 `wind` (m/s、x z) で風上へずらす (雨は風下へ流れて落ちる)。
void ShaftQuads(std::span<const RainShaft> shafts, Vec3 eye, float bottom, float top, Vec2 wind, std::vector<ShaftVertex>& out);

// 幕を描くシェーダー (ShaftVS / ShaftPS。vs_4_0 / ps_4_0)。b4 に ShaftConstants、頂点は ShaftVertex。
// 縦に流れる筋 (列ごとのハッシュと、時間で下へ動く模様)、横の端・上下の端・近く (雨粒の箱)・kShaftReach の
// 近くで薄れる。透ける (アルファで重ねる)。
extern const char kRainShaftHlsl[];
struct ShaftConstants {
    float viewProj[16];
    float light[4];  // rgb: 照らす光 (空と太陽)
    float look[4];   // x: 秒、y: 消えきる近さ (m)、z: 見え始める距離 (m)、w: 薄れきる遠さ (kShaftReach)
};
static_assert(sizeof(ShaftConstants) % 16 == 0);
static_assert(sizeof(ShaftVertex) == 32, "ShaftIn と合わせる");

}  // namespace rm::weather
