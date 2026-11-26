#pragma once
// 物理 - コライダーがワールドのどこにあってどれだけ大きいか (エンティティのワールドの拡大率を取り込む) を 1 か所に書く。
// 物理ワールドはこれから物体の形を作り、シーンビューはこれから線を描く。
// 純粋 (I/O も描画もしない)。tests/TestPhysics.cpp でテストする。

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/Primitives.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <optional>
#include <vector>

namespace rm::physics {

// エンティティのいる所: ワールド行列 (3D か平面上) と、親のもの (書き戻し用)。
struct EntityPlace {
    bool flat = false;  // 平面上 (Transform 2D)
    space3d::Mat4 world3, parent3;
    space2d::Mat3 world2, parent2;
    int parentOrder = 0;  // 2D: 親たちの 並び順 を足したもの
    int order = 0;        // ... と自分の分を足したもの
};
// 親の位置 `up` (null: 一番上) の下にある `e` の位置。false: 自分の位置は持たない
// (Transform / Transform 2D がない): そのとき `out` は子のいる所 (親の位置) になる。
bool PlaceUnder(const game_production::SceneEntity& e, const EntityPlace* up, EntityPlace& out);

// 分解したワールド行列: どこに、どの向きに (純粋な回転、列の長さ 1)、どれだけ大きいか。
struct Pose {
    Vec3 position{0, 0, 0};
    space3d::Mat4 turn;  // 回転だけ
    Vec3 scale{1, 1, 1};
};
Pose PoseOf(const space3d::Mat4& world);
// 平面上の 2D のワールド行列: z = 0 で、Z まわりに回転する。
Pose PoseOf(const space2d::Mat3& world);
// 2D の姿勢の、Z まわりの回転の角度 (度、反時計回りが +)。
float AngleOf(const Pose& pose);

// 物理が作るコライダーの形: ワールドでの半分の大きさ (エンティティの拡大率を取り込む) と、
// エンティティから見た中心の位置 (エンティティの回した軸で)。
struct ShapeDims {
    enum class Kind { Box, Sphere, Capsule, Mesh, Cylinder, Cone, Pyramid } kind = Kind::Box;
    Vec3 half{0.5f, 0.5f, 0.5f};  // 箱: 各軸の大きさの半分。2D の箱もこれ (z: kPlaneHalfDepth)。四角錐: 底の幅・高さ・奥行きの半分
    float radius = 0.5f;           // 球 / カプセル / 円 / 円柱 / 円錐 (底)
    float halfHeight = 0.5f;       // カプセル: まっすぐな部分 (Y) の半分。0: 球。円柱・円錐: 高さの半分 (円錐の先は +Y)
    Vec3 offset{0, 0, 0};          // エンティティから見たもの。拡大率をかけてある
    Vec3 meshScale{1, 1, 1};       // メッシュ: エンティティの拡大率 (モデル自身の点にこれをかける)
};
// 2D の物体はこの厚さ (半分: Z -0.5 .. 0.5)。どれも平面 z = 0 の上にあるので、平らなもの同士のように当たる。
inline constexpr float kPlaneHalfDepth = 0.5f;
ShapeDims DimsOf(const Collider& c, Vec3 scale);
ShapeDims DimsOf(const Collider2D& c, Vec3 scale);
// キャラクターの体: エンティティの位置 (足元) に立つカプセル。
ShapeDims DimsOf(const Character& c);

// `pose` に置いた形の線 (点の組): シーンビューが描く。
struct Segment {
    Vec3 a, b;
};
void Outline(const ShapeDims& dims, const Pose& pose, bool flat, std::vector<Segment>& out);

// 四角錐の 5 つの角 (底の 4 つ、先): 物理はこれを包む凸の形 (凸包) で当てる。形の空間 (ずらす前)。
std::vector<Vec3> PyramidPoints(const ShapeDims& dims);

// --- メッシュ (3D モデル) の部品との組 (2026-11-26) -------------------------------------------------
// 物理 は ゲーム制作 × 3D を必要としないので、メッシュの部品 (rm::game_x_3d::MeshRenderer) は型の名前と
// フィールドの名前 ("shape" の列挙の名前と "model") で読む。
struct MeshSource {
    bool present = false;                       // メッシュの部品がある
    std::optional<space3d::Shape> primitive;    // ファイル無しの形 (箱 / 球 / 円錐 ...)。無し: モデル
    AssetRef model;                             // モデルのとき
};
MeshSource MeshOf(const game_production::SceneEntity& e);
// メッシュの「形」の列挙の名前 ("Box" / "Cone" ...) -> 形。"Model" や知らない名前: 無し。
std::optional<space3d::Shape> PrimitiveNamed(std::string_view name);

// ファイル無しの形 `primitive` (Primitives.h の大きさ) にぴったりのコライダーの形: 箱 -> 箱、円錐 -> 円錐 ...
// 平面 -> 箱 (薄い板)。
Shape ColliderShapeFor(space3d::Shape primitive);
// コライダーの形を `shape` にし、大きさとずらしを形 `primitive` に合わせる (同じ形ならぴったり。違う形なら
// 包む大きさ: 球は一番長い半分、円柱・円錐・カプセルは横の半径と高さ)。メッシュは大きさを持たない
// (そのまま)。トリガーは変えない。追加 ▸ 3D オブジェクトと、インスペクターで形を選び直したときに使う。
void FitCollider(Collider& c, Shape shape, space3d::Shape primitive);

}  // namespace rm::physics
