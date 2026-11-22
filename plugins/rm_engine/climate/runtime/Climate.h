#pragma once
// 気候 - シーンの設定「気候」(エリアの外の気温と湿り気) と、部品「気候エリア」(3D) /「気候エリア 2D」、
// 場所ごとの気温と湿り気の計算。設計: docs/CLIMATE.md。
//
//   エリアの中での深さ (一番近い縁までの距離) -> 重み (境目の幅でなめらかに 0 から 1)。外の気候から始め、
//   エリアを優先度の小さい順に、重みの分だけそのエリアの値へ寄せる。3D は高い所ほど冷える。
//
// 計算は純粋 (tests/TestClimate.cpp)。シーンからエリアを集めるのは ClimateField (同じファイル)。

#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <span>
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

// ある場所の気候。
struct ClimateHere {
    float temperature = 15.f;  // ℃
    float humidity = 50.f;     // %
};

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
class ClimateField {
public:
    explicit ClimateField(const game_production::Scene& scene);
    // 3D の場所 (3D のエリアと、高さで冷える)。
    ClimateHere At(Vec3 position) const;
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
    std::vector<Zone3D> zones3_;
    std::vector<Zone2D> zones2_;
};

// エリアの形 (ワールドの長さ) と、ワールドの点をエリアの向き (中心から、ワールドの長さ) に直したもの。
ZoneBody BodyOf(const ClimateZone& zone, const space3d::Mat4& world);
ZoneBody BodyOf(const ClimateZone2D& zone, const space2d::Mat3& world);
Vec3 LocalOf(const space3d::Mat4& world, Vec3 point);
Vec3 LocalOf(const space2d::Mat3& world, Vec2 point);

}  // namespace rm::climate
