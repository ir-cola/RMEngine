#include "rm_engine/animation3d/editor/Register.h"

#include "editor/assets/AssetInspect.h"
#include "rm_engine/animation3d/editor/ClipStrip.h"

namespace rm::animation3d {

void RegisterEditor(editor::PluginRegistry& registry) {
    // 3D モデルのインスペクターの「クリップとイベント」(イベントを置く帯)。
    static const editor::AssetInspectPart kClips{asset::Kind::Model, &OpenClipStrip};
    registry.Contribute(editor::kAssetInspectPoint, kClips);
    // アセットのパネルで、モデルのタイルの ▸ で中のクリップを並べる (ドラッグで欄へ。docs/ANIMATION.md 7-3 の C1)。
    static const editor::AssetChildSource kClipTiles{asset::Kind::Model, editor::w::Icon::Play, &ClipChildren};
    registry.Contribute(editor::kAssetChildrenPoint, kClipTiles);
}

}  // namespace rm::animation3d
