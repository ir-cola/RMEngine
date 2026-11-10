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

    // What installing (or repairing) `id` copies: it and its requirements that are not installed
    // and usable already, requirements first. Requirements this engine does not have -> *unavailable.
    std::vector<PluginId> InstallPlan(const PluginId& id, std::vector<PluginId>* unavailable = nullptr) const;
    // Installed plugins that stop working without `id` (direct first, then indirect).
    std::vector<Dependent> Dependents(const PluginId& id) const;

    // File operations. They do not reload: call Reload() after a batch.
    bool Install(const std::vector<PluginId>& ids, std::string* error);  // also reinstalls installed ones
    bool Uninstall(const PluginId& id, std::string* error);

private:
    bool CopyIn(const PluginId& id, std::string* error);

    std::filesystem::path availableDir_, installedDir_;
    std::vector<PluginManifest> available_;  // manifests of the engine's copies
    std::vector<PluginManifest> installed_;  // manifests of the installed copies that could be read
    std::vector<Entry> entries_;             // one per plugin built into the exe
    PluginRegistry registry_;
};

}  // namespace rm::editor
