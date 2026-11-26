#pragma once
// 季節テーマパック - 季節の絵の形 (桜の木、太陽、もみじの葉、雪の結晶、雲、下に積もる雪 ...)。
// 純粋: ui::Path を作るだけ (tests/TestTheme.cpp でテストする)。Seasons.cpp が季節の色で、
// 内容の後ろに描く。
//
// どの図形も同じ向き (画面上で時計回り) にまわり、描く側は巻き数で塗るので、重なった図形
// (木の樹冠の円) は 1 つの影になり、穴はできない。
// 「ランダム」な散らばりは固定した種から作る: 毎フレーム同じ絵になる。

#include "ui/Path.h"
#include "ui/Shape.h"
#include "ui/Types.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace rm::theme_seasons::ornament {

// --- 部品 (核の ui/Shape.h。前はここにあった) ------------------------------------------------
using ui::shape::AddBar;
using ui::shape::AddCircle;
using ui::shape::AddEllipse;
using ui::shape::AddPolygon;
using ui::shape::FitInto;
using ui::shape::Random;
using ui::shape::Rotate;

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
ui::Path BeachScene(Vec2 base, float height, bool parasol = true);  // (parasol: false = パラソル無し。夏の夜)
// 右下の角の秋の影: ススキのひとむら。茎と細長い葉は根元からほぼまっすぐ、左へ少し傾いて立ち、
// 上で左へ曲がる。穂は茎の先から左下へ垂れ、中心の軸から細い小穂が何本も、同じ向きに流れて垂れる。
// `base` = 根元の真ん中 (ウィンドウの下端上)、`height` = 一番高い穂まで。
ui::Path Susuki(Vec2 base, float height, uint32_t seed);

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
// 降る雪を止めたもの: `area` に散らした、約 `size` の丸い雪片 (ui::shape::Dots)。
ui::Path Snowfall(const ui::Rect& area, int count, float size, uint32_t seed);
// `r` (ウィンドウ) の下に積もった雪: 下の端から最大 `depth` まで盛り上がる、なだらかに波打つ吹きだまり。
// 左右の端まで積もり (端で下がらない。端を 2 ピクセル越えるので、描く側がウィンドウの端で切る)、
// うねりの高さは r の左端からのピクセルだけで決まる: ウィンドウの幅や高さを変えても形は変わらず、
// 右に見える分が増えたり減ったりするだけ。高さは `depth` を越えない。
ui::Path SnowDrift(const ui::Rect& r, float depth);
// その積もった雪の、左端から `l` ピクセルの所の高さ (下の辺から上へ。depth の 0.46 .. 0.98 倍)。
float SnowDriftHeight(float l, float depth);
// 雪だるま: 白い丸 2 つ (下が大きい) と、頭に少し傾けてかぶった赤いバケツ。
// `base` = 下の丸の底 (雪の上)、`height` = 底からバケツのてっぺんまで。`grow`: 丸を太らせる量
// (輪郭: 太らせた丸を輪郭の色で先に描き、その上に白い丸)。
struct SnowmanShapes {
    ui::Path body;    // 丸 2 つ
    ui::Path bucket;  // バケツ
    ui::Path rim;     // バケツの縁 (少し濃く)
};
SnowmanShapes Snowman(Vec2 base, float height, float grow = 0);
// `window` の上の端からぶら下がるつららがところどころに: 260 .. 640 ピクセルごとに、雪の塊の下に 1 本から 3 本の
// 小さな 1 組。組はウィンドウの左端から並べ、内側で始まる組はすべて作る (右端を越えるものはそこで切るだけ)。
// だからウィンドウの大きさを変えても、出たり消えたりしない。つららはそれぞれ、小さな波 (`ice`) のある鋭い先に向かって
// 細くなり、左側に細いつや (`shine`) がある。`snow` = 雪の塊。
struct IcicleShapes {
    ui::Path snow, ice, shine;
};
IcicleShapes Icicles(const ui::Rect& window, float maxLength, uint32_t seed);

// --- 夜 (2026-11-26: 季節の夜用のテーマ) ---------------------------------------------------------
// 花見団子: 斜めの串の上の端に寄せた丸い団子が 3 つ (下は持つ所)。radius = 中心から串の端まで。`degreesCcw` 回す (0: 串がまっすぐ上を向く)。
ui::Path HanamiDango(Vec2 center, float radius, float degreesCcw);
// 上の端に沿ってたるむ紐 (提灯やクリスマスの電球を下げる): `window` の左端から `swing` ピクセルごとに上の端に留め、
// 留めた所の間で `sag` だけ下へたるむ。左端から並べるので、ウィンドウの大きさを変えても形は変わらない (右の端で切れる)。
// `points` = たるみ 1 つに `perSwing` 個ずつ、紐の上の下げる所。紐の太さは `width`。
struct GarlandShapes {
    ui::Path cord;
    std::vector<Vec2> points;
};
GarlandShapes Garland(const ui::Rect& window, float swing, float sag, int perSwing, float width);
// 提灯 (ちょうちん): `hang` (紐の所) から下がる。上と下の黒い枠 (`caps`) と、ふくらんだ紙の胴 (`body`)。`height` = 全体の高さ。
void AddLantern(ui::Path& body, ui::Path& caps, Vec2 hang, float height);
// 月見団子: 三方 (台) の上に団子を 3・2・1 と積んだもの。radius = 中心から上の団子のてっぺんまで。
ui::Path TsukimiDango(Vec2 center, float radius);
// クリスマスツリー: 3 段の三角の葉、幹、てっぺんの星。radius = 中心から星の先まで。
ui::Path ChristmasTree(Vec2 center, float radius, float degreesCcw);

