#include "rm_engine/game_x_3d/editor/Connector.h"

#include "editor/lang/Lang.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_3d/editor/SceneView3D.h"
#include "rm_engine/game_x_3d/runtime/Camera.h"
#include "rm_engine/game_x_3d/runtime/Light.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

namespace {

// A Transform's place in the world: its parents' Transforms, then its own (shown under it, read-only).
void DeriveWorld(std::span<const void* const> chain, void* out) {
    space3d::Mat4 world;
    for (const void* p : chain) world = space3d::WorldMatrix(world, *static_cast<const space3d::Transform*>(p));
    // Near its own angles: a thing at the top level shows the same numbers as its Transform.
    const Vec3 hint = static_cast<const space3d::Transform*>(chain.back())->rotation;
    *static_cast<space3d::WorldPlacement*>(out) = space3d::Decompose(world, hint);
}

// World values typed in the inspector -> the Transform that gives them under its parents.
void UnderiveWorld(std::span<const void* const> chain, const void* derived, void* component) {
    space3d::Mat4 parent;
    for (size_t i = 0; i + 1 < chain.size(); ++i) parent = space3d::WorldMatrix(parent, *static_cast<const space3d::Transform*>(chain[i]));
    auto& t = *static_cast<space3d::Transform*>(component);
    t = space3d::FromWorld(t, parent, *static_cast<const space3d::WorldPlacement*>(derived));
}

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    // A 3D game's entities have a place in 3D space - shown as it is (ローカル: in the parent) or in
    // the world (ワールド: through every parent), switched above its fields in the inspector.
    static const game_production::ComponentInfo kTransform{&reflect::TypeOf<space3d::Transform>, true,
                                                           &reflect::TypeOf<space3d::WorldPlacement>, &DeriveWorld,
                                                           &UnderiveWorld, editor::TrKey(L"ローカル"), editor::TrKey(L"ワールド"), "cube",
                                                           /*multiple*/ false, /*placement*/ true, /*space*/ "3d"};
    registry.Contribute(game_production::kComponentPoint, kTransform);
    // ... and may be drawn as a 3D model (added by hand: a new entity is a box until it has one).
    static const game_production::ComponentInfo kMesh{&reflect::TypeOf<MeshRenderer>, false};
    registry.Contribute(game_production::kComponentPoint, kMesh);
    // カメラ (what the game sees: the game settings' 最初のカメラ) and 光 - each with its own picture in the 階層.
    static const game_production::ComponentInfo kCamera{.type = &reflect::TypeOf<Camera>, .icon = "camera"};
    registry.Contribute(game_production::kComponentPoint, kCamera);
    static const game_production::ComponentInfo kLight{.type = &reflect::TypeOf<Light>, .icon = "light"};
    registry.Contribute(game_production::kComponentPoint, kLight);
    static const game_production::ComponentInfo kAmbient{.type = &reflect::TypeOf<AmbientLight>, .icon = "light"};
    registry.Contribute(game_production::kComponentPoint, kAmbient);
    // (回り続ける, カメラを順に切り替える and their systems are the runtime part's: runtime/Register.cpp)
    // ... and are seen in 3D in the scene view.
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView3D, "3d", editor::TrKey(L"3D"), /*order*/ 0};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
}

}  // namespace rm::game_x_3d
