#pragma once
// タイルマップ × 物理 のエディターの部分: シーンビューにタイルの当たりの線 (物理に渡すのと同じまとめた形。
// ゲーム制作の口 scene_lines。ツールバーの「形: 選択 / 全部」に従う)。

namespace rm::editor {
class PluginRegistry;
}

namespace rm::tilemap_x_physics {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::tilemap_x_physics
