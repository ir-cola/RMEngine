#pragma once
// 物理 - 部品 (データだけ: 物理ワールドが固定ステップごとに読む)。設計: TestEngine
// NewEngineBluePrint 5 (Rigidbody / Collider は部品)、9-f (設定だけ。物体は物理ワールドのもので、保存しない)。
//
//   剛体 (Rigidbody)      どう動くか: 動く (重力があり、他に押される) / 動かない / キネマティック (Transform
//                         でだけ動かし、他を押す); 質量、摩擦、反発、重力、回転の固定
//   コライダー (Collider)  3D での形: 箱 / 球 / カプセル / メッシュ (その メッシュ部品 のモデル: 動かない
//                         ものだけ)、トリガー (知らせるだけで、止めない)、エンティティから動かす
//   コライダー (2D)        平面での形: 箱 / 円 / カプセル - 平面に留まる物体 (Z は無く、Z まわりにだけ
//                         回る)
//   キャラクター          物体ではなく、歩く (坂、段差、地面) キャラクター
//
// コライダーがあって 剛体 が無いエンティティは、動かない物体 (床、壁)。大きさは、エンティティの
// Transform の拡縮 (ワールドでの拡縮: 親の分も含む) の倍。単位: m、kg、s。
// 純粋なデータ - 物理ワールド (PhysicsWorld.h) がこれから物体を作る。

#include "core/reflect/Reflect.h"

