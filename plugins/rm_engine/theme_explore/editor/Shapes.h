#pragma once
// 探検テーマパック - 宇宙・深海・火山の絵の形。純粋: ui::Path を作るだけ (tests/TestTheme.cpp でテストする)。
// Explore.cpp がそれぞれの色で、内容の後ろに描く。図形はどれも画面上で時計回り (重なると 1 つの影になる)。
// 散らばりは決まった種から (毎フレーム同じ絵)。円・多角形・粒・下の波などの部品は核の ui/Shape.h。

#include "ui/Path.h"
#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rm::theme_explore::shapes {

// --- 宇宙 ---------------------------------------------------------------------------------
// `area` に散らした 4 本の光の筋のきらめく星 `count` 個 (大きさは約 `size` の 0.5 .. 1 倍)。
ui::Path Sparkles(const ui::Rect& area, int count, float size, uint32_t seed);
// 星雲: `center` のまわりに重なる、大きさの違うもやの楕円。外ほど広く薄い 3 枚 (外 → 内の順に塗る)。`size` = 外の半径。
std::array<ui::Path, 3> Nebula(Vec2 center, float size, uint32_t seed);
// 輪のある惑星: 球 (`body`) と、傾いた輪の奥の半分 (`ringBack`: 球より先に塗る) と手前の半分 (`ringFront`: 球の後に塗る)。
// radius = 球の半径。輪は球の 2.2 倍まで広がる。`tiltDegrees` = 輪の傾き (反時計回り)。
struct PlanetShapes {
    ui::Path ringBack, body, ringFront;
};
PlanetShapes RingedPlanet(Vec2 center, float radius, float tiltDegrees);
// 流れ星: `head` (明るい頭) から `degreesCcw` の向きへ伸びて細くなる尾。length = 尾の長さ。
ui::Path ShootingStar(Vec2 head, float length, float degreesCcw);
// ロケット: とがった先、窓のない胴、左右の翼、下の噴射の炎。radius = 中心から先まで。`degreesCcw` 回す (0: 上向き)。
ui::Path Rocket(Vec2 center, float radius, float degreesCcw);

// --- 深海 ---------------------------------------------------------------------------------
// 上の端から斜めに差しこむ光の筋 `count` 本 (1 本に 1 つのパス: 重ねて塗ると、重なった所が明るい)。
std::vector<ui::Path> LightRays(const ui::Rect& window, int count, uint32_t seed);
// 泡: `area` の下ほど小さい丸 `count` 個 (`body`) と、それぞれの左上の小さなつや (`shine`)。
struct BubbleShapes {
    ui::Path body, shine;
};
BubbleShapes Bubbles(const ui::Rect& area, int count, float size, uint32_t seed);
// 海藻のひとむら: `base` (海の底) から高さ `height` まで、揺れながら細くなる帯が数本。
ui::Path Kelp(Vec2 base, float height, uint32_t seed);
// 魚の群れ: `center` のまわり (横 `spread`、縦 spread の半分) に、長さ約 `length` の魚 `count` 匹 (みな左を向く)。
ui::Path FishSchool(Vec2 center, float spread, int count, float length, uint32_t seed);
// クラゲ: 丸い傘と、波打って垂れる足 5 本。radius = 中心から傘のてっぺんまで。`degreesCcw` 回す。
ui::Path Jellyfish(Vec2 center, float radius, float degreesCcw);

// --- 火山 ---------------------------------------------------------------------------------
// 火山: `base` (ウィンドウの下の端の、山の真ん中) から高さ `height` の山 (`mountain`。てっぺんに火口のくぼみ)、
// 火口から斜面を流れ下る溶岩 (`lava`)、火口の上の照り (`glow`: 大きさの違う丸 3 つ。重ねて塗る)。
struct VolcanoShapes {
    ui::Path mountain, lava;
    std::array<ui::Path, 3> glow;
    Vec2 crater;  // 火口の真ん中 (煙が出る所)
};
VolcanoShapes Volcano(Vec2 base, float height, uint32_t seed);
// 煙: `from` から上へ高さ `height` まで、上るほど大きくなり左へ流れる丸いもくもく。
ui::Path Smoke(Vec2 from, float height, uint32_t seed);
// 火の粉: `area` の下ほど多い、小さなひし形 `count` 個 (大きさ約 `size`)。
ui::Path Embers(const ui::Rect& area, int count, float size, uint32_t seed);
// 炎: しずく形の大きな炎と、その左右の小さな炎。radius = 中心から先まで。`degreesCcw` 回す。
ui::Path Flame(Vec2 center, float radius, float degreesCcw);

}  // namespace rm::theme_explore::shapes
