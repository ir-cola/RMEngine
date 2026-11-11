#pragma once
// The 2D scene view of a game (the connector ゲーム制作 × 2D): the grid and the axes, every entity that
// has a Transform 2D where it is (parents included) - its sprite, or a small ring when it has none - back
// to front by 並び順, the selection outlined.
//
//   right / middle drag   slide the picture (it follows the mouse)
//   wheel                 nearer / farther (about the point under the mouse)
//   left click            select the entity under the mouse (the front one; nothing there: none)
//   Ctrl + left click     add to / take from the selection
//   left drag on a thing  move it (and everything selected with it, when it is selected)
//   F                     come to the selected entity
//
// The selected entity has a gizmo (2D 空間's space2d::gizmo2d): its arrows / ring / boxes move, turn or
// scale every selected entity, one undo entry per drag. W / E / R or the strip at the top left choose the
// tool, X (or the strip) the thing's own axes or the world's. Ctrl while dragging: in steps (the grid's
// step, 15 degrees, 0.1 times). Esc puts everything back while dragging. The camera, the tool and the
// grid on / off are kept in UserSettings.
//
// Drawn by 2D 空間's renderer with Direct2D straight into the panel (no picture of its own).

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_x_2d {

std::unique_ptr<game_production::SceneView> CreateSceneView2D();

}  // namespace rm::game_x_2d
