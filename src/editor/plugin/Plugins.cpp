#include "editor/plugin/Plugins.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "platform/Shell.h"

#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

void PluginRegistry::AddSoftware(SoftwareType type) {
    type.plugin = current_;
    if (const char* p = plugin_id_detail::PartProblem(type.id, 2, 64, "ソフトの ID が短すぎます", "ソフトの ID が長すぎます")) {
        log::Error("プラグイン {} のソフト「{}」を登録できません: {}", current_.Text(), type.id, p);
        return;
    }
    for (const auto& s : softwares_) {
        if (s.plugin == type.plugin && s.id == type.id) {
            log::Error("プラグイン {} がソフト「{}」を 2 回登録しました", current_.Text(), type.id);
            return;
        }
    }
    if (!type.create) {
        log::Error("プラグイン {} のソフト「{}」に作り方 (create) がありません", current_.Text(), type.id);
        return;
    }
    softwares_.push_back(std::move(type));
}

namespace {

struct ReadResult {
    bool found = false;
    std::optional<PluginManifest> manifest;
    std::string error;
};

ReadResult ReadManifest(const fs::path& folder) {
    ReadResult r;
    const fs::path file = folder / L"plugin.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) return r;
    r.found = true;
    std::string err;
    if (auto bytes = platform::ReadFileBytes(file, &err)) {
        json::ParseError pe;
        if (auto doc = json::Parse(*bytes, &pe)) {
            r.manifest = ParsePluginManifest(*doc, &r.error);
        } else {
            r.error = TrU8F("plugin.json が壊れています ({} 行 {} 文字目: {})", pe.line, pe.column, pe.message);
        }
    } else {
        r.error = err;
    }
    return r;
}

fs::path FolderOf(const fs::path& root, const PluginId& id) { return root / PathFromUtf8(id.author) / PathFromUtf8(id.name); }

std::string FsError(const std::error_code& ec) { return platform::ErrorText(static_cast<DWORD>(ec.value())); }

}  // namespace

void Plugins::Load(const fs::path& availableDir, const fs::path& installedDir) {
    availableDir_ = availableDir;
    installedDir_ = installedDir;
    std::error_code ec;
    const bool firstStart = !fs::exists(installedDir_, ec);
    Reload();  // the engine's copies are known (names in the log) before anything is installed
    if (firstStart) {
        // First start of this engine for this user: everything that comes with it is installed.
        fs::create_directories(installedDir_, ec);
        std::vector<PluginId> all;
        for (const BuiltinPlugin& b : BuiltinPlugins()) {
            if (fs::exists(FolderOf(availableDir_, b.id.Id()) / L"plugin.json", ec)) all.push_back(b.id.Id());
        }
        std::string err;
        if (!Install(all, &err)) log::Error("{}", err);
        log::Info("最初の起動なので、エンジンに入っているプラグインをすべてインストールしました ({} 個)", all.size());
        Reload();
    }
}

