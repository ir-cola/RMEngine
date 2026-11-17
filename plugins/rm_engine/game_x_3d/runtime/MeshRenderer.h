#pragma once
// ゲーム制作 × 3D - an entity drawn as a 3D model (its shape from a file), placed by its Transform.
// Without a model (or one that cannot be read) it is drawn as a box, as before. Or as a simple shape
// made without a file (形: 箱 / 球 / 円柱 / カプセル / 平面 - 3D 空間's Primitives.h, their sizes there): the
// 階層's 追加 ▸ 3D オブジェクト makes entities with them.
//
// `model` is an asset reference (the id in the file's .meta): moving or renaming the file in the アセット
// panel or in Explorer does not lose it. Data from before references held a path inside the project
// ("Assets/Models/robot.glb"); it is read as it is and still found (SoftwareHost::AssetFile) until
// another model is chosen.

#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Primitives.h"

#include <optional>
#include <string>

namespace rm::game_x_3d {

RM_ENUM(Label="形")
enum class MeshShape {
    RM_ITEM(Label="モデル") Model,
    RM_ITEM(Label="箱") Box,
    RM_ITEM(Label="球") Sphere,
    RM_ITEM(Label="円柱") Cylinder,
    RM_ITEM(Label="カプセル") Capsule,
    RM_ITEM(Label="平面") Plane,
};

// The simple shape it is drawn as; nullopt: the model (MeshShape::Model).
inline std::optional<space3d::Shape> ShapeOf(MeshShape s) {
    switch (s) {
        case MeshShape::Model: return std::nullopt;
        case MeshShape::Box: return space3d::Shape::Box;
        case MeshShape::Sphere: return space3d::Shape::Sphere;
        case MeshShape::Cylinder: return space3d::Shape::Cylinder;
        case MeshShape::Capsule: return space3d::Shape::Capsule;
        case MeshShape::Plane: return space3d::Shape::Plane;
    }
    return std::nullopt;
}

RM_STRUCT(Label="メッシュ (3D モデル)")
struct MeshRenderer {
    RM_PROPERTY(Edit, Label="形", Tooltip="モデル: 下で選んだ 3D モデルのファイルで描きます / 箱・球・円柱・カプセル・平面: ファイルなしで、その形で描きます (大きさは 1。カプセルは高さ 2、平面は 10 x 10)")
    MeshShape shape = MeshShape::Model;
    RM_PROPERTY(Edit, Label="モデル", Tooltip="3D モデルのアセット (.gltf / .glb / .obj / .fbx が読めます)。アセットのパネルからドラッグするか、クリックして選びます", AssetKind="model") AssetRef model;  // (形 is モデル)
    RM_PROPERTY(Edit, Label="影を落とす") bool castShadows = true;
};

}  // namespace rm::game_x_3d
