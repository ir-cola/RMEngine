#pragma once
// The plugins of this engine, shared by the Hub and every project
// (design RME_PLATFORM 5-a: a plugin installed in the Hub works in every project).
//
//   available   <exe folder>/Plugins/<author>/<name>/          what this engine has (comes with the engine)
//   installed   %LOCALAPPDATA%\RM_Engine\Plugins\<author>\<name>\   what the user installed
//
// Installing copies a plugin's folder from "available" to "installed", removing deletes it, and
// reinstalling copies it again (repairs broken files without passing through "not installed").
// The code itself is built into the exe (static linking): a plugin that is not installed, or whose
// requirements are not, is simply never initialised. The first start installs everything available.

#include "editor/plugin/PluginManifest.h"
#include "editor/plugin/Software.h"
#include "editor/project/Templates.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rm::editor {

class Plugins {
public:
    struct Entry {
        PluginStatus status;     // id, display name, state, reason
        bool available = false;  // this engine has its files, so it can be (re)installed
        bool installed = false;
        // A connector (its plugin.json's connectorFor): never chosen by hand - it comes along when what it
        // joins is installed. Shown apart (Hub) and not offered for installing (the editor's menu).
        bool connector = false;
        std::vector<PluginId> joins;  // what it requires (the two sides it joins)
    };

    void Load(const std::filesystem::path& availableDir, const std::filesystem::path& installedDir);
    // Reads everything again and rebuilds the registry. Only while no editor is open
    // (open workspaces point into the registry).
    void Reload();

    const std::vector<Entry>& Entries() const { return entries_; }
    const Entry* Find(const PluginId& id) const;
    bool IsEnabled(const PluginId& id) const;
    std::string DisplayName(const PluginId& id) const;  // the ID text for an unknown plugin
    // Where an installed plugin's files are (its theme files: themes/*.json).
    std::filesystem::path InstalledFolder(const PluginId& id) const;
    const PluginRegistry& Registry() const { return registry_; }

    // The project templates (the Hub's 新規作成): 白紙 and every template.json of the plugins this
    // engine has - installed or not (<engine plugins>/<author>/<name>/templates/<id>/template.json).
    const TemplateTree& Templates() const { return templates_; }
    // The softwares of the template tree with their plugins (the Hub's "ソフトで絞る").
    const std::vector<SoftwareGroup>& SoftwareGroups() const { return softwareGroups_; }
    // What to copy before a project can be made from the template tree's `node` (its plugins and those
    // below it, with their requirements and connectors). This engine lacks some -> *unavailable.
    std::vector<PluginId> TemplateInstallPlan(size_t node, std::vector<PluginId>* unavailable = nullptr) const;

    // What installing (or repairing) `id` copies: it and its requirements that are not installed
    // and usable already, requirements first. Requirements this engine does not have -> *unavailable.
    std::vector<PluginId> InstallPlan(const PluginId& id, std::vector<PluginId>* unavailable = nullptr) const;
    // Installed plugins that stop working without `id` (direct first, then indirect).
    std::vector<Dependent> Dependents(const PluginId& id) const;

    // File operations. They do not reload: call Reload() after a batch.
    bool Install(const std::vector<PluginId>& ids, std::string* error);  // also reinstalls installed ones
    bool Uninstall(const PluginId& id, std::string* error);

    // Each start, after Load: when the engine was updated (its file list changed since the last start),
    // installed plugins whose files differ from the engine's copies are copied again - what the Hub's
    // 再インストール does, without asking (they are the engine's files, not the user's). Plugins the
    // user removed stay removed. Reloads when anything was copied. Returns what was copied.
    // The stamp (SHA-256 of the file list) is kept in <installed>/engine_files.sha256.
    std::vector<PluginId> RefreshFromEngine(const std::filesystem::path& fileList);

private:
    bool CopyIn(const PluginId& id, std::string* error);
    void LoadTemplates();

    std::filesystem::path availableDir_, installedDir_;
    std::vector<PluginManifest> available_;  // manifests of the engine's copies
    std::vector<PluginManifest> installed_;  // manifests of the installed copies that could be read
    std::vector<Entry> entries_;             // one per plugin built into the exe
    TemplateTree templates_;
    std::vector<SoftwareGroup> softwareGroups_;
    PluginRegistry registry_;
};

}  // namespace rm::editor
