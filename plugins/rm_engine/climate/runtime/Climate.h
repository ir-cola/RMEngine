#pragma once
// 気候 - シーンの設定「気候」(エリアの外の気温と湿り気) と「気候の地図」(真上から塗った気候)、
// 部品「気候エリア」(3D) /「気候エリア 2D」、場所ごとの気温と湿り気の計算。設計: docs/CLIMATE.md。
//
//   エリアの中での深さ (一番近い縁までの距離) -> 重み (境目の幅でなめらかに 0 から 1)。外の気候から始め、
//   エリアを優先度の小さい順に、重みの分だけそのエリアの値へ寄せる。3D は高い所ほど冷える。
//
// 計算は純粋 (tests/TestClimate.cpp)。シーンからエリアを集めるのは ClimateField (同じファイル)。

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::climate {

// シーン自身のもの (階層の一番上の行を選ぶとインスペクターに出る): 気候エリアの外の気候。
RM_STRUCT(Label="気候")
struct ClimateSettings {
    RM_PROPERTY(Edit, Label="気温 (℃)", Tooltip="気候エリアの外の気温。3D では「基準の高さ」での気温で、高い所ほど下がります", Min=-80, Max=80, Step=0.5, Decimals=1)
    float temperature = 15.f;
    RM_PROPERTY(Edit, Label="湿り気 (%)", Tooltip="気候エリアの外の湿り気。0: からから 〜 50: ふつう 〜 100: じめじめ", Min=0, Max=100, Step=1, Decimals=0)
    float humidity = 50.f;
    RM_PROPERTY(Edit, Label="高さで冷える (℃ / 100 m)", Tooltip="3D のシーン: 高い所ほど気温が下がります (0.6 が本物の空気くらい)。0: 下がらない。2D のシーンでは使いません", Min=0, Max=20, Step=0.1, Decimals=1)
    float lapse = 0.6f;
    RM_PROPERTY(Edit, Label="基準の高さ (m)", Tooltip="3D のシーン: この高さで、上の気温やエリアの気温になります", Min=-100000, Max=100000)
    float baseHeight = 0.f;
};

RM_ENUM(Label="エリアの形")
enum class ZoneShape {
    RM_ITEM(Label="箱") Box,
    RM_ITEM(Label="円柱") Cylinder,
};

RM_ENUM(Label="エリアの形 (2D)")
enum class ZoneShape2D {
    RM_ITEM(Label="長方形") Rect,
    RM_ITEM(Label="円") Circle,
};

// 3D の気候エリア: Transform の場所・向き・大きさで置く。
RM_STRUCT(Label="気候エリア")
struct ClimateZone {
    RM_PROPERTY(Edit, Label="形", Tooltip="円柱は縦 (Y) が高さ") ZoneShape shape = ZoneShape::Box;
    RM_PROPERTY(Edit, Label="大きさ (m)", Tooltip="箱: 幅・高さ・奥行き / 円柱: X が直径、Y が高さ。Transform の大きさを掛けます", Min=0, Max=1000000) Vec3 size{40, 40, 40};
    RM_PROPERTY(Edit, Label="境目の幅 (m)", Tooltip="縁からこの幅だけ内側へ、外の気候から少しずつこのエリアの気候に変わります。0: 縁で切り替わる", Min=0, Max=100000) float border = 10.f;
    RM_PROPERTY(Edit, Label="気温を変える") bool setTemperature = true;
    RM_PROPERTY(Edit, Label="気温 (℃)", Min=-80, Max=80, Step=0.5, Decimals=1, ShowIf="setTemperature") float temperature = -5.f;
    RM_PROPERTY(Edit, Label="湿り気を変える") bool setHumidity = false;
    RM_PROPERTY(Edit, Label="湿り気 (%)", Min=0, Max=100, Step=1, Decimals=0, ShowIf="setHumidity") float humidity = 50.f;
    RM_PROPERTY(Edit, Label="優先度", Tooltip="エリアが重なったとき、大きいほうを上に重ねます (寒い地方の中の、もっと寒い山頂)。同じなら階層の下のほうが上", Min=-1000, Max=1000) int priority = 0;
};

// 2D の気候エリア: Transform 2D の場所・向き・大きさで置く。
RM_STRUCT(Label="気候エリア 2D")
struct ClimateZone2D {
    RM_PROPERTY(Edit, Label="形") ZoneShape2D shape = ZoneShape2D::Rect;
    RM_PROPERTY(Edit, Label="広さ", Tooltip="長方形: 幅と高さ / 円: X が直径。Transform 2D の大きさを掛けます", Min=0, Max=1000000) Vec2 size{20, 20};
    RM_PROPERTY(Edit, Label="境目の幅", Tooltip="縁からこの幅だけ内側へ、外の気候から少しずつこのエリアの気候に変わります。0: 縁で切り替わる", Min=0, Max=100000) float border = 5.f;
    RM_PROPERTY(Edit, Label="気温を変える") bool setTemperature = true;
    RM_PROPERTY(Edit, Label="気温 (℃)", Min=-80, Max=80, Step=0.5, Decimals=1, ShowIf="setTemperature") float temperature = -5.f;
    RM_PROPERTY(Edit, Label="湿り気を変える") bool setHumidity = false;
    RM_PROPERTY(Edit, Label="湿り気 (%)", Min=0, Max=100, Step=1, Decimals=0, ShowIf="setHumidity") float humidity = 50.f;
    RM_PROPERTY(Edit, Label="優先度", Tooltip="エリアが重なったとき、大きいほうを上に重ねます。同じなら階層の下のほうが上", Min=-1000, Max=1000) int priority = 0;
};

// 気候の地図 (気候のタブで塗る) の、気候の種類 1 つ。塗ったマスは `id` を指す (並べ替えても消しても
// ほかの種類のマスは変わらない)。
RM_STRUCT(Label="気候の種類")
struct ClimateKind {
    RM_PROPERTY() int id = 1;  // 保存するが見せない: 塗ったマスが指すもの (地図の中で使い回さない)
    RM_PROPERTY(Edit, Label="名前") std::string name;
    RM_PROPERTY(Edit, Label="色", Tooltip="気候のタブの地図で、この気候を塗る色 (ゲームには出ません)") Color color{0.42f, 0.75f, 0.35f, 1.f};
    RM_PROPERTY(Edit, Label="気温 (℃)", Tooltip="この気候の気温。3D では「基準の高さ」での気温で、高い所ほど下がります", Min=-80, Max=80, Step=0.5, Decimals=1) float temperature = 15.f;
    RM_PROPERTY(Edit, Label="湿り気 (%)", Tooltip="この気候の湿り気。0: からから 〜 50: ふつう 〜 100: じめじめ", Min=0, Max=100, Step=1, Decimals=0) float humidity = 50.f;
};

// 地図に描く層: 気候の種類 / 雲の量 / 雨雲 (その場所だけ降る量)。
RM_ENUM(Label="地図の層")
enum class MapLayer {
    RM_ITEM(Label="気候") Climate,
    RM_ITEM(Label="雲") Clouds,
    RM_ITEM(Label="雨雲") Rain,
};

// 地図にペンで囲んだ形 (気候のタブ)。中のマスが、その層の値 (気候の種類 / 雲の量) になる。点は
// ワールドの (x, z)。筆で塗ったマスの上に、並びの順で重なる (あとのものが上)。
RM_STRUCT(Label="地図の形")
struct ClimateShape {
    RM_PROPERTY() MapLayer layer = MapLayer::Climate;
    RM_PROPERTY() int kind = 0;           // 気候の層: 種類 (ClimateKind::id)
    RM_PROPERTY() float clouds = 80.f;    // 雲の層: 雲の量 (%)
    RM_PROPERTY() float rain = 60.f;      // 雨雲の層: 降る量 (%。天候の降水量と同じ目盛り)
    RM_PROPERTY() std::vector<Vec2> points;  // (x, z)。3 点から形になる
};
bool operator==(const ClimateShape& a, const ClimateShape& b);

// 新しい地図に最初からある種類: 温帯 15 ℃ 50 % / 寒冷 -8 ℃ 60 % / 砂漠 32 ℃ 10 % / 熱帯 28 ℃ 85 %。
std::vector<ClimateKind> DefaultClimateKinds();
// 足した種類の色 (n 番目に足したもの。順に回る)。
Color ClimateKindColor(size_t n);

// シーン自身のもの (3D のシーン): 真上から見た地面に塗った気候 (気候のタブ)。ワールドに固定の
// マス目 (一辺 `cellSize` m) に、塗った所だけを持つ: 塗った所を囲む長方形 (最初のマス cellX / cellZ、
// 幅 width・奥行き depth マス) の中を、行 (z の小さい行から) ごとに続けて読んだ種類の番号を、
// 同じ番号の続き (ランレングス) で `cells` に書く。0 は塗っていない。地図の外と塗っていない所は
// シーンの「気候」(エリアの外の気候) のまま。気候エリアは、この上に重なる。
// 雲の量も同じマス目に塗る (cloud* : 値は 雲の量 + 1。0 は塗っていない = シーンの天気の雲のまま)。
// 雨雲も同じ (rain* : 値は 降る量 + 1。塗った所だけ、シーンの降水量にかかわらずその量が降る)。
// ペンで囲んだ形 (shapes) は、計算の前にマスに焼いて、筆の塗りの上に重ねる (BakedOf)。
RM_STRUCT(Label="気候の地図")
struct ClimateMap {
    RM_PROPERTY(Edit, Label="地図の境目の幅 (m)", Tooltip="気候のタブで塗った所の縁をはさんで、この幅でなめらかに気候が変わります。0: マスの縁で切り替わる。塗るのは「気候」のタブです", Min=0, Max=10000, Step=1, Decimals=1) float border = 8.f;
    RM_PROPERTY() float cellSize = 4.f;  // マスの一辺 (m)。気候のタブで変える (塗った所を描き直す)
    RM_PROPERTY() std::vector<ClimateKind> kinds = DefaultClimateKinds();
    RM_PROPERTY() int cellX = 0;
    RM_PROPERTY() int cellZ = 0;
    RM_PROPERTY() int width = 0;
    RM_PROPERTY() int depth = 0;
    RM_PROPERTY() std::string cells;  // ランレングス: "番号" か "番号*数" をカンマで区切る (EncodeRuns)
    RM_PROPERTY() int cloudX = 0;
    RM_PROPERTY() int cloudZ = 0;
    RM_PROPERTY() int cloudWidth = 0;
    RM_PROPERTY() int cloudDepth = 0;
    RM_PROPERTY() std::string cloudCells;  // 雲の量 + 1 のランレングス
    RM_PROPERTY() int rainX = 0;
    RM_PROPERTY() int rainZ = 0;
    RM_PROPERTY() int rainWidth = 0;
    RM_PROPERTY() int rainDepth = 0;
    RM_PROPERTY() std::string rainCells;  // 雨雲の降る量 + 1 のランレングス
    RM_PROPERTY() std::vector<ClimateShape> shapes;
};

// ある場所の気候。
struct ClimateHere {
    float temperature = 15.f;  // ℃
    float humidity = 50.f;     // %
    // 地図に塗った雲: まわりの (境目の幅の正方形の) うち雲を塗った割合 (0 .. 1) と、そこの雲の量の
    // 平均 (%)。その場所の雲の量 = (1 - cloudWeight) x シーンの雲の量 + cloudWeight x cloudAmount
    // (シーンの雲の量は天候のもの: 混ぜるのはつなぎ 天候 × 気候)。
    float cloudWeight = 0;
    float cloudAmount = 0;
    // 地図に描いた雨雲: まわりのうち雨雲の割合 (0 .. 1) と、そこの降る量の平均 (%)。その場所だけ降る量 =
    // rainWeight x rainAmount (シーンの降水量より多ければこちら: 混ぜるのはつなぎ 天候 × 気候)。
    float rainWeight = 0;
    float rainAmount = 0;
};

// --- 気候の地図 (純粋) -----------------------------------------------------------------------

inline constexpr float kMinCellSize = 0.5f, kMaxCellSize = 1000.f;
inline constexpr int kMaxPaintCells = 2048;  // 塗った所の長方形の一辺のマスの数の上限 (表の大きさを抑える)
inline constexpr size_t kMaxShapePoints = 4096;  // 1 つの形の点の数の上限

// 塗ったマスを広げたもの (地図の `cells` を読んだもの)。マスの番号はワールドのもの: floor(x / cellSize)。
struct PaintGrid {
    int cellX = 0, cellZ = 0;
    int width = 0, depth = 0;
    std::vector<int> ids;  // width * depth。行が z (z の小さい行から)。0: 塗っていない
    // ワールドのマス (x, z) の種類の番号 (長方形の外: 0)。
    int At(int x, int z) const;
    bool Empty() const { return ids.empty(); }
};
// 番号の並び -> "1*12,0*3,2" (続く同じ番号をまとめる)。
std::string EncodeRuns(std::span<const int> ids);
// その逆。`count` 個にそろえる (足りない分は 0、多い分は捨てる)。読めない所は 0。
std::vector<int> DecodeRuns(std::string_view text, size_t count);
// 層 `layer` の筆の塗り (気候: 種類の番号 / 雲: 雲の量 + 1 / 雨雲: 降る量 + 1)。
PaintGrid PaintOf(const ClimateMap& map, MapLayer layer = MapLayer::Climate);
// `grid` を地図の層 `layer` に書く (塗った所だけを囲む長方形に縮める。何も塗っていない: 幅 0)。
void SetPaint(ClimateMap& map, const PaintGrid& grid, MapLayer layer = MapLayer::Climate);
// 雲の量 (%) <-> 雲の層のマスの値 (雲の量 + 1。0 は塗っていない)。雨雲の層の降る量も同じ。
int CloudCellOf(float clouds);
float CloudsOfCell(int cell);
// 形のマスの値 (気候: 種類 (地図に無い種類: 0) / 雲: 雲の量 + 1 / 雨雲: 降る量 + 1)。
int ShapeCellOf(const ClimateMap& map, const ClimateShape& shape);
// 点 `points` (x, z) で囲んだ形の中に、真ん中が入るマス (偶奇の規則) を `id` にする。長方形は
// いるだけ広げる (PaintCircle と同じ上限)。変わったら true。3 点より少ないと何もしない。
bool FillShape(PaintGrid& grid, float cellSize, std::span<const Vec2> points, int id);
// 層 `layer` の、計算に使うマス: 筆の塗りの上に、その層の形を並びの順に焼いたもの。
PaintGrid BakedOf(const ClimateMap& map, MapLayer layer);
// 点 `p` が形の中か (偶奇の規則)。
bool InsideShape(std::span<const Vec2> points, Vec2 p);
// 真ん中 `center` (ワールドの x, z)・半径 `radius` (m) の円の中に、真ん中が入るマスを `id` にする
// (0: 消す)。円が小さくてもその真ん中のマス 1 つは塗る。長方形はいるだけ広げる (一辺 kMaxPaintCells
// まで。それより外は塗らない)。変わったら true。
bool PaintCircle(PaintGrid& grid, float cellSize, Vec2 center, float radius, int id);
// `from` m のマスで塗ったものを、`to` m のマスで塗り直す (新しいマスの真ん中が入る古いマスの番号)。
PaintGrid Resample(const PaintGrid& grid, float from, float to);
// 地図に足す種類の新しい番号 (今の一番大きいものの次)。
int NewKindId(const ClimateMap& map);
// 種類 `id` と、それで塗ったマスと、その種類の形を消す。
void RemoveKind(ClimateMap& map, int id);
const ClimateKind* FindKind(const ClimateMap& map, int id);

// 地図の塗った所の、それぞれの種類の割合の表 (積分画像 = summed-area table)。ある場所のまわりの
// 一辺「境目の幅」の正方形の中に、それぞれの種類で塗った面積がどれだけあるかを、マスの数に
// よらず 4 回ずつ表を引くだけで出す。1 つの地図から 1 回作り、何度でも聞ける。
class PaintTables {
public:
    explicit PaintTables(const ClimateMap& map);
    // 場所 `xz` (ワールドの x, z) の気候: 塗った割合 w の分だけ、その種類の気温と湿り気に寄せる
    // (残りの 1 - Σw は `outside`)。
    // 雲と雨雲も入れる (cloudWeight / cloudAmount / rainWeight / rainAmount)。
    ClimateHere Blend(ClimateHere outside, Vec2 xz) const;
    // それぞれの種類の割合 (地図の kinds の並び。塗っていない種類は 0)。
    std::vector<float> Weights(Vec2 xz) const;
    // 場所 `xz` のまわりの、一辺 2 x `half` m (< 0: 境目の幅) の正方形の中の雲 (割合と、塗った所の平均)。
    void Clouds(Vec2 xz, float half, float& weight, float& amount) const;
    // 同じく雨雲 (割合と、雨雲の所の降る量の平均)。
    void Rain(Vec2 xz, float half, float& weight, float& amount) const;
    bool Empty() const { return kinds_.empty() && clouds_.count.sums.empty() && rain_.count.sums.empty(); }
    bool HasClouds() const { return !clouds_.count.sums.empty(); }
    bool HasRain() const { return !rain_.count.sums.empty(); }
    // 雨雲の降る量の一番多いもの (%。雨雲が無い: 0)。天候が粒をどれだけ用意するか。
    float MostRain() const { return mostRain_; }

private:
    // 積分画像: (width + 1) x (depth + 1)。行 < j、列 < i の中のマスの値の和。
    struct Table {
        int cellX = 0, cellZ = 0, width = 0, depth = 0;
        std::vector<int32_t> sums;
        // 長方形 (ワールドのマスの単位 u0 .. u1, v0 .. v1) の中の値の和 (端数のマスは面積の分)。
        double Area(double u0, double v0, double u1, double v1) const;
        double Sum(double u, double v) const;  // (長方形の角から)
    };
    static Table MakeTable(const PaintGrid& g, const std::function<int(int)>& value);
    // 量の層 (雲 / 雨雲): 焼いたマスと、塗ったマスの数と量の和の表。
    struct AmountLayer {
        PaintGrid grid;
        Table count, sum;
    };
    static AmountLayer MakeAmount(PaintGrid grid);
    void AmountAt(const AmountLayer& layer, Vec2 xz, float half, float& weight, float& amount) const;
    struct Kind {
        size_t index = 0;  // 地図の kinds の中
        int id = 0;
        float temperature = 0, humidity = 0;
        Table table;  // この種類のマスの数
    };
    float cellSize_ = 4, half_ = 0;  // 正方形の半分の一辺 (m) = 境目の幅 / 2
    size_t kindCount_ = 0;
    PaintGrid grid_;  // (境目の幅 0: そのマスだけを見る)
    std::vector<Kind> kinds_;
    AmountLayer clouds_, rain_;
    float mostRain_ = 0;
};
// 地図 `map` の表 (同じ中身の地図には、作ってある表を使い回す: 天候は 1 コマに何十回も ClimateField
// を作るため)。塗った所が無ければ null。
std::shared_ptr<const PaintTables> PaintTablesOf(const ClimateMap& map);

// --- 純粋な計算 ------------------------------------------------------------------------------

// エリアの形 (エリアの向きの、大きさを掛けた後の半分の大きさ。単位はワールド)。
struct ZoneBody {
    enum class Kind { Box, Cylinder, Rect, Circle } kind = Kind::Box;
    Vec3 half{0, 0, 0};  // 円柱・円: x が半径、y が高さの半分 (円柱)
};
// エリアの向きでの点 `local` (中心から) が、一番近い縁からどれだけ内側か (外: 0 以下)。
float ZoneDepth(const ZoneBody& body, Vec3 local);
// 深さ -> 重み (0 .. 1): 縁で 0、境目の幅で 1 (なめらかな段)。幅 0: 中なら 1。
float ZoneWeight(float depth, float border);

// 1 つのエリアがその場所にすること。
struct ZoneEffect {
    float weight = 0;  // 0 .. 1
    int priority = 0;
    bool setTemperature = false, setHumidity = false;
    float temperature = 0, humidity = 0;
};
// 外の気候 `outside` に、エリアを優先度の小さい順 (同じなら並びの順) に重ねる。
ClimateHere Blend(ClimateHere outside, std::span<const ZoneEffect> zones);
// 高さで冷える (3D): (高さ - 基準の高さ) / 100 x 下がる量 を引く。
float CooledByHeight(float temperature, float height, const ClimateSettings& s);

// --- シーンから ------------------------------------------------------------------------------

// シーンの気候エリアを、置かれた所 (親を含む) と一緒に集めたもの。1 回集めて、何度でも聞ける
// (天候は 1 コマに何十か所も聞く)。切ってある (自分か親が) エリアは入らない。
// 3D の場所は: エリアの外の気候 -> 気候の地図で塗った所 (下の層) -> 気候エリア (上に重ねる) -> 高さで冷える。
class ClimateField {
public:
    explicit ClimateField(const game_production::Scene& scene);
    // 3D の場所 (気候の地図・3D のエリアと、高さで冷える)。地図は (x, z) で引く。
    ClimateHere At(Vec3 position) const;
    // 地図に塗った雲だけ (ワールドの x, z)。`half`: 見る正方形の半分の一辺 (m。< 0: 地図の境目の幅の半分)。
    // 雲を塗っていない: weight 0。
    void CloudsAt(Vec2 xz, float half, float& weight, float& amount) const;
    // 地図に雲を塗った所があるか。
    bool HasClouds() const { return paint_ && paint_->HasClouds(); }
    // 地図に描いた雨雲だけ (CloudsAt と同じ)。雨雲が無い: weight 0。
    void RainAt(Vec2 xz, float half, float& weight, float& amount) const;
    bool HasRain() const { return paint_ && paint_->HasRain(); }
    float MostRain() const { return paint_ ? paint_->MostRain() : 0.f; }
    // 2D の場所 (2D のエリアだけ。高さでは冷えない)。
    ClimateHere At2D(Vec2 position) const;
    // エリアの外の気候 (シーンに設定が無ければ既定の値)。
    const ClimateSettings& Settings() const { return settings_; }

    struct Zone3D {
        int entity = 0;
        space3d::Mat4 world;  // エンティティのワールド
        ClimateZone zone;
    };
    struct Zone2D {
        int entity = 0;
        space2d::Mat3 world;
        ClimateZone2D zone;
    };
    std::span<const Zone3D> Zones3D() const { return zones3_; }
    std::span<const Zone2D> Zones2D() const { return zones2_; }

private:
    ClimateSettings settings_;
    std::shared_ptr<const PaintTables> paint_;  // 気候の地図 (塗った所が無い: null)
    std::vector<Zone3D> zones3_;
    std::vector<Zone2D> zones2_;
};

// エリアの形 (ワールドの長さ) と、ワールドの点をエリアの向き (中心から、ワールドの長さ) に直したもの。
ZoneBody BodyOf(const ClimateZone& zone, const space3d::Mat4& world);
ZoneBody BodyOf(const ClimateZone2D& zone, const space2d::Mat3& world);
Vec3 LocalOf(const space3d::Mat4& world, Vec3 point);
Vec3 LocalOf(const space2d::Mat3& world, Vec2 point);

}  // namespace rm::climate
