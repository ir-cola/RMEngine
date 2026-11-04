#pragma once
// The plugins of this engine: read once at start-up, shared by the Hub and every project.
// (Design RME_PLATFORM 5-a: a plugin installed in the Hub works in every project.)

#include "editor/plugin/PluginManifest.h"
#include "editor/plugin/Software.h"

#include <filesystem>
#include <vector>

namespace rm::editor {

class Plugins {
public:
    // Reads <dir>/<author>/<name>/plugin.json of every built-in plugin, decides which can be used,
    // and lets those register what they add (requirements first).
    void Load(const std::filesystem::path& dir);

    const std::vector<PluginStatus>& Statuses() const { return statuses_; }
    const PluginRegistry& Registry() const { return registry_; }
    const PluginStatus* Find(const PluginId& id) const;
    bool IsEnabled(const PluginId& id) const;

private:
    std::vector<PluginStatus> statuses_;
    PluginRegistry registry_;
};

}  // namespace rm::editor
