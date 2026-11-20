#pragma once
// ゲーム制作 × 3D - ライトの部品: 平行光 (太陽のような directional)、点光源 (電球のような
// point)、スポット (懐中電灯のような円錐)。その物自身の +Z に沿って照らす (Transform の回転で
// 向きが変わる。点光源はどちらにも照らす)。シーンビューとゲームビューはこれでシーンを照らす。
// 無いときは、上からの白い平行光 1 つ (3D 空間の既定、Lighting.h)。描くのは最大 8 つ。影は、
// 影を付ける最初の平行光と、マップが続く間の点光源 / スポット (Lighting.h)。
// 純粋 - tests/TestSpace3d.cpp でテストする。

#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

RM_ENUM(Label="光の種類")
enum class LightKind {
    RM_ITEM(Label="平行光") Directional,
    RM_ITEM(Label="点光源") Point,
    RM_ITEM(Label="スポット") Spot,
};

RM_STRUCT(Label="光")
struct Light {
    RM_PROPERTY(Edit, Label="種類", Tooltip="平行光: 太陽のように全体へ同じ向きで (位置は関係ない) / 点光源: 電球のように周りへ / スポット: 懐中電灯のように円すいの中へ")
    LightKind kind = LightKind::Directional;
    RM_PROPERTY(Edit, Label="色", Min=0, Max=1) Color color{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="強さ", Min=0, Max=100, Step=0.1) float intensity = 1.f;
    RM_PROPERTY(Edit, Label="届く距離", Tooltip="点光源とスポット。ここで光が 0 になります", Min=0.01, Max=100000) float range = 10.f;
    RM_PROPERTY(Edit, Label="円すいの角度 (度)", Tooltip="スポットのとき。光の広がりの全体の角度", Min=1, Max=179) float spotAngle = 30.f;
    RM_PROPERTY(Edit, Label="影を落とす", Tooltip="平行光は、影を落とす平行光の 1 つ目だけが影を出します。点光源とスポットは影の地図 8 枚を階層の順に使います (点光源は 6 枚、スポットは 1 枚)。足りない光は影なし") bool castShadows = true;
};

// レンダラーが受け取る形のライト。そのエンティティのワールド行列で置く (+Z に沿って照らす)。
space3d::Light ToRenderLight(const Light& light, const space3d::Mat4& world);

// 環境光: どのライトがあっても、すべての面が受ける光 - 上を向く面には空の色、下を向く面には
// 地面の色。これを持つ最初の 有効 なエンティティが決める (無ければ 3D 空間の既定。設定できる
// ようになる前の見た目)。そのエンティティがどこにあるかは関係ない。
RM_STRUCT(Label="環境光")
struct AmbientLight {
    RM_PROPERTY(Edit, Label="上からの色", Tooltip="上を向いた面に当たる光 (空の明るさ)", Min=0, Max=1) Color sky{0.16f, 0.16f, 0.16f, 1};
    RM_PROPERTY(Edit, Label="下からの色", Tooltip="下を向いた面に当たる光 (地面からの照り返し)", Min=0, Max=1) Color ground{0.08f, 0.08f, 0.08f, 1};
    RM_PROPERTY(Edit, Label="強さ", Min=0, Max=10, Step=0.05) float intensity = 1.f;
};

space3d::Ambient ToRenderAmbient(const AmbientLight& ambient);

}  // namespace rm::game_x_3d