namespace rm::physics {

RM_ENUM(Label="動き方")
enum class Motion {
    RM_ITEM(Label="動く") Dynamic,
    RM_ITEM(Label="動かない") Static,
    RM_ITEM(Label="キネマティック") Kinematic,
};

// シーン自身のもの (game_production::SceneSettingInfo: シーン - 階層の一番上の行 - を選ぶと
// インスペクターに出る)。シーンごとに自分の重力を持つ。(2026-11-17 より前はゲーム設定の 重力 だった:
// その値はプロジェクトファイルに残るが、使わない。)
RM_STRUCT(Label="物理")
struct PhysicsSettings {
    RM_PROPERTY(Edit, Label="重力", Tooltip="m/s²。世界の向きで (Y が上)。2D のゲームは X と Y を使います") Vec3 gravity{0, -9.81f, 0};
};

RM_STRUCT(Label="剛体")
struct Rigidbody {
    RM_PROPERTY(Edit, Label="動き方", Tooltip="動く: 重力で落ち、ぶつかると押される / 動かない: 床や壁 / キネマティック: Transform (スクリプト) で動かし、ほかを押す")
    Motion motion = Motion::Dynamic;
    RM_PROPERTY(Edit, Label="質量 (kg)", Min=0.001, Max=1000000) float mass = 1.f;
    RM_PROPERTY(Edit, Label="摩擦", Tooltip="0: つるつる 〜 1: すべりにくい (ぶつかった 2 つを掛けた値の平方根)", Min=0, Max=10) float friction = 0.5f;
    RM_PROPERTY(Edit, Label="跳ね返り", Tooltip="0: 跳ねない 〜 1: そのまま跳ね返る (2 つの大きい方)", Min=0, Max=1) float bounciness = 0.f;
    RM_PROPERTY(Edit, Label="重力の倍率", Tooltip="シーンの「重力」に掛けます (0: 浮く)", Min=-100, Max=100) float gravityScale = 1.f;
    RM_PROPERTY(Edit, Label="回転の固定 X", Icon="RotationLock") bool lockRotationX = false;
    RM_PROPERTY(Edit, Label="回転の固定 Y", Icon="RotationLock") bool lockRotationY = false;
    RM_PROPERTY(Edit, Label="回転の固定 Z", Icon="RotationLock", Tooltip="2D の物は Z だけが回るので、これで回らなくなります") bool lockRotationZ = false;
};

RM_ENUM(Label="形")
enum class Shape {
    RM_ITEM(Label="箱") Box,
    RM_ITEM(Label="球") Sphere,
    RM_ITEM(Label="カプセル") Capsule,
    RM_ITEM(Label="メッシュ") Mesh,
    RM_ITEM(Label="円柱") Cylinder,
};

RM_STRUCT(Label="コライダー")
struct Collider {
    RM_PROPERTY(Edit, Label="形", Tooltip="メッシュ: 同じエンティティのメッシュ (3D モデル) の形。動かない物だけ (剛体が無いか「動かない」)")
    Shape shape = Shape::Box;
    RM_PROPERTY(Edit, Label="箱の大きさ", Tooltip="箱の幅・高さ・奥行き (Transform の大きさを掛けます)", Min=0.001, Max=100000) Vec3 size{1, 1, 1};
    RM_PROPERTY(Edit, Label="半径", Tooltip="球・カプセル・円柱", Min=0.001, Max=100000) float radius = 0.5f;
    RM_PROPERTY(Edit, Label="高さ", Tooltip="カプセル: 端から端まで (Y の向き。半径の 2 倍より短いと球)。円柱: 上の面から下の面まで", Min=0.001, Max=100000) float height = 2.f;
    RM_PROPERTY(Edit, Label="ずらし", Tooltip="エンティティの中心から形の中心まで (エンティティの向きで)") Vec3 offset{0, 0, 0};
    RM_PROPERTY(Edit, Label="トリガー", Tooltip="ぶつからずに通り抜け、出入りをスクリプトに知らせます (OnTriggerEnter / OnTriggerExit)") bool trigger = false;
};

RM_ENUM(Label="形 (2D)")
enum class Shape2D {
    RM_ITEM(Label="箱") Box,
    RM_ITEM(Label="円") Circle,
    RM_ITEM(Label="カプセル") Capsule,
};

RM_STRUCT(Label="コライダー (2D)")
struct Collider2D {
    RM_PROPERTY(Edit, Label="形") Shape2D shape = Shape2D::Box;
    RM_PROPERTY(Edit, Label="箱の大きさ", Tooltip="箱の幅と高さ (Transform 2D の大きさを掛けます)", Min=0.001, Max=100000) Vec2 size{1, 1};
    RM_PROPERTY(Edit, Label="半径", Tooltip="円とカプセル", Min=0.001, Max=100000) float radius = 0.5f;
    RM_PROPERTY(Edit, Label="高さ", Tooltip="カプセルの端から端まで (Y の向き)", Min=0.001, Max=100000) float height = 2.f;
    RM_PROPERTY(Edit, Label="ずらし", Tooltip="エンティティの中心から形の中心まで (エンティティの向きで)") Vec2 offset{0, 0};
    RM_PROPERTY(Edit, Label="トリガー", Tooltip="ぶつからずに通り抜け、出入りをスクリプトに知らせます (OnTriggerEnter / OnTriggerExit)") bool trigger = false;
};

RM_STRUCT(Label="キャラクター")
struct Character {
    RM_PROPERTY(Edit, Label="半径", Tooltip="体 (カプセル) の太さ", Min=0.01, Max=100) float radius = 0.3f;
    RM_PROPERTY(Edit, Label="高さ", Tooltip="体の足から頭まで。エンティティの位置が足元", Min=0.05, Max=100) float height = 1.8f;
    RM_PROPERTY(Edit, Label="登れる坂 (度)", Tooltip="これより急な坂は登れず、すべり落ちます", Min=0, Max=89) float maxSlope = 45.f;
    RM_PROPERTY(Edit, Label="登れる段差", Tooltip="この高さまでの段は歩いて上がります", Min=0, Max=10) float stepHeight = 0.3f;
    RM_PROPERTY(Edit, Label="重力の倍率", Min=-100, Max=100) float gravityScale = 1.f;
    RM_PROPERTY(Edit, Label="質量 (kg)", Tooltip="ほかの物を押す強さ", Min=0.1, Max=100000) float mass = 70.f;
};

}  // namespace rm::physics
