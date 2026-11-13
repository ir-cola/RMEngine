#pragma once
// How the user looks at a game's 2D scene view: the camera, the gizmo chosen (and its steps), the grid on / off. Kept
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
    RM_PROPERTY() space2d::gizmo2d::Pivot pivot = space2d::gizmo2d::Pivot::Each;
    RM_PROPERTY() bool grid = true;
    // While playing: the lines the game's scripts draw (rm::Debug) - the デバッグ button.
    RM_PROPERTY() bool debugDraw = true;
    // The lines plugins show over the scene (物理: the colliders) of every thing, not only the selected - 形.
    RM_PROPERTY() bool allShapes = false;
    // The steps of a drag (Ctrl held, or always when snapAlways - then Ctrl moves smoothly).
    RM_PROPERTY() bool snapToGrid = true;                     // moves by the grid's step (finer when nearer)
    RM_PROPERTY(Min=0.001, Max=100000) float snapMove = 0.25f;  // ... or by this (units)
    RM_PROPERTY(Min=0.1, Max=180) float snapDegrees = 15.f;
    RM_PROPERTY(Min=0.001, Max=10) float snapScale = 0.1f;     // of the scale at the start
    RM_PROPERTY() bool snapAlways = false;
};

}  // namespace rm::game_x_2d
