#pragma once
// タイルマップのエディターの部分: ファイルの種類 .rmtileset (アセットのパネルの「作成 ▸ タイルセット」と
// インスペクター。核の口 core.asset_file_type) と、そのサムネイル (1 つ目の絵の元の画像)。

namespace rm::editor {
class PluginRegistry;
}

namespace rm::tilemap {

void RegisterEditor(editor::PluginRegistry& registry);

}  // namespace rm::tilemap
