#pragma once
// 天候 - 雨雲のかたまり (2026-11-26): 気候の地図に描いた雨雲 (その場所だけ降る雨) のすぐ上空に、低い雨雲を
// 3D のかたまり (カメラを向いた、縁のもくもくした丸い板) で置く。降る量に比例して、小雨は明るく薄い 1 段の
// 膜、嵐は黒ずんだ 3 段の厚い雲 (下の段ほど暗い)。地球の空の高い雨雲の層 (600 m 上) とは別に、雨の降る所の
// 真上に見える: 小さく描いた雨雲でも、その上に雲が出る。
//
//   置く所  雲の地図 (PlaceClouds: 1 マス kCloudMapCell m) の、その場所だけ降る量 (rains) のあるマスの上。
//           マスの中の位置・大きさのゆらぎはワールドのマスの番号から決める (カメラが動いても雲は動かない)
//   高さ    kRainCloudBase m から上へ段を重ねる
//   数      カメラから kRainCloudReach m (真上から見て) まで、近いものから kMostRainPuffs 個まで
//
// 純粋 (I/O も描画もしない)。描くのは WeatherDraw.cpp。tests/TestWeather.cpp の Weather_RainCloudPuffs。

#include "base/Math.h"
#include "rm_engine/weather/runtime/WeatherPlace.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rm::weather {

inline constexpr float kRainCloudBase = 70.f;     // 雨雲の底の高さ (m)
inline constexpr float kRainCloudReach = 1200.f;  // カメラからこの距離 (真上から見て) まで置く (m)
inline constexpr size_t kMostRainPuffs = 3000;     // かたまりの数の上限
inline constexpr float kLeastRainForPuff = 0.01f;  // これより少なく降る所には置かない

// 雨雲のかたまり 1 つ: 真ん中、半径 (m)、暗さ (0: 明るい膜 .. 1: 黒ずんだ底)、濃さ (0 .. 1)、模様の種。
struct CloudPuff {
    Vec3 center;
    float radius = 1;
    float shade = 0;
    float alpha = 1;
    uint32_t seed = 0;
};
// 雲の地図 `map` (一辺 `cells` マス、1 マス `cell` m) の、その場所だけ降る量のあるマスの上にかたまりを置く。
// 段の数 = 1 + 降る量の厚み (smoothstep(0.15, 0.95, 降る量)) x 3 未満 (1 〜 3)、濃さ・暗さ・大きさも降る量に
// 比例。並びはカメラ `eye` から遠い順 (後ろから描く: 透けるものを重ねる)。地図の数が合わない: 空。
void RainCloudPuffs(const PlaceClouds& map, int cells, float cell, Vec3 eye, std::vector<CloudPuff>& out);

// 描く三角形の頂点: 場所、板の上の場所 (-1 .. 1)、暗さ・濃さ・模様の種。
struct PuffVertex {
    Vec3 position;
    float u = 0, v = 0;
    float shade = 0, alpha = 1, seed = 0;
};
// かたまりごとに、カメラを向いた正方形 (三角形 2 つ・頂点 6 個)。`right`, `up`: カメラの右と上 (長さ 1)。
void PuffQuads(std::span<const CloudPuff> puffs, Vec3 right, Vec3 up, std::vector<PuffVertex>& out);

// かたまりを描くシェーダー (PuffVS / PuffPS。vs_4_0 / ps_4_0)。b4 に PuffConstants、頂点は PuffVertex。
// 丸いぼかしの縁をノイズでもくもくさせ、暗さで明るい膜の色から黒ずんだ底の色へ (板の下ほど暗い)。
// 近すぎる所 (カメラが雲の中) と kRainCloudReach の近くは薄れる。透ける (アルファで重ねる)。
extern const char kRainPuffHlsl[];
struct PuffConstants {
    float viewProj[16];
    float light[4];  // rgb: 雲を照らす光 (空と太陽)
    float eye[4];    // xyz: カメラ、w: 薄れきる距離 (kRainCloudReach)
};
static_assert(sizeof(PuffConstants) % 16 == 0);
static_assert(sizeof(PuffVertex) == 32, "PuffIn と合わせる");

}  // namespace rm::weather
