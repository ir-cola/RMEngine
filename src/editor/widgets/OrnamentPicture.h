#pragma once
// テーマパックの絵 (w::OrnamentInfo の `window`) を描くときの共通の道具 (2026-11-26: 季節パックから移した。
// 季節・探検・ゲーミングのパックが使う)。
//
// 絵 = 色の付いた図形 (ui::Path) を順に塗ったもの。1 フレームに何度も描かれる (背景、そして各パネルとバーの上に、
// 切り抜いて) ので、ウィンドウの大きさごとに 1 回だけ作り、最近のいくつかのウィンドウ (メインとパネルのウィンドウ)
// の分を覚えておく。影の色はテーマのアクセントから来るので、アクセントが変わっても作り直す。

#include "ui/Path.h"
#include "ui/Types.h"

#include <algorithm>
#include <vector>

namespace rm::ui {
class Context;
}

namespace rm::editor::w {

struct OrnamentLayer {
    ui::Path path;
    ui::Color color;
};
using OrnamentPicture = std::vector<OrnamentLayer>;
// 絵を作る関数: `kind` はパックが決める番号 (どの絵か)。`screen` = 画面 (モニター) の場所 (使わない絵には空を渡す)。
using MakeOrnament = OrnamentPicture (*)(int kind, const ui::Rect& window, bool floating, const ui::Rect& screen);

// `make` で作った (覚えておいた) 絵を塗る。
void DrawOrnamentPicture(ui::Context& c, MakeOrnament make, int kind, const ui::Rect& window, bool floating, const ui::Rect& screen = {});

// 左下の角の影 (どのパックも同じ正方形に収める: 桜の花も、ロケットも、ゲームのコントローラーも同じ高さ)。
// 大きさはウィンドウで決まる。形は ui::shape::FitInto でここに収める。
inline ui::Rect OrnamentCorner(const ui::Rect& w) {
    const float r = std::clamp(std::min(w.w, w.h) * 0.17f, 56.f, 150.f);
    return {w.x + 28.f, w.y + w.h - 28.f - 2.f * r, 2.f * r, 2.f * r};
}

}  // namespace rm::editor::w
