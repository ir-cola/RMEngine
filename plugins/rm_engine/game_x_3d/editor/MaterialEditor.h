#pragma once
// マテリアル (.rmmat。runtime/Material.h) のエディターでの扱い:
//   アセットのパネル   右クリック「作成 ▸ マテリアル」で新しいファイル、選ぶとインスペクターで編集
//                      (核の editor::AssetFileType: 型の表から欄・Undo・ファイルへの書き込みを作る)
//   サムネイル         そのマテリアルを塗った球 (3D 空間のサムネイルと同じ描き手。核の kThumbnailPoint)
// シーンビューとゲームは、どちらも MaterialLook で描く (SceneView3D.cpp / GameDraw3D.cpp)。

#include "editor/plugin/Software.h"

namespace rm::game_x_3d {

void RegisterMaterialEditor(editor::PluginRegistry& registry);

}  // namespace rm::game_x_3d
