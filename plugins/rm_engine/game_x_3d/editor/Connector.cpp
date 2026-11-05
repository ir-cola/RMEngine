#include "rm_engine/game_x_3d/editor/Connector.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_x_3d/editor/SceneView3D.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

void RegisterEditor(editor::PluginRegistry& registry) {
    // A 3D game's entities have a place in 3D space.
    static const game_production::ComponentInfo kTransform{&reflect::TypeOf<space3d::Transform>, true};
    registry.Contribute(game_production::kComponentPoint, kTransform);
    // ... and are seen in 3D in the scene view.
    static const game_production::SceneViewInfo kSceneView{&CreateSceneView3D};
    registry.Contribute(game_production::kSceneViewPoint, kSceneView);
}

}  // namespace rm::game_x_3d
