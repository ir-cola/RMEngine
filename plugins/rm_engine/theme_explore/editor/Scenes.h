#pragma once
// 探検テーマパックの動く絵 (宇宙 / 深海 / 火山): それぞれの場のシェーダー (HLSL) と、毎フレームの粒。
// 描き方は核の editor/widgets/MovingBackdrop.h (GPU で 1 フレーム 1 枚。地の色に光を足す)。
// 粒の動きは Motion.h (純粋、テストあり)。

#include "editor/widgets/MovingBackdrop.h"

namespace rm::theme_explore::scenes {

extern const editor::w::MovingLook kSpace;
extern const editor::w::MovingLook kDeepSea;
extern const editor::w::MovingLook kVolcano;

// パネルとバーの上に足す光の強さ (ウィンドウの背景は 1)。文字が読めるように弱く。
inline constexpr float kPanelStrength = 0.42f;

}  // namespace rm::theme_explore::scenes
