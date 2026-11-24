#pragma once
// 季節テーマパック - 季節の絵の形 (桜の木、太陽、もみじの葉、雪の結晶、雲、下に積もる雪 ...)。
// 純粋: ui::Path を作るだけ (tests/TestTheme.cpp でテストする)。Seasons.cpp が季節の色で、
// 内容の後ろに描く。
//
// どの図形も同じ向き (画面上で時計回り) にまわり、描く側は巻き数で塗るので、重なった図形
// (木の樹冠の円) は 1 つの影になり、穴はできない。
// 「ランダム」な散らばりは固定した種から作る: 毎フレーム同じ絵になる。

#include "ui/Path.h"
#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <span>

namespace rm::theme_seasons::ornament {

// --- 部品 --------------------------------------------------------------------------
void AddCircle(ui::Path& p, Vec2 center, float r);
void AddEllipse(ui::Path& p, Vec2 center, float rx, float ry, float degrees);
// 閉じた多角形。点がどちら回りに与えられても時計回りにする。
void AddPolygon(ui::Path& p, std::span<const Vec2> points);
// `a` から `b` への棒。a での幅が `wa`、b での幅が `wb` (枝、雪の結晶の腕)。
void AddBar(ui::Path& p, Vec2 a, Vec2 b, float wa, float wb);

// `box` の中に収まる最大の大きさで中央に置くよう、動かして拡大縮小したパス (縦横同じ倍率):
// どの季節の角の影も、元の形に関係なく同じ箱を埋める。
ui::Path FitInto(const ui::Path& path, const ui::Rect& box);

// 種から作る、何度でも同じになる小さな乱数 (0 .. 1)。
struct Random {
    uint32_t state;
    float Next();
};

// --- 春 -----------------------------------------------------------------------------------
// 桜の花。1 つの影として塗る: 花びら 5 枚で、それぞれ先に切れ込みがある。回す前は、
// 花びら 1 枚がまっすぐ上を向く。radius = 中心から花びらの先まで。
// degreesCcw = 画面上で反時計回りに回す度数。
ui::Path Sakura(Vec2 center, float radius, float degreesCcw);
// 満開の桜の木。`window` の下の端から右の端の近くに生える: 幹が (少し曲がり、根元で根が広がりながら)
// ウィンドウの真ん中より少し上の枝分かれまで伸び、そこから枝が上に広がって上のほうの広い花の
// 樹冠になる (上と右はウィンドウの端で切れる)。左へは、自分の房を持つ低い枝。`size` =
// 樹冠の半分の幅。花はウィンドウの右端から左へ size の約 1.6 倍まで届く。
ui::Path CherryTree(const ui::Rect& window, float size);
// その木の樹冠から左へ吹き飛ばされた花びら: 木の近く (`fromX` = 木の左端) では多く、
// ウィンドウを横切って飛ぶにつれて減り、沈む。
ui::Path PetalStream(const ui::Rect& window, float fromX, int count, float size, uint32_t seed);
// `area` に散らした小さな花びら (吹雪のように)。それぞれ長さ約 `size`。
ui::Path Petals(const ui::Rect& area, int count, float size, uint32_t seed);
// `window` の上に沿って濃く、`depth` くらいまで下に向かって薄くなる桜の花びら (秋の Canopy と同じ散らばり)。
// 少しずつ違う 4 色 (1 色に 1 パス)。`perPixel` = 幅 1 ピクセルあたりの花びらの数。
std::array<ui::Path, 4> PetalCanopy(const ui::Rect& window, float depth, float perPixel, uint32_t seed);

// --- 夏 -----------------------------------------------------------------------------------
// 太陽: 丸い円盤とそのまわりのとがった光線 12 本 (長いのと短いのが交互。radius = 長い光線の先まで)。
// `degreesCcw` 回す。
ui::Path Sun(Vec2 center, float radius, float degreesCcw);
// 積雲 (もくもくした夏の雲): 大きさの違うふくらみ (円) がいくつも重なった形で、底は平ら。ふくらみの並びは
// `seed` で少しずつ変わる。`center` = 雲のおおよその真ん中、`width` = 横幅 (底は center.y + 0.16 * width)。
// 塗る順: `body` (雲全体) → `light` (上の縁に沿った明るい帯。上ほど重なって明るい) → `shade` (下ほど重なって
// 暗くなる影の帯)。帯はどれも body の内側 (各 x で、雲の上の縁から底までの間の決まった割合)。
// 塗りは 1 色ずつなので、薄い帯を重ねてなめらかな明るさの変わり方に見せる。
struct CloudShapes {
    ui::Path body;
    std::array<ui::Path, 3> light;  // 上の縁から雲の高さの 42% / 26% / 12% まで
    std::array<ui::Path, 4> shade;  // 底から雲の高さの 55% / 40% / 26% / 13% まで
};
CloudShapes Cumulus(Vec2 center, float width, uint32_t seed);
// `base` (足の真ん中) に立つ砂のお城。手で作る: 壁と塔 3 つ (少し傾いてふくらむ)、
// てっぺんの丸い塊、足元の砂の山 (だから、浜と一緒に塗ると、砂から生えているように見える)。
ui::Path SandCastle(Vec2 base, float height);
// `base` の砂に刺さって傾いた浜のパラソル: 傘 (ふちどりのあるドーム) と柄。
ui::Path BeachParasol(Vec2 base, float height, float degreesCcw);
// 右下の角の夏の影 (春の桜の木のように): 低い砂の小山に、パラソルが刺さり、
// そのそばに砂のお城。`base` = 小山の足の真ん中 (ウィンドウの下端上)、`height` = パラソルのてっぺんまで。
ui::Path BeachScene(Vec2 base, float height);

// --- 秋 -----------------------------------------------------------------------------------
// もみじの葉 (五つの尖った葉先と葉柄): radius = 中心から上の先まで。`points` = 縁を
// どれだけ細かく描くか (樹冠の小さな葉は少なめ)。
ui::Path MapleLeaf(Vec2 center, float radius, float degreesCcw, int points = 180);
// `window` の上に沿って濃く、`depth` くらいまで下に向かって薄くなる紅葉。4 色 (1 色に 1 パス)。
// `perPixel` = 幅 1 ピクセルあたりの葉の数。
std::array<ui::Path, 4> Canopy(const ui::Rect& window, float depth, float perPixel, uint32_t seed);

// --- 冬 -----------------------------------------------------------------------------------
// 雪の結晶: 太い腕 6 本 (それぞれ先は鋭い槍先)、各腕に太くとがった枝が 2 対、
// 中心に六角形。radius = 腕の先まで。
ui::Path Snowflake(Vec2 center, float radius, float degreesCcw);
// 降る雪を止めたもの: `area` に散らした、約 `size` の丸い雪片。
ui::Path Snowfall(const ui::Rect& area, int count, float size, uint32_t seed);
// `r` (ウィンドウ) の下に積もった雪: 下の端から最大 `depth` まで盛り上がる、なだらかに波打つ吹きだまり。
// 左右の端まで積もり (端で下がらない。端を 2 ピクセル越えるので、描く側がウィンドウの端で切る)、
// うねりの高さは r の左端からのピクセルだけで決まる: ウィンドウの幅や高さを変えても形は変わらず、
// 右に見える分が増えたり減ったりするだけ。高さは `depth` を越えない。
ui::Path SnowDrift(const ui::Rect& r, float depth);
// `window` の上の端からぶら下がるつららがところどころに: 260 .. 640 ピクセルごとに、雪の塊の下に 1 本から 3 本の
// 小さな 1 組。組はウィンドウの左端から並べ、内側で始まる組はすべて作る (右端を越えるものはそこで切るだけ)。
// だからウィンドウの大きさを変えても、出たり消えたりしない。つららはそれぞれ、小さな波 (`ice`) のある鋭い先に向かって
// 細くなり、左側に細いつや (`shine`) がある。`snow` = 雪の塊。
struct IcicleShapes {
    ui::Path snow, ice, shine;
};
IcicleShapes Icicles(const ui::Rect& window, float maxLength, uint32_t seed);

}  // namespace rm::theme_seasons::ornament
