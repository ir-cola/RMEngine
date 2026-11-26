#pragma once
// ゲーム制作 × 3D - 3D モデル (形はファイルから) として描くエンティティ。Transform で置く。
// モデルが無い (または読めない) ときは、これまでどおり箱で描く。または、ファイルなしで作る
// 単純な形 (形: 箱 / 球 / 円柱 / カプセル / 平面 / 円錐 / 四角錐 - 3D 空間の Primitives.h、大きさはそこ):
// 階層の 追加 ▸ 3D オブジェクト が、これらのエンティティを作る。
//
// `model` はアセットの参照 (ファイルの .meta にある id): アセット パネルや Explorer で
// ファイルを動かしたり名前を変えたりしても失われない。参照より前のデータはプロジェクト内の
// パス ("Assets/Models/robot.glb") を持っていた。それはそのまま読み、別のモデルを選ぶまで
// 見つかる (SoftwareHost::AssetFile)。

#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Primitives.h"

#include <optional>
#include <string>
#include <vector>

namespace rm::game_x_3d {

RM_ENUM(Label="形")
enum class MeshShape {
    RM_ITEM(Label="モデル") Model,
    RM_ITEM(Label="箱") Box,
    RM_ITEM(Label="球") Sphere,
    RM_ITEM(Label="円柱") Cylinder,
    RM_ITEM(Label="カプセル") Capsule,
    RM_ITEM(Label="平面") Plane,
    RM_ITEM(Label="円錐") Cone,
    RM_ITEM(Label="四角錐") Pyramid,
};

// 描く単純な形。nullopt: モデル (MeshShape::Model)。
inline std::optional<space3d::Shape> ShapeOf(MeshShape s) {
    switch (s) {
        case MeshShape::Model: return std::nullopt;
        case MeshShape::Box: return space3d::Shape::Box;
        case MeshShape::Sphere: return space3d::Shape::Sphere;
        case MeshShape::Cylinder: return space3d::Shape::Cylinder;
        case MeshShape::Capsule: return space3d::Shape::Capsule;
        case MeshShape::Plane: return space3d::Shape::Plane;
        case MeshShape::Cone: return space3d::Shape::Cone;
        case MeshShape::Pyramid: return space3d::Shape::Pyramid;
    }
    return std::nullopt;
}

RM_STRUCT(Label="メッシュ (3D モデル)")
struct MeshRenderer {
    RM_PROPERTY(Edit, Label="形", Tooltip="モデル: 下で選んだ 3D モデルのファイルで描きます / 箱・球・円柱・カプセル・平面・円錐・四角錐: ファイルなしで、その形で描きます (大きさは 1。カプセルは高さ 2、平面は 10 x 10)")
    MeshShape shape = MeshShape::Model;
    RM_PROPERTY(Edit, Label="モデル", Tooltip="3D モデルのアセット (.gltf / .glb / .obj / .fbx が読めます)。アセットのパネルからドラッグするか、クリックして選びます", AssetKind="model") AssetRef model;  // (形 is モデル)
    // マテリアル (Material.h の .rmmat): 形は 0 番で塗る。モデルは i 番がモデル自身の i 番の材質の
    // 代わり (空の欄はモデルのまま)。無い (前のデータ): 形は灰色、モデルはファイルの材質のまま。
    RM_PROPERTY(Edit, Label="マテリアル", Tooltip="見た目 (.rmmat。アセットのパネルの右クリック →「作成」→「マテリアル」で作ります)。形は 1 つ目で塗ります。モデルは i 番目がモデルの i 番目の材質の代わりで、空の欄はモデルのままです", AssetKind=".rmmat")
    std::vector<AssetRef> materials;
    RM_PROPERTY(Edit, Label="影を落とす") bool castShadows = true;
};

}  // namespace rm::game_x_3d
