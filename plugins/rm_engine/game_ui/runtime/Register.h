#pragma once
// The runtime part of ゲーム UI: what an exported game needs from it too (listed in
// plugins/RuntimePlugins.cpp; the editor's loader calls it before RegisterEditor) - the UI's component
// types, the service that runs it while the game plays and the scripts' part. The drawing over the
// screen is UiScreenDraw.h's (registered for the game there, by the editor part for the game view).

#include "core/RuntimeRegistry.h"

namespace rm::game_ui {

void RegisterRuntime(RuntimeRegistry& registry);

}  // namespace rm::game_ui
