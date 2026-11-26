#pragma once
// ゲーミング テーマの動き: 流れる虹の色、縁に沿って流れる虹の位置、漂う光の粒。純粋 (tests/TestTheme.cpp でテストする)。
// 描くのは Scenes.cpp (GPU の動く絵) と Gaming.cpp (パネルの縁の虹)。

#include "ui/Types.h"

#include <cstdint>

namespace rm::theme_gaming::motion {

// 虹の色: t = 0 .. 1 で一回り (赤 → 黄 → 緑 → 水色 → 青 → 紫 → 赤)。外は回して読む。鮮やかなまま。
Color Rainbow(float t, float alpha = 1.f);
// 四角 r の縁を、左上から時計回りにたどった長さ (周 = 2 (w + h))。`edge` = 0 上 / 1 右 / 2 下 / 3 左、`along` = その辺の
// 始まり (時計回りの向き) からの長さ。
float PerimeterAt(const ui::Rect& r, int edge, float along);
// 縁の虹の色の位置 (0 .. 1 で一回り): 周の上の長さ `s`、時刻 `t`。`period` DIP で虹が一回りし、`speed` 回り / 秒で流れる
// (時計回りに進む)。
float EdgeHue(float s, double t, float period, float speed);
// 漂う光の粒: 下から上へゆっくり上り、端で回り込む。またたく。
struct Mote {
    Vec2 at;
    float radius = 0;
    float alpha = 0;  // 0 .. 1
    float hue = 0;    // 0 .. 1 (Rainbow)
};
Mote MoteAt(const ui::Rect& window, uint32_t index, double t, uint32_t seed);

}  // namespace rm::theme_gaming::motion
