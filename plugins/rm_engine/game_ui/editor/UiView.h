#pragma once
// The UI view (a panel of the game software: game production's ViewPanelInfo): the game's screen as the
// game settings' 画面, the UI drawn on it as the game draws it (the same layout and drawing), and the
// tools to make it:
//
//   left click            select the UI thing under the mouse (the frontmost)
//   left drag on a thing  move it (its 位置)
//   a corner's square     resize it (the opposite corner stays)
//   Esc while dragging    back to where it was
//
// The selected thing shows its anchors (the little marks where it is fixed in its parent) and the canvas
// its edge; the safe area of the game settings is a guide (セーフエリア in the strip). One undo entry per
// drag. A canvas switched off is shown while one of its things is selected (the window being made).

#include "rm_engine/game_production/editor/SceneView.h"

#include <memory>

namespace rm::game_ui {

std::unique_ptr<game_production::SceneView> CreateUiView();

}  // namespace rm::game_ui
