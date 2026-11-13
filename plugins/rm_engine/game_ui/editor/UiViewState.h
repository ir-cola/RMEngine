#pragma once
// How the user looks at the UI view: the guides on or off. Kept next to the panel layout
// (UserSettings/Layout.json: the game's "viewPanels" -> "ui"), not in the project.

#include "core/reflect/Reflect.h"

namespace rm::game_ui {

RM_STRUCT()
struct UiViewState {
    RM_PROPERTY() bool safeArea = true;  // the safe area's guide
    RM_PROPERTY() bool anchors = true;   // the selected thing's anchors
};

}  // namespace rm::game_ui
