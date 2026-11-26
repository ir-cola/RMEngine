#pragma once
// ui::Path を作る部品: 円・楕円・多角形・棒・箱に収める・決まった種の乱数・回転。
// テーマパックの絵 (季節・探検・ゲーミング) が共通で使う (2026-11-26: 季節パックの Ornament から移した)。
// 純粋: Path を作るだけ (tests/TestTheme.cpp でテストする)。
//
// どの図形も画面上で時計回りにまわる (描く側は巻き数で塗るので、重なった図形は 1 つの影になり、穴はできない)。

#include "ui/Path.h"
#include "ui/Types.h"

#include <cstdint>
#include <span>

namespace rm::ui::shape {

// 画面上で反時計回り (y は下向き) に `degreesCcw` 度回す: 上 (0, -1) は左へ行く。
Vec2 Rotate(Vec2 v, float degreesCcw);

void AddCircle(Path& p, Vec2 center, float r);
void AddEllipse(Path& p, Vec2 center, float rx, float ry, float degrees);
// 閉じた多角形。点がどちら回りに与えられても時計回りにする。
void AddPolygon(Path& p, std::span<const Vec2> points);
// `a` から `b` への棒。a での幅が `wa`、b での幅が `wb`。
void AddBar(Path& p, Vec2 a, Vec2 b, float wa, float wb);
// `points` 本のとがった星 (外の半径 `outer`、へこみの半径 `inner`)。先の 1 つがまっすぐ上を向き、`degreesCcw` 回す。
void AddStar(Path& p, Vec2 center, int points, float outer, float inner, float degreesCcw = 0);

// `area` に散らした `count` 個の丸い粒 (直径は約 `size` の 0.35 .. 1 倍): 降る雪、星、泡、火の粉。
Path Dots(const Rect& area, int count, float size, uint32_t seed);
// `r` (ウィンドウ) の下の端に沿って、なだらかに波打つ帯 (下の端から depth の 0.45 .. 1 倍まで): 夜の海、溶岩の照り、
// 海の底。うねりは r の左端からのピクセルだけで決まる (大きさを変えても形は変わらない)。`phase` で波をずらす
// (重ねる 2 枚目)。左右と下は 2 ピクセル外まで (描く側がウィンドウで切る)。
Path WavyBottom(const Rect& r, float depth, float phase);

// `box` の中に収まる最大の大きさで中央に置くよう、動かして拡大縮小したパス (縦横同じ倍率)。
Path FitInto(const Path& path, const Rect& box);

// 種から作る、何度でも同じになる小さな乱数 (0 .. 1)。毎フレーム同じ絵になる。
struct Random {
    uint32_t state;
    float Next();
};

}  // namespace rm::ui::shape
