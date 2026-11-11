#pragma once
// How the user looks at a game's 2D scene view: the camera, the gizmo chosen, the grid on / off. Kept
// next to the panel layout (UserSettings/Layout.json, "sceneViews" -> "2d"), not in the project - written
// once here, so saving and loading come from the description (rule 9). Never shown in an inspector.

#include "rm_engine/space2d/editor/Gizmo2D.h"
#include "rm_engine/space2d/runtime/View2D.h"

namespace rm::game_x_2d {

RM_STRUCT()
struct SceneViewState2D {
    RM_PROPERTY() space2d::Camera2D camera;
    RM_PROPERTY() space2d::gizmo2d::Tool tool = space2d::gizmo2d::Tool::Move;
    RM_PROPERTY() space2d::gizmo2d::Space space = space2d::gizmo2d::Space::Local;
    RM_PROPERTY() bool grid = true;
};

}  // namespace rm::game_x_2d
