#pragma once
// ゲーム制作 × ナビゲーション のエディター部分: 「ナビ」タブ (NavTab.h)・シーンビューの線 (NavLines.h)・
// 階層の 追加 ▸ ナビ ▸ ナビメッシュ / ナビのエリア・部品の欄のドロップダウン (型とエリア)・書き出しの焼き手 (.rmnav)。
// 部品とシーンの設定はランタイム部分 (runtime/Register.cpp) のもの。

#include "editor/plugin/Software.h"

namespace rm::game_x_navigation {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_navigation
