#include "rm_engine/animation3d/editor/Register.h"

#include "editor/assets/AssetInspect.h"
#include "rm_engine/animation3d/editor/ClipStrip.h"

namespace rm::animation3d {

void RegisterEditor(editor::PluginRegistry& registry) {
    // 3D モデルのインスペクターの「クリップとイベント」(イベントを置く帯)。
    static const editor::AssetInspectPart kClips{asset::Kind::Model, &OpenClipStrip};
    registry.Contribute(editor::kAssetInspectPoint, kClips);
}

}  // namespace rm::animation3d
