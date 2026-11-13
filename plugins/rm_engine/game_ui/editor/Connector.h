#pragma once
// The editor part of ゲーム UI: the UI view (a panel of the game: UiView.h), the 階層's rows to make UI
// things (キャンバス / ボタン / 文字 / 画像 / バー) and their pictures there.

#include "editor/plugin/Software.h"

namespace rm::game_ui {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_ui
