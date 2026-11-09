#include "rm_engine/game_x_3d/editor/Connector.h"

#include "editor/lang/Lang.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_3d/editor/SceneView3D.h"
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
                                                           &UnderiveWorld, editor::TrKey(L"ローカル"), editor::TrKey(L"ワールド"), "cube"};
    registry.Contribute(game_production::kComponentPoint, kTransform);
    // ... and are seen in 3D in the scene view.
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView3D};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
}

}  // namespace rm::game_x_3d
