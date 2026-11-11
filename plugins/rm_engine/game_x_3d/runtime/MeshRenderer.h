#pragma once
// ゲーム制作 × 3D - an entity drawn as a 3D model (its shape from a file), placed by its Transform.
// Without a model (or one that cannot be read) it is drawn as a box, as before.
//
// `model` is an asset reference (the id in the file's .meta): moving or renaming the file in the アセット
// panel or in Explorer does not lose it. Data from before references held a path inside the project
// ("Assets/Models/robot.glb"); it is read as it is and still found (SoftwareHost::AssetFile) until
// another model is chosen.

#include "core/reflect/Reflect.h"

#include <string>

namespace rm::game_x_3d {

RM_STRUCT(Label="メッシュ (3D モデル)")
struct MeshRenderer {
    RM_PROPERTY(Edit, Label="モデル", Tooltip="3D モデルのアセット (.gltf / .glb / .obj / .fbx が読めます)。アセットのパネルからドラッグするか、クリックして選びます", AssetKind="model") AssetRef model;
    RM_PROPERTY(Edit, Label="影を落とす") bool castShadows = true;
};

}  // namespace rm::game_x_3d
