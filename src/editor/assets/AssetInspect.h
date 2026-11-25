#pragma once
// アセットのインスペクターに、プラグインがブロックを足す口。核は中身を知らない (3D アニメーション:
// モデルのクリップの帯。docs/ANIMATION.md 3-1 の注「プラグインが .meta の "import" に自分のキーを足せる口」)。
//
//   // プラグインの RegisterEditor で:
//   static const editor::AssetInspectPart kClips{asset::Kind::Model, &OpenClips};
//   registry.Contribute(editor::kAssetInspectPoint, kClips);
//
// アセットを 1 つインスペクターに出すとき (アセットのパネルで選んだとき) に `open` を 1 回呼ぶ。返した関数を
// インスペクターを描くたびに呼び、そのブロックを核の取り込みの設定の下に並べる (欄のブロックでも、
// 自分で描くブロック InspectBlock::customDraw でもよい)。返した関数は、ほかの物を出すまで持たれる
// (選んだクリップなどの見せ方は、その中に持てばよい)。
//
// 取り込みの設定 (.meta の "import") は `access` を通して読み書きする: `setImportKey` は "import" の
// 1 つのキーを前と後の値で替える Undo の 1 件をソフトの履歴に積む = Ctrl+Z で戻り、未保存になる (憲法 7)。
// 書くのは核の取り込みの設定と同じ: 編集が終わったときに .meta へ (AssetLibrary::FlushImports)。
// ほかのキーと、そのキーの中の知らないものは、プラグインが残す (憲法 6)。

#include "base/Json.h"
#include "core/asset/AssetIndex.h"
#include "editor/plugin/Software.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rm::editor {

inline constexpr char kAssetInspectPoint[] = "core.asset_inspect";

// インスペクターに出している 1 つのアセットと、その取り込みの設定の窓。コピーしてよい (中は共有)。
struct AssetImportAccess {
    std::string guid;
    asset::Kind kind = asset::Kind::Other;
    // 今のフルパス (動かされても追う)。消えたら空。
    std::function<std::filesystem::path()> file;
    // 今の "import" (待っている編集を含む。ディスクの .meta は 0.5 秒に 1 回まで読み直す)。無ければ空の
    // オブジェクトか null。返した参照は次の呼び出しまで使える。
    std::function<const json::Value&()> import;
    // "import" のキー `key` を `after` に (null: キーを消す)。前と同じなら何もしない。`merge` が 0 以外:
    // 同じ鍵が続く間 (1 回のドラッグ・1 つの欄への入力) は Undo の 1 件にまとまる
    // (inspector::NewMergeKey で作る)。`label` は Undo の履歴の名前。
    std::function<void(std::string key, json::Value after, std::string label, uint64_t merge)> setImportKey;
};

struct AssetInspectPart {
    asset::Kind kind = asset::Kind::Other;
    std::function<std::vector<InspectBlock>()> (*open)(const AssetImportAccess& access) = nullptr;
};

// その種類に足すもの (登録の順)。
std::vector<const AssetInspectPart*> InspectPartsFor(const PluginRegistry& registry, asset::Kind kind);

}  // namespace rm::editor
