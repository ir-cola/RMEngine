#pragma once
// ゲーム制作 × タイルマップ のエディターの部分: 追加 ▸ 2D オブジェクト ▸ タイルマップ・パネル「タイル」・
// 2D のシーンビューの塗る道具・部品の見せ方。

namespace rm::editor {
class PluginRegistry;
}

namespace rm::game_x_tilemap {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_tilemap
