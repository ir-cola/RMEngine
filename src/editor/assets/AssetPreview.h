#pragma once
// アセットパネルからアセットを聞く (または見る) - その種類を知っているプラグイン (音
// プラグイン: 音) が出す。核は音について何も知らない: パネルは `describe` が返す行 (音の
// 長さと形式) を出し、その種類のプレビューがあれば「鳴らしてみる」/「止める」を出すだけ。
//
//   // プラグインの RegisterEditor で:
//   static const editor::AssetPreview kSound{asset::Kind::Audio, &Describe, &Play, &Stop, &Playing};
//   registry.Contribute(editor::kAssetPreviewPoint, kSound);

#include "base/Json.h"
#include "core/asset/AssetIndex.h"

#include <filesystem>
#include <functional>
#include <string>

namespace rm::editor {

class PluginRegistry;

inline constexpr char kAssetPreviewPoint[] = "core.asset_preview";

struct AssetPreview {
    asset::Kind kind = asset::Kind::Audio;
    // ファイルについての短い 1 行 (使っている言語で。ないときは "")。プラグインはそれを自分の
    // スレッドで読んでもよい: それまでは、そう言う行を返し、本当の行が出来たら (どのスレッドからでも)
    // `known` を呼ぶ - するとパネルは全ウィンドウを描き直させる。
    std::wstring (*describe)(const std::filesystem::path& file, const std::function<void()>& known) = nullptr;
    // 取り込みの設定 (`import`: その .meta の "import") で鳴らす。前に鳴っていた物は止まる。
    void (*play)(const std::filesystem::path& file, const json::Value& import) = nullptr;
    void (*stop)() = nullptr;
    bool (*playing)() = nullptr;  // 何かが鳴っている
};

// その種類のプレビュー。なければ null。
const AssetPreview* PreviewFor(const PluginRegistry& registry, asset::Kind kind);

}  // namespace rm::editor