// --- 2026-11-26 の 2 回目 -------------------------------------------------------------------------
// 風に吹かれて飛ぶ桜の花びら: 風上の上の角から、いくつかの風の道すじ (うねりながら少し下がる) に沿って、風下へ
// 散りながら流れる (風上ほど多い)。花びらの長い向きは風の向き。`wind` > 0: 左から右へ (春の朝)、< 0: 右から左へ
// (春の夜)。`petals` は少しずつ違う 4 色、`streaks` は道すじに沿った細い風の筋。`depth` = 下へどこまで流れるか。
struct PetalWindShapes {
    std::array<ui::Path, 4> petals;
    ui::Path streaks;
};
PetalWindShapes PetalWind(const ui::Rect& window, float depth, float perPixel, float wind, uint32_t seed);
// 打ち上げ花火 1 発 (リアル調): 中心のまわりの光 (`glow`: 外 → 内の 2 枚。重ねて塗る)、光の筋 (`trails`:
// 中心の近くは細く消え、外ほど太い。重さで少し垂れる)、筋の先の星ときらめく粒 (`tips`)。
// 菊 = 放射の筋と先の星 / 柳 = 金色の長い筋が大きく垂れる / 牡丹 = 筋の無い光の粒の 2 重の球。
enum class FireworkKind { Chrysanthemum, Willow, Peony };
struct FireworkShapes {
    std::array<ui::Path, 2> glow;
    ui::Path trails, tips;
};
FireworkShapes FireworkShell(Vec2 center, float radius, FireworkKind kind, uint32_t seed);
// かまくら: `base` (地面の真ん中) に立つ雪のドーム (底の幅 1.6 h、高さ h)、手前の左寄りのアーチの入り口 (`door`)、
// その中の灯り (`light`)。`grow` = ドームを太らせる量 (輪郭)。
struct KamakuraShapes {
    ui::Path dome, door, light;
};
KamakuraShapes Kamakura(Vec2 base, float height, float grow = 0);
// 左へ飛ぶサンタクロースとトナカイ 2 頭のシルエット (駆けるトナカイ・手綱・そり・サンタ・袋)。`size` = 全長の半分ほど。
// `degreesCcw` 回す (負: 左が上がる = 左上へ飛ぶ)。
ui::Path SantaSleigh(Vec2 center, float size, float degreesCcw);
// 左下の影の案 (見本にして選んでもらう。radius = 中心から一番遠い所まで、だいたい):
ui::Path Bonbori(Vec2 center, float radius, float degreesCcw);      // 春の夜: ぼんぼり (屋根・紙のほや・柱・台)
ui::Path SakuraBranch(Vec2 center, float radius, float degreesCcw); // 春の夜: 花の付いた桜の枝
ui::Path TokkuriSakazuki(Vec2 center, float radius);                // 春の夜: 徳利と、桜の花が浮かぶ盃
ui::Path Goldfish(Vec2 center, float radius, float degreesCcw);     // 夏の夜: 金魚 (左を向く。ひらひらの尾)
ui::Path Furin(Vec2 center, float radius);                          // 夏の夜: 風鈴 (糸・ガラスの鉢・短冊)
ui::Path Kakigori(Vec2 center, float radius);                       // 夏の夜: かき氷 (氷の山・足の付いた器・さじ)
ui::Path MosquitoCoil(Vec2 center, float radius);                   // 夏の夜: 蚊取り線香 (渦巻きと煙)
// 夏の夜の別案 (2026-11-26 の 3 回目)。種・ビー玉・模様は穴 (反時計回りの図形) であける: 1 色でも何か分かる。
ui::Path SenkoHanabi(Vec2 center, float radius);  // 線香花火 (こよりの持ち手・火の玉・松葉のように枝分かれする火花)
ui::Path Ramune(Vec2 center, float radius);       // ラムネの瓶 (口・ビー玉のふくらみ・くびれ・胴。ビー玉と丸いラベルは穴)
ui::Path Watermelon(Vec2 center, float radius);   // 半月に切ったスイカ (皮との境目の線と種は穴)
ui::Path WaterYoyo(Vec2 center, float radius);    // 水ヨーヨー (風船・結び目・ゴムひもと指の輪。模様の帯は穴)

}  // namespace rm::theme_seasons::ornament
