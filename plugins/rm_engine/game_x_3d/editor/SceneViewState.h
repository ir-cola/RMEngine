#pragma once
// How the user looks at a game's 3D scene view: the camera and the gizmo chosen. Kept next to the
// panel layout (UserSettings/Layout.json), not in the project - written once here, so saving and
// loading come from the description (rule 9). Never shown in an inspector.

#include "rm_engine/space3d/editor/Gizmo.h"
#include "rm_engine/space3d/runtime/View.h"

namespace rm::game_x_3d {

RM_STRUCT()
struct SceneViewState {
    RM_PROPERTY() space3d::OrbitView camera;
    RM_PROPERTY() space3d::gizmo::Tool tool = space3d::gizmo::Tool::Move;
    RM_PROPERTY() space3d::gizmo::Space space = space3d::gizmo::Space::Local;
};

}  // namespace rm::game_x_3d
