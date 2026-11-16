#pragma once
// How the user looks at the UI view: the guides on or off. Kept next to the panel layout
// (UserSettings/Layout.json: the game's "viewPanels" -> "ui"), not in the project.

#include "base/Math.h"
#include "core/reflect/Reflect.h"

namespace rm::game_ui {

RM_STRUCT()
struct UiViewState {
    RM_PROPERTY() bool safeArea = true;  // the safe area's guide
    RM_PROPERTY() bool anchors = true;   // the selected thing's anchors
    RM_PROPERTY(Min=0.1, Max=10) float zoom = 1;  // 1: the game's screen fits the panel
    RM_PROPERTY() Vec2 pan{0, 0};                  // DIPs the screen is moved by from the middle
    RM_PROPERTY() bool snapGuides = true;          // to other things' sides and middles (補助線)
    RM_PROPERTY() bool snapGrid = false;           // to the grid (格子)
    RM_PROPERTY(Min=1, Max=1000) float gridStep = 10;  // the grid's step (the canvas's units)
};

}  // namespace rm::game_ui
