#pragma once
// ゲーム UI のエディターの部分: UI ビュー (ゲームのパネル: UiView.h)、UI のものを作る 階層 の行
// (キャンバス / ボタン / 文字 / 画像 / バー)、階層に出すそれらの絵。

#include "editor/plugin/Software.h"

namespace rm::game_ui {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_ui
