#pragma once
// ゲーム制作 × 3D - an entity drawn as a 3D model (its shape from a file), placed by its Transform.
// Without a model (or one that cannot be read) it is drawn as a box, as before.
//
// ★ `model` is, for now, a path inside the project (relative to the folder of the .rmpf, with / or
// \: "Assets/Models/robot.glb"). When asset references are made (another session is building them)
// this becomes one, so a model that is moved or renamed is still found (docs/ARCHITECTURE.md).

#include "core/reflect/Reflect.h"

#include <string>

namespace rm::game_x_3d {

RM_STRUCT(Label="メッシュ (3D モデル)")
struct MeshRenderer {
    RM_PROPERTY(Edit, Label="モデル", Tooltip="プロジェクトのフォルダからの相対パス (例: Assets/Models/robot.glb)。.gltf / .glb / .obj が読めます") std::string model;
    RM_PROPERTY(Edit, Label="影を落とす") bool castShadows = true;
};

}  // namespace rm::game_x_3d