void Plugins::Reload() {
    const auto builtin = BuiltinPlugins();
    available_.clear();
    installed_.clear();
    entries_.clear();
    registry_ = PluginRegistry{};

    std::vector<PluginCandidate> candidates;
    std::vector<bool> availableFlags;
    for (const BuiltinPlugin& b : builtin) {
        PluginCandidate cand;
        cand.id = b.id.Id();
        const ReadResult engineCopy = ReadManifest(FolderOf(availableDir_, cand.id));
        if (engineCopy.manifest) available_.push_back(*engineCopy.manifest);
        availableFlags.push_back(engineCopy.manifest.has_value());

        std::error_code ec;
        cand.installed = fs::is_directory(FolderOf(installedDir_, cand.id), ec);
        if (cand.installed) {
            const ReadResult mine = ReadManifest(FolderOf(installedDir_, cand.id));
            cand.manifestFound = mine.found;
            cand.manifest = mine.manifest;
            cand.manifestError = mine.error;
            if (mine.manifest) installed_.push_back(*mine.manifest);
        } else {
            cand.manifest = engineCopy.manifest;  // for its display name
        }
        candidates.push_back(std::move(cand));
    }

    PluginResolution res = ResolvePlugins(candidates);
    for (size_t i = 0; i < candidates.size(); ++i) {
        PluginStatus st = res.statuses[i];
        if (!candidates[i].manifest) {
            for (const auto& m : available_) {
                if (m.id == st.id) st.displayName = m.displayName;  // the installed plugin.json is unreadable
            }
        }
        entries_.push_back({std::move(st), availableFlags[i], candidates[i].installed});
    }
    for (const size_t i : res.order) {
        if (!builtin[i].registerEditor) continue;  // only a runtime part (e.g. 3D 空間)
        registry_.BeginPlugin(entries_[i].status.id);
        builtin[i].registerEditor(registry_);
        registry_.EndPlugin();
    }
    for (const auto& e : entries_) {
        const PluginStatus& s = e.status;
        if (s.state == PluginState::Enabled) {
            log::Info("プラグイン: {} ({})", s.displayName, s.id.Text());
        } else if (s.state != PluginState::NotInstalled) {
            log::Warn("プラグイン {} ({}) は使えません: {}", s.displayName, s.id.Text(), s.reason);
        }
    }
}

const Plugins::Entry* Plugins::Find(const PluginId& id) const {
    for (const auto& e : entries_) {
        if (e.status.id == id) return &e;
    }
    return nullptr;
}

bool Plugins::IsEnabled(const PluginId& id) const {
    const Entry* e = Find(id);
    return e && e->status.state == PluginState::Enabled;
}

std::filesystem::path Plugins::InstalledFolder(const PluginId& id) const {
    return installedDir_ / PathFromUtf8(id.author) / PathFromUtf8(id.name);
}

std::string Plugins::DisplayName(const PluginId& id) const {
    const Entry* e = Find(id);
    return e ? TrU8(e->status.displayName) : id.Text();
}

std::vector<PluginId> Plugins::InstallPlan(const PluginId& id, std::vector<PluginId>* unavailable) const {
    // "Ready" = installed, readable and the same as this engine's copy. A broken or older copy is copied again.
    return editor::InstallPlan(id, available_, ReadyPlugins(installed_, available_), unavailable);
}

std::vector<Dependent> Plugins::Dependents(const PluginId& id) const { return editor::Dependents(id, installed_); }

bool Plugins::CopyIn(const PluginId& id, std::string* error) {
    const fs::path from = FolderOf(availableDir_, id);
    const fs::path to = FolderOf(installedDir_, id);
    fs::path temp = to;
    temp += L".installing";
    std::error_code ec;
    // Copy next to the target first, then swap: a failure in the middle leaves the old copy as it was.
    fs::remove_all(temp, ec);
    fs::create_directories(temp.parent_path(), ec);
    fs::copy(from, temp, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (!ec) fs::remove_all(to, ec);
    if (!ec) fs::rename(temp, to, ec);
    if (ec) {
        fs::remove_all(temp, ec);
        if (error) *error = TrU8F("「{}」をインストールできませんでした: {}", DisplayName(id), FsError(ec));
        return false;
    }
    log::Info("プラグインをインストールしました: {} ({})", DisplayName(id), id.Text());
    return true;
}

bool Plugins::Install(const std::vector<PluginId>& ids, std::string* error) {
    for (const PluginId& id : ids) {
        if (!CopyIn(id, error)) return false;
    }
    return true;
}

bool Plugins::Uninstall(const PluginId& id, std::string* error) {
    std::error_code ec;
    fs::remove_all(FolderOf(installedDir_, id), ec);
    if (ec) {
        if (error) *error = TrU8F("「{}」を削除できませんでした: {}", DisplayName(id), FsError(ec));
        return false;
    }
    log::Info("プラグインを削除しました: {} ({})", DisplayName(id), id.Text());
    return true;
}

}  // namespace rm::editor
