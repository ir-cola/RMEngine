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

fs::path WithSuffix(const fs::path& p, const wchar_t* suffix) {
    fs::path out = p;
    out += suffix;
    return out;
}

// A copy that was put aside (<folder>.old) and never replaced - the app stopped between the two
// renames of CopyIn: the old copy goes back, so the plugin is never lost.
void PutBackInterrupted(const fs::path& to) {
    const fs::path aside = WithSuffix(to, L".old");
    std::error_code ec;
    if (fs::is_directory(to, ec) || !fs::is_directory(aside, ec)) return;
    fs::rename(aside, to, ec);
    if (ec) {
        log::Error("途中で止まったインストールの前の写しを戻せませんでした: {} ({})", PathToUtf8(aside), FsError(ec));
    } else {
        log::Warn("途中で止まったインストールがあったので、前の写しに戻しました: {}", PathToUtf8(to));
    }
}

}  // namespace

void Plugins::Load(const fs::path& availableDir, const fs::path& installedDir, const fs::path& legacyDir) {
    availableDir_ = availableDir;
    installedDir_ = installedDir;
    std::error_code ec;
    const bool firstStart = !fs::exists(installedDir_, ec);
    Reload();  // the engine's copies are known (names in the log) before anything is installed
    if (firstStart) {
        // First start of this engine for this user: what was installed in the folder every engine
        // shared before (the user's choice carries over), else everything that comes with it.
        fs::create_directories(installedDir_, ec);
        std::vector<PluginId> available, legacy;
        for (const BuiltinPlugin& b : BuiltinPlugins()) {
            if (fs::exists(FolderOf(availableDir_, b.id.Id()) / L"plugin.json", ec)) available.push_back(b.id.Id());
            if (!legacyDir.empty() && fs::exists(FolderOf(legacyDir, b.id.Id()) / L"plugin.json", ec)) legacy.push_back(b.id.Id());
        }
        const std::vector<PluginId> first = FirstStartInstalls(available, legacy);
        std::string err;
        if (!Install(first, &err)) log::Error("{}", err);
        if (legacy.empty()) {
            log::Info("このエンジンの最初の起動なので、エンジンに入っているプラグインをすべてインストールしました ({} 個)", first.size());
        } else {
            log::Info("このエンジンの最初の起動なので、前に入れていたプラグイン ({}) と同じものをインストールしました ({} 個)", PathToUtf8(legacyDir), first.size());
        }
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
        PutBackInterrupted(FolderOf(installedDir_, cand.id));
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
    const fs::path temp = WithSuffix(to, L".installing");
    const fs::path aside = WithSuffix(to, L".old");
    auto fail = [&](const wchar_t* step, const std::error_code& why) {
        std::error_code ignored;
        fs::remove_all(temp, ignored);
        if (error) *error = TrU8F("「{}」をインストールできませんでした ({}): {}", DisplayName(id), WideToUtf8(Tr(step)), FsError(why));
        return false;
    };
    // The old copy is never deleted before the new one is in place:
    //   1. copy the engine's files next to it (<folder>.installing)    fails -> the old copy is untouched
    //   2. put the old copy aside (<folder>.old)                        fails -> the old copy is untouched
    //   3. the new copy takes its place                                 fails -> the old copy goes back
    //   4. the old copy is deleted                                      fails -> only a leftover (removed next time)
    PutBackInterrupted(to);
    std::error_code ec;
    fs::remove_all(temp, ec);
    fs::remove_all(aside, ec);
    fs::create_directories(temp.parent_path(), ec);
    std::error_code copyEc;
    fs::copy(from, temp, fs::copy_options::recursive | fs::copy_options::overwrite_existing, copyEc);
    if (copyEc) return fail(TrKey(L"ファイルを写す"), copyEc);

    const bool hadOld = fs::is_directory(to, ec);
    if (hadOld) {
        std::error_code asideEc;
        fs::rename(to, aside, asideEc);
        if (asideEc) return fail(TrKey(L"前の写しをよける"), asideEc);  // a file inside is open in another app ...
    }
    std::error_code placeEc;
    fs::rename(temp, to, placeEc);
    if (placeEc) {
        if (hadOld) {
            std::error_code backEc;
            fs::rename(aside, to, backEc);
            if (backEc) log::Error("前の写しを戻せませんでした: {} ({})", PathToUtf8(aside), FsError(backEc));
        }
        return fail(TrKey(L"新しい写しを置く"), placeEc);
    }
    if (hadOld) {
        std::error_code removeEc;
        fs::remove_all(aside, removeEc);
        if (removeEc) log::Warn("前の写しを消せませんでした (次のインストールで消します): {} ({})", PathToUtf8(aside), FsError(removeEc));
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

std::vector<PluginId> Plugins::InstalledAndAvailable() const {
    std::vector<PluginId> out;
    for (const Entry& e : entries_) {
        if (e.installed && e.available) out.push_back(e.status.id);
    }
    return out;
}

Plugins::EngineRefreshPlan Plugins::PlanEngineRefresh(const fs::path& fileList, const fs::path& availableDir, const fs::path& installedDir,
                                                      const std::vector<PluginId>& candidates) {
    // Any thread: reads files only (no log, no translations, nothing of the app).
    EngineRefreshPlan plan;
    std::error_code ec;
    if (!fs::is_directory(installedDir, ec)) return plan;
    plan.listHash = platform::Sha256File(fileList);
    std::string kept;
    if (auto bytes = platform::ReadFileBytes(installedDir / L"engine_files.sha256", nullptr)) kept = *bytes;
    if (!integrity::EngineChanged(plan.listHash, kept)) return plan;
    plan.looked = true;

    std::optional<std::vector<integrity::FileEntry>> list;
    if (plan.listHash) {
        std::string err;
        auto bytes = platform::ReadFileBytes(fileList, &err);
        auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
        if (doc) list = integrity::ParseFileList(*doc, nullptr);  // (no translated message off the UI thread)
        if (!list) plan.listError = err.empty() ? PathToUtf8(fileList) : err;
    }
    plan.refresh = integrity::PluginsToRefresh(
        list, candidates,
        [&](const PluginId& id, const std::string& rel) { return platform::Sha256File(FolderOf(installedDir, id) / PathFromUtf8(rel)); },
        [&](const PluginId& id) {
            const auto engine = platform::ReadFileBytes(FolderOf(availableDir, id) / L"plugin.json", nullptr);
            const auto mine = platform::ReadFileBytes(FolderOf(installedDir, id) / L"plugin.json", nullptr);
            return engine && (!mine || *mine != *engine);  // an unreadable engine copy is never copied
        });
    return plan;
}

std::vector<PluginId> Plugins::ApplyEngineRefresh(const EngineRefreshPlan& plan) {
    if (!plan.looked) return {};
    if (!plan.listError.empty()) log::Warn("ファイルの一覧を読めないので、入れたプラグインは plugin.json だけを比べました: {}", plan.listError);
    std::vector<PluginId> copied;
    bool allCopied = true;
    for (const PluginId& id : plan.refresh) {
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
    if (plan.listHash && allCopied) {
        std::string err;
        if (!platform::WriteFileAtomic(installedDir_ / L"engine_files.sha256", *plan.listHash, &err)) {
            log::Warn("プラグインの確認の印を書けませんでした: {}", err);
        }
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
