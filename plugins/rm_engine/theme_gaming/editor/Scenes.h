#pragma once
// ゲーミング テーマの動く絵: 場のシェーダー (HLSL) と、毎フレームの粒。描き方は核の editor/widgets/MovingBackdrop.h。

#include "editor/widgets/MovingBackdrop.h"

namespace rm::theme_gaming::scenes {

extern const editor::w::MovingLook kGaming;

// パネルとバーの上に足す光の強さ (ウィンドウの背景は 1)。
inline constexpr float kPanelStrength = 0.32f;
// 縁の虹: 一回りの長さ (DIP) と、流れる速さ (回り / 秒)。ウィンドウの縁 (シェーダー) とパネルの縁 (Gaming.cpp) で同じ。
inline constexpr float kRainbowPeriod = 900.f;
inline constexpr float kRainbowSpeed = 0.25f;

}  // namespace rm::theme_gaming::scenes
