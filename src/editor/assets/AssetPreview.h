#pragma once
// Hearing (or looking at) an asset from the アセット panel - offered by a plugin that knows the kind (the
// 音 plugin: sounds). The core knows nothing of sound: the panel only shows the line `describe` gives
// (a sound's length and form) and offers 「鳴らしてみる」/「止める」 when there is a preview for the kind.
//
//   // in a plugin's RegisterEditor:
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
    // A short line about the file (in the language in use; "" when there is none). The plugin may read
    // it on a thread of its own: until then it gives a line saying so, and calls `known` (from any
    // thread) once the real line is there - the panel then has the windows drawn again.
    std::wstring (*describe)(const std::filesystem::path& file, const std::function<void()>& known) = nullptr;
    // Plays it with its import settings (`import`: its .meta's "import"); what played before stops.
    void (*play)(const std::filesystem::path& file, const json::Value& import) = nullptr;
    void (*stop)() = nullptr;
    bool (*playing)() = nullptr;  // something is playing
};

// The preview of a kind, or null.
const AssetPreview* PreviewFor(const PluginRegistry& registry, asset::Kind kind);

}  // namespace rm::editor
