#pragma once
// 天候 - 雷 (2026-11-26): 空の閃光 (SkyWeather::flash) と、目に見える稲妻 (空から地面へ走る光の筋と、
// 光ったあとの残り)。いつ・どこに・どんな形で光るかはゲームの時間だけから決まる: 同じ時間なら同じ稲妻で、
// 空の閃光と稲妻はいつも同じ時に光る。天候の「雷」をオンにして、雲が多いとき (Play 中) に出る。
//
//   いつ    kStrikeTick 秒ごとに、その刻みの番号から作った数で起きるかを決める (40 回に 1 回ほど)
//   明るさ  光ってから: 強く光る → 一瞬暗くなる → もう一度光る (2 度目の落雷) → 残光でゆっくり消える
//   形      雲の下から地面へ、曲がりくねった幹 (中点を横にずらしながら細かくする) と、途中から出る細い枝
//   場所    カメラのまわりの、遠すぎず近すぎない所 (向きと距離も刻みの番号から)
//
// 純粋 (I/O も描画もしない)。描くのは LightningDraw.cpp。tests/TestWeather.cpp でテストする。

#include "base/Math.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rm::weather {

inline constexpr float kStrikeTick = 1.f / 8;  // 雷が起きるかを決める刻み (秒)
inline constexpr float kBoltLife = 0.7f;       // 稲妻が見えている長さ (光ってから消えるまで、秒)
inline constexpr float kBoltNear = 120.f, kBoltFar = 420.f;  // カメラから落ちる所までの距離 (m)
inline constexpr float kBoltHeight = 260.f;    // 稲妻の上の端 (雲の下) の、地面からの高さ (m)

// 0 .. 1 の数を、ある数から毎回同じになるように作る (雷の起きる時・明るさ・形・場所)。
float Hash01(uint32_t x);

struct Strike {
    uint32_t seed = 0;   // 起きた刻みの番号 (形と場所を決める)
    float age = 0;       // 光ってからの秒 (0 .. kBoltLife)
    float strength = 1;  // 0.5 .. 1
};
// ゲームの時間 `seconds` に見えている、一番新しい稲妻。起きていない・もう消えた・時間が進んでいない (0 以下): 無し。
std::optional<Strike> StrikeAt(double seconds);
// 光ってから `age` 秒の稲妻の明るさ (0 .. 1): 強く光る → 暗くなる → もう一度光る → 残光で薄れて kBoltLife で 0。
float BoltGlow(float age);
// 同じときの空全体の閃光 (0 .. 1): 稲妻と同じ時に光るが、残光は短い。
float FlashGlow(float age);

// 稲妻の 1 本の線: a から b へ、太さの割合 (幹 1。枝は細い) と明るさの割合 (枝は暗い)。
struct BoltSegment {
    Vec3 a, b;
    float width = 1;
    float glow = 1;
};
// `seed` の稲妻の線: `top` (雲の下) から `bottom` (地面) への幹 (曲がりくねる: 長さの 1 割ほど横にぶれる) と、
// 幹の途中から下へ伸びて途中で消える枝 (2 〜 4 本)。幹は top から bottom へつながった順に並ぶ (最初が top、
// 幹の最後が bottom)。
std::vector<BoltSegment> BoltPath(uint32_t seed, Vec3 top, Vec3 bottom);
// `seed` の稲妻が、カメラ `eye` から見てどこに落ちるか: 地面 (高さ `ground`) の落ちる所と、その上の雲の下。
// 落ちる所はカメラから kBoltNear .. kBoltFar (真上から見て)、向きはどこでも。
void BoltPlace(uint32_t seed, Vec3 eye, float ground, Vec3& top, Vec3& bottom);

// 描く三角形の頂点: 線の場所、太さの向きのどこか (-1 .. 1: 真ん中が 0)、明るさの割合。
struct BoltVertex {
    Vec3 position;
    float across = 0;
    float light = 1;
};
// 線ごとに、カメラ `eye` の方を向いた帯 (三角形 2 つ) を 2 枚: 細く明るい芯 (`coreWidth` m) と、太く淡い光の
// にじみ (`haloWidth` m)。太さは線の width を掛ける。1 本につき頂点 12 個。
void BoltRibbons(std::span<const BoltSegment> segments, Vec3 eye, float coreWidth, float haloWidth, std::vector<BoltVertex>& out);

}  // namespace rm::weather
