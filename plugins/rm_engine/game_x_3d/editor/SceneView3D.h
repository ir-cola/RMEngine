#pragma once
// The 3D scene view of a game (the connector ゲーム制作 × 3D): the floor grid and axes, every
// entity that has a Transform as a box where it is (parents included), the selection outlined.
// The camera moves like Unity's:
//
//   right drag          look around (the eye stays);  + W A S D / Q E: fly (Shift: faster)
//   middle drag         slide (the picture follows the mouse)
//   wheel               nearer / farther
//   Alt + left drag     go around the point looked at
//   F                   come to the selected entity
//   left click          select the entity under the mouse (nothing there: none)
//
// The selected entity has a gizmo (3D 空間's space3d::gizmo): drag its arrows / rings / boxes to move,
// turn or scale it - every selected entity with it, one undo entry per drag. W / E / R or the strip at
// the top left choose the tool, X (or the strip) the thing's own axes or the world's, Z (or the strip)
// each in its own place or all about their middle (中心: the gizmo there). Rotating also has an outer
// ring facing the eye and a free turn inside the ball. Ctrl moves in steps (their sizes, and "always
// in steps", in the strip's 刻み box), Esc puts everything back while dragging. The camera, the tool
// and the steps are kept in UserSettings.
//
// Drawn by 3D 空間's renderer into a picture that the panel shows (Direct3D and Direct2D share one GPU).

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_x_3d {

std::unique_ptr<game_production::SceneView> CreateSceneView3D();

}  // namespace rm::game_x_3d
