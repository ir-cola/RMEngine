#pragma once
// エディターの整合性の確認 (Hub > オプション): エンジンのファイルは全部あって、変わっていないか?
// Steam の「ゲームファイルの整合性を確認」と同じ。ビルドは <exe>/RM_Engine.files.json を書く
// (tools/make_filelist.ps1: パス、サイズ、RM_Engine.exe・Lang/**・Plugins/** の SHA-256)。確認では
// ファイルをもう一度ハッシュして比べる。入っているプラグイン (%LOCALAPPDATA%\RM_Engine\Engines\<engine>\Plugins) は
// 同じ一覧にあるエンジンのコピーと比べ、古い物や壊れた物はコピーし直せる。
//
// 純粋 (ハッシュは外から渡す)。tests/TestIntegrity.cpp でテストする。

#include "base/Json.h"
#include "core/PluginId.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor::integrity {

struct FileEntry {
    std::string path;  // exe のフォルダーからの相対パス、'/' 区切り
    uint64_t size = 0;
    std::string sha256;  // 小文字の 16 進
};

std::optional<std::vector<FileEntry>> ParseFileList(const json::Value& v, std::string* error);

enum class Problem { Missing, Changed };
struct FileProblem {
    std::string path;
    Problem problem;
};

// hashOf(path) = そのファイルの今の SHA-256。ファイルがなければ nullopt。
using HashOf = std::function<std::optional<std::string>(const std::string& path)>;

std::vector<FileProblem> CheckFiles(const std::vector<FileEntry>& list, const HashOf& hashOf);

// ファイルが一覧にあるエンジンのコピーと違う、入っているプラグイン ("Plugins/<a>/<n>/..."):
// installedHashOf(id, プラグインの中のパス) = 入っているファイルの SHA-256。なければ nullopt。
// 入っているコピーにだけあるファイルは見ない。
using InstalledHashOf = std::function<std::optional<std::string>(const PluginId& id, const std::string& pathInPlugin)>;
std::vector<PluginId> StalePlugins(const std::vector<FileEntry>& list, const std::vector<PluginId>& installed,
                                   const InstalledHashOf& installedHashOf);

// --- エンジンを更新した後 (起動のたびに) ------------------------------------------------
// 入っているプラグインはエンジンのコピーで、新しいエンジンは新しいコピー (plugin.json、
// 翻訳、テーマのファイルなど) を持ってくることがある。そもそも調べるかどうか: エンジンの
// ファイル一覧の SHA-256 が、前回調べた後に残した印と違うとき (一覧がなければいつも調べる。
// その場合は plugin.json だけを比べるので軽い)。
bool EngineChanged(const std::optional<std::string>& fileListHash, const std::string& keptStamp);
// 入っているプラグインのうち (このエンジンにもあるもの) どれをコピーし直すか: ファイル一覧が
// あれば、ファイルが違うもの (StalePlugins)。なければ plugin.json が違うもの (manifestDiffers)。
using ManifestDiffers = std::function<bool(const PluginId& id)>;
std::vector<PluginId> PluginsToRefresh(const std::optional<std::vector<FileEntry>>& list, const std::vector<PluginId>& installed,
                                       const InstalledHashOf& installedHashOf, const ManifestDiffers& manifestDiffers);

}  // namespace rm::editor::integrity
