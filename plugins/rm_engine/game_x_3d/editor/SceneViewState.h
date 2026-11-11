#pragma once
// How the user looks at a game's 3D scene view: the camera, the gizmo chosen and its steps, shadows on / off. Kept next to the
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
    RM_PROPERTY() space3d::gizmo::Pivot pivot = space3d::gizmo::Pivot::Each;
    // The steps of the gizmo (Ctrl held, or always when snapAlways - then Ctrl moves smoothly).
    RM_PROPERTY(Min=0.001, Max=1000) float snapMove = 0.25f;  // world units
    RM_PROPERTY(Min=0.1, Max=180) float snapDegrees = 15.f;
    RM_PROPERTY(Min=0.001, Max=10) float snapScale = 0.1f;   // of the scale at the start
    RM_PROPERTY() bool snapAlways = false;
    // Shadows of the light (on the floor and on things): the 影 button.
    RM_PROPERTY() bool shadows = true;
};

}  // namespace rm::game_x_3d
