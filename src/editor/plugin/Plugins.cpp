#include "editor/plugin/Plugins.h"

#include "editor/app/Integrity.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "platform/Shell.h"

#include <algorithm>
#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

void PluginRegistry::AddSoftware(SoftwareType type) {
    type.plugin = Current();
    if (const char* p = plugin_id_detail::PartProblem(type.id, 2, 64, "ソフトの ID が短すぎます", "ソフトの ID が長すぎます")) {
        log::Error("プラグイン {} のソフト「{}」を登録できません: {}", Current().Text(), type.id, p);
        return;
    }
    for (const auto& s : softwares_) {
        if (s.plugin == type.plugin && s.id == type.id) {
            log::Error("プラグイン {} がソフト「{}」を 2 回登録しました", Current().Text(), type.id);
            return;
        }
    }
    if (!type.create) {
        log::Error("プラグイン {} のソフト「{}」に作り方 (create) がありません", Current().Text(), type.id);
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
        Entry entry{std::move(st), availableFlags[i], candidates[i].installed};
        const PluginManifest* m = candidates[i].manifest ? &*candidates[i].manifest : nullptr;
        for (const auto& a : available_) {
            if (!m && a.id == entry.status.id) m = &a;
        }
        if (m && m->connectorFor) {
            entry.connector = true;
            entry.joins = m->requirements;
        }
        entries_.push_back(std::move(entry));
    }
    for (const size_t i : res.order) {
        // The runtime part first (what an exported game registers too), then the editor part.
        const PluginId id = entries_[i].status.id;
        RegisterRuntimeParts(registry_, std::span<const PluginId>(&id, 1));
        if (!builtin[i].registerEditor) continue;  // nothing of the editor's (e.g. 2D 空間)
        registry_.BeginPlugin(id);
        builtin[i].registerEditor(registry_);
        registry_.EndPlugin();
    }
    LoadTemplates();
    for (const auto& e : entries_) {
        const PluginStatus& s = e.status;
        if (s.state == PluginState::Enabled) {
            log::Info("プラグイン: {} ({})", s.displayName, s.id.Text());
        } else if (s.state != PluginState::NotInstalled) {
            log::Warn("プラグイン {} ({}) は使えません: {}", s.displayName, s.id.Text(), s.reason);
        }
    }
}

void Plugins::LoadTemplates() {
    std::vector<ProjectTemplate> list{BlankTemplate()};
    for (const BuiltinPlugin& b : BuiltinPlugins()) {
        const PluginId id = b.id.Id();
        std::error_code ec;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(FolderOf(availableDir_, id) / L"templates", ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (fs::is_regular_file(it->path() / L"template.json", e2)) files.push_back(it->path() / L"template.json");
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& file : files) {
            std::string err;
            auto bytes = platform::ReadFileBytes(file, &err);
            json::ParseError pe;
            auto doc = bytes ? json::Parse(*bytes, &pe) : std::nullopt;
            if (bytes && !doc) err = TrU8F("JSON が壊れています ({} 行 {} 文字目: {})", pe.line, pe.column, pe.message);
            auto t = doc ? ParseTemplate(*doc, id, &err) : std::nullopt;
            if (!t) {
                log::Warn("テンプレートを読めませんでした: {} ({})", PathToUtf8(file), err);
                continue;
            }
            list.push_back(std::move(*t));
        }
    }
    templates_ = BuildTemplateTree(std::move(list));
    for (const auto& w : templates_.warnings) log::Warn("テンプレート: {}", w);
    softwareGroups_ = GroupPluginsBySoftware(templates_, available_);
}

std::vector<PluginId> Plugins::TemplateInstallPlan(size_t node, std::vector<PluginId>* unavailable) const {
    return editor::TemplateInstallPlan(TemplatePlugins(templates_, node), available_, ReadyPlugins(installed_, available_), unavailable);
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

std::vector<PluginId> Plugins::RefreshFromEngine(const fs::path& fileList) {
    std::error_code ec;
    if (!fs::is_directory(installedDir_, ec)) return {};
    const fs::path stampFile = installedDir_ / L"engine_files.sha256";
    const std::optional<std::string> listHash = platform::Sha256File(fileList);
    std::string kept;
    if (auto bytes = platform::ReadFileBytes(stampFile, nullptr)) kept = *bytes;
    if (!integrity::EngineChanged(listHash, kept)) return {};

    std::optional<std::vector<integrity::FileEntry>> list;
    if (listHash) {
        std::string err;
        auto bytes = platform::ReadFileBytes(fileList, &err);
        auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
        if (doc) list = integrity::ParseFileList(*doc, &err);
        if (!list) log::Warn("ファイルの一覧を読めないので、入れたプラグインは plugin.json だけを比べます: {}", err);
    }
    std::vector<PluginId> candidates;
    for (const Entry& e : entries_) {
        if (e.installed && e.available) candidates.push_back(e.status.id);
    }
    const auto refresh = integrity::PluginsToRefresh(
        list, candidates,
        [&](const PluginId& id, const std::string& rel) { return platform::Sha256File(FolderOf(installedDir_, id) / PathFromUtf8(rel)); },
        [&](const PluginId& id) {
            const auto engine = platform::ReadFileBytes(FolderOf(availableDir_, id) / L"plugin.json", nullptr);
            const auto mine = platform::ReadFileBytes(FolderOf(installedDir_, id) / L"plugin.json", nullptr);
            return engine && (!mine || *mine != *engine);  // an unreadable engine copy is never copied
        });

    std::vector<PluginId> copied;
    bool allCopied = true;
    for (const PluginId& id : refresh) {
        std::string err;
        if (CopyIn(id, &err)) {
            copied.push_back(id);
        } else {
            allCopied = false;
            log::Error("{}", err);
        }
    }
    if (!copied.empty()) {
        log::Info("エンジンが新しくなったので、入れたプラグインを写し直しました ({} 個)", copied.size());
        Reload();
    }
    // Looked at again on the next start when something failed (or there is no list to stamp).
    if (listHash && allCopied) {
        std::string err;
        if (!platform::WriteFileAtomic(stampFile, *listHash, &err)) log::Warn("プラグインの確認の印を書けませんでした: {}", err);
    }
    return copied;
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
