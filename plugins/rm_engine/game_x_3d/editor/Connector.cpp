#include "rm_engine/game_x_3d/editor/Connector.h"

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

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    // A 3D game's entities have a place in 3D space (and in the world, through their parents).
    static const game_production::ComponentInfo kTransform{&reflect::TypeOf<space3d::Transform>, true,
                                                           &reflect::TypeOf<space3d::WorldPlacement>, &DeriveWorld};
    registry.Contribute(game_production::kComponentPoint, kTransform);
    // ... and are seen in 3D in the scene view.
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView3D};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
}

}  // namespace rm::game_x_3d
