#include "editor/app/Integrity.h"

#include "editor/lang/Lang.h"
#include <algorithm>

namespace rm::editor::integrity {

std::optional<std::vector<FileEntry>> ParseFileList(const json::Value& v, std::string* error) {
    if (!v.IsObject() || v.Get("kind").AsString() != "rm_engine.files" || !v.Get("files").IsArray()) {
        if (error) *error = TrU8("RM_Engine.files.json の形が正しくありません");
        return std::nullopt;
    }
    std::vector<FileEntry> out;
    for (const auto& f : v.Get("files").Items()) {
        FileEntry e;
        e.path = f.Get("path").AsString();
        e.size = static_cast<uint64_t>(std::max<int64_t>(0, f.Get("size").AsInt(0)));
        e.sha256 = f.Get("sha256").AsString();
        std::transform(e.sha256.begin(), e.sha256.end(), e.sha256.begin(), [](char c) {
            return c >= 'A' && c <= 'F' ? static_cast<char>(c - 'A' + 'a') : c;
        });
        // エンジンのフォルダーの外へ出るパスは決して使わない。
        if (e.path.empty() || e.sha256.size() != 64 || e.path.find("..") != std::string::npos || e.path.front() == '/' ||
            e.path.find(':') != std::string::npos) {
            if (error) *error = TrU8("RM_Engine.files.json に読めない行があります: ") + e.path;
            return std::nullopt;
        }
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<FileProblem> CheckFiles(const std::vector<FileEntry>& list, const HashOf& hashOf) {
    std::vector<FileProblem> out;
    for (const FileEntry& e : list) {
        const auto h = hashOf(e.path);
        if (!h) {
            out.push_back({e.path, Problem::Missing});
        } else if (*h != e.sha256) {
            out.push_back({e.path, Problem::Changed});
        }
    }
    return out;
}

std::vector<PluginId> StalePlugins(const std::vector<FileEntry>& list, const std::vector<PluginId>& installed,
                                   const InstalledHashOf& installedHashOf) {
    std::vector<PluginId> out;
    for (const PluginId& id : installed) {
        const std::string prefix = "Plugins/" + id.author + "/" + id.name + "/";
        for (const FileEntry& e : list) {
            if (!e.path.starts_with(prefix)) continue;
            const auto h = installedHashOf(id, e.path.substr(prefix.size()));
            if (!h || *h != e.sha256) {
                out.push_back(id);
                break;
            }
        }
    }
    return out;
}

bool EngineChanged(const std::optional<std::string>& fileListHash, const std::string& keptStamp) {
    return !fileListHash || *fileListHash != keptStamp;
}

std::vector<PluginId> PluginsToRefresh(const std::optional<std::vector<FileEntry>>& list, const std::vector<PluginId>& installed,
                                       const InstalledHashOf& installedHashOf, const ManifestDiffers& manifestDiffers) {
    if (list) return StalePlugins(*list, installed, installedHashOf);
    std::vector<PluginId> out;
    for (const PluginId& id : installed) {
        if (manifestDiffers(id)) out.push_back(id);
    }
    return out;
}

}  // namespace rm::editor::integrity
