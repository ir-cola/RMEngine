#pragma once
// ゲーミング テーマの絵の形と色。純粋 (tests/TestTheme.cpp でテストする)。Gaming.cpp が描く。

#include "ui/Path.h"
#include "ui/Types.h"

#include <cstdint>

namespace rm::theme_gaming::shapes {

// 虹の色: t = 0 .. 1 で 赤 → 黄 → 緑 → 水色 → 青 → 紫 (色相 0 .. 300 度、鮮やかなまま)。外は回して読む。
ui::Color Spectrum(float t, float alpha = 1.f);
// 下のほうのネオンの格子 (遠近): `horizon` (y) から下の端まで、下ほど間の広い横の線と、地平線の真ん中に集まる縦の線。
// `lines` = 細い線、`horizonLine` = 地平線の明るい線。横の線は r の左右の端まで (描く側が切る)。
struct GridShapes {
    ui::Path lines, horizonLine;
};
GridShapes NeonGrid(const ui::Rect& window, float horizon, float spacing);
// `area` に散らした小さな四角 (ドットの絵のかけら) `count` 個。大きさは `size` か、その 2 倍。
ui::Path Pixels(const ui::Rect& area, int count, float size, uint32_t seed);
// ゲームのコントローラー: 横長の胴と、左右の握り。radius = 中心から握りの先まで。`degreesCcw` 回す。
ui::Path Controller(Vec2 center, float radius, float degreesCcw);

}  // namespace rm::theme_gaming::shapes
