#include "rm_engine/animation/editor/Register.h"

#include "editor/assets/AssetFileType.h"
#include "editor/lang/Lang.h"
#include "rm_engine/animation/runtime/Controller.h"

namespace rm::animation {

void RegisterEditor(editor::PluginRegistry& registry) {
    using editor::TrKey;
    // 状態とルール (.rmanim): 部品「アニメーター」の「状態とルール」に入れると、ルールがクリップを決める。
    static const editor::AssetFileType kControllerFile{
        .extension = kControllerExtension,
        .kind = kControllerKind,
        .type = &reflect::TypeOf<AnimController>,
        .label = TrKey(L"アニメーター (状態とルール)"),
        .newName = TrKey(L"新しいアニメーター"),
        .folder = "Animations",
        .note = TrKey(L"部品「アニメーター」の「状態とルール」に入れると、ルールが上から調べられ、当たった行の状態のクリップを流します。動きの様子は「アニメーション」タブで見られます"),
    };
    registry.Contribute(editor::kAssetFileTypePoint, kControllerFile);
}

}  // namespace rm::animation
