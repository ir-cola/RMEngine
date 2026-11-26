#pragma once
// アニメーション - 混ぜる重み (docs/ANIMATION.md 4-5「混ぜるは図で置く」)。純粋 (描画もファイルもない)。
// tests/TestAnimationRules.cpp でテストする。3D も 2D も知らない: 点 (クリップの置き場所) と、今のパラメーターの
// 値から、点ごとの重み (合計 1) を出すだけ。
//
//   1D  数直線の上の点。値の両隣の 2 つを直線で混ぜる。端より外は端の点だけ
//   2D  平面の上の点を三角形に分け (ドロネー)、値の入った三角形の 3 点で混ぜる (重心座標)。三角形の外は、
//       一番近い辺 (の上の一番近い所) の 2 点で混ぜる。点が 1 つ: それだけ。全部が一直線: その線の上の 1D

#include "base/Math.h"

#include <array>
#include <span>
#include <vector>

namespace rm::animation {

// 1D: 点 `points` (並んでいなくてよい) と値 `x` → 点ごとの重み `out` (点と同じ数)。同じ値の点が 2 つあれば
// 前の点。点が無い: 空。
void BlendWeights1D(std::span<const float> points, float x, std::vector<float>& out);

// ドロネーの三角形分割 (Bowyer-Watson)。三角形は点の番号 3 つ (反時計回り)。同じ点・一直線: 三角形が無いこともある。
std::vector<std::array<int, 3>> Triangulate(std::span<const Vec2> points);

// 2D: 点 `points` と値 `p` → 点ごとの重み `out`。`triangles` は Triangulate(points) (毎フレーム作り直さない)。
void BlendWeights2D(std::span<const Vec2> points, std::span<const std::array<int, 3>> triangles, Vec2 p, std::vector<float>& out);

}  // namespace rm::animation
