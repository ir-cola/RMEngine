#pragma once
// エディターの整合性の確認 (Hub > オプション): are the engine's files all there and unchanged?
// Like Steam's "verify integrity of game files". The build writes <exe>/RM_Engine.files.json
// (tools/make_filelist.ps1: path, size, SHA-256 of RM_Engine.exe, Lang/**, Plugins/**); the check
// hashes the files again and compares. Installed plugins (%LOCALAPPDATA%\RM_Engine\Plugins) are
// compared with the engine's copies in the same list: a stale or broken one can be copied again.
//
// Pure (the hashing is passed in) - tested in tests/TestIntegrity.cpp.

#include "base/Json.h"
#include "core/PluginId.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor::integrity {

struct FileEntry {
    std::string path;  // relative to the exe's folder, '/' separated
    uint64_t size = 0;
    std::string sha256;  // lower-case hex
};

std::optional<std::vector<FileEntry>> ParseFileList(const json::Value& v, std::string* error);

enum class Problem { Missing, Changed };
struct FileProblem {
    std::string path;
    Problem problem;
};

// hashOf(path) = the file's SHA-256 now, nullopt when it is missing.
using HashOf = std::function<std::optional<std::string>(const std::string& path)>;

std::vector<FileProblem> CheckFiles(const std::vector<FileEntry>& list, const HashOf& hashOf);

// Installed plugins whose files differ from the engine's copies in the list ("Plugins/<a>/<n>/..."):
// installedHashOf(id, pathInsideThePlugin) = the installed file's SHA-256, nullopt when missing.
// Files only the installed copy has are not looked at.
using InstalledHashOf = std::function<std::optional<std::string>(const PluginId& id, const std::string& pathInPlugin)>;
std::vector<PluginId> StalePlugins(const std::vector<FileEntry>& list, const std::vector<PluginId>& installed,
                                   const InstalledHashOf& installedHashOf);

// --- After the engine was updated (each start) ------------------------------------------------
// The installed plugins are copies of the engine's; a new engine may bring new copies (plugin.json,
// translations, theme files ...). Whether to look at all: the SHA-256 of the engine's file list
// differs from the stamp kept after the last look (no list: always - then only plugin.json is
// compared, which is cheap).
bool EngineChanged(const std::optional<std::string>& fileListHash, const std::string& keptStamp);
// Which installed plugins (that this engine also has) to copy again: with the file list, those whose
// files differ (StalePlugins); without it, those whose plugin.json differs (manifestDiffers).
using ManifestDiffers = std::function<bool(const PluginId& id)>;
std::vector<PluginId> PluginsToRefresh(const std::optional<std::vector<FileEntry>>& list, const std::vector<PluginId>& installed,
                                       const InstalledHashOf& installedHashOf, const ManifestDiffers& manifestDiffers);

}  // namespace rm::editor::integrity
