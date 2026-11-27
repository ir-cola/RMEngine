#pragma once
// アセットの子 (docs/ANIMATION.md 7-3 の C1: モデルの中のクリップ)。アセットのパネルで、子のあるファイルのタイルに
// ▸ が出て、押すとそのタイルの後ろに子のタイルが並ぶ (もう一度押すと閉じる)。子はドラッグできる: 渡すもの
// (w::DragThing の kind と values) は子を出すプラグインが決める。核は子が何かを知らない (憲法 10)。
//
//   static const editor::AssetChildSource kClips{asset::Kind::Model, w::Icon::Play, &ClipChildren};
//   registry.Contribute(editor::kAssetChildrenPoint, kClips);
//
// 名前を渡す子 (kind w::kDragName、values: その名前) は、インスペクターの選ぶ欄 (▾ の文字の欄) に落とせる: 落とした名前が
// その欄の選べるものの中にあるときだけ (Inspector.cpp の DrawChoices)。

#include "core/asset/AssetIndex.h"
#include "editor/widgets/Widgets.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rm::editor {

inline constexpr char kAssetChildrenPoint[] = "core.asset_children";

struct AssetChild {
    std::wstring name;     // タイルの名前
    std::wstring tooltip;  // マウスの下に出す (空: 出さない)
    w::DragThing drag;     // ドラッグで渡すもの (kind が空: ドラッグしない)
};

struct AssetChildSource {
    asset::Kind kind = asset::Kind::Model;
    w::Icon icon = w::Icon::Play;  // 子のタイルの絵
    // `file` の子 (並びの順)。読んでいる途中は *pending = true (空でよい: パネルが後のフレームにまた尋ねる)。
    // UI スレッドで、描くたびに呼ぶ (速く: 覚えておくこと。重い読み込みは別のスレッドで)。
    std::vector<AssetChild> (*children)(const std::filesystem::path& file, bool* pending) = nullptr;
};

}  // namespace rm::editor
