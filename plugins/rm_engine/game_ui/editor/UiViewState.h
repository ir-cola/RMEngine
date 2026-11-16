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
};

}  // namespace rm::game_ui
