#pragma once
// What a plugin uses to add a "software" (game, illustration, music ...) to the editor.
//
//   plugin (editor part)  --RegisterEditor(PluginRegistry&)-->  SoftwareType { id, title, create }
//   editor opens a project --create(SoftwareHost&)----------->  Software (one per open project)
//
// Design RME_PLATFORM 4: softwares switch like windows (tabs), the panels of one software never
// mix with another's, each software has its own layout, its own selection and undo history
// (Ctrl+Z in music never undoes a mesh), and the project has a single "unsaved" mark.
// The core panels (hierarchy, inspector, console, assets) exist once per software.
//
// Plugins are built into the exe (static linking, design RME_PLATFORM 10 (a)). The list of them
// is in plugins/BuiltinPlugins.cpp - the core never includes a plugin header.

#include "core/PluginId.h"
#include "core/UndoHistory.h"
#include "editor/dock/DockLayout.h"
#include "editor/dock/DockSpace.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rm::editor {

struct Project;

// The core panels every software has. A software's default layout places them by these ids.
namespace core_panel {
inline constexpr char kHierarchy[] = "core.hierarchy";
inline constexpr char kInspector[] = "core.inspector";
inline constexpr char kConsole[] = "core.console";
inline constexpr char kAssets[] = "core.assets";
}  // namespace core_panel

// What the editor gives one software.
class SoftwareHost {
public:
    virtual ~SoftwareHost() = default;
    // This software's own history. Pushing an edit marks the whole project unsaved.
    virtual UndoHistory& History() = 0;
    virtual const Project& GetProject() const = 0;
    virtual void Redraw() = 0;
};

// One software inside one open project.
class Software {
public:
    virtual ~Software() = default;
    // Panels this software adds (ids unique within the software, not starting with "core.").
    virtual std::vector<dock::PanelType> Panels() = 0;
    // The first layout (also "reset layout"). May use the core_panel ids.
    virtual dock::Layout DefaultLayout() const = 0;
};

struct SoftwareType {
    PluginId plugin;      // filled in by the registry: the plugin that added it
    std::string id;       // unique within the plugin, e.g. "game" (same rules as a plugin name part)
    std::wstring title;   // tab label, e.g. L"ゲーム"
    std::function<std::unique_ptr<Software>(SoftwareHost&)> create;
};

class PluginRegistry {
public:
    void AddSoftware(SoftwareType type);
    const std::vector<SoftwareType>& Softwares() const { return softwares_; }

    // Used by the loader around each plugin's RegisterEditor call.
    void BeginPlugin(const PluginId& id) { current_ = id; }
    void EndPlugin() { current_ = {}; }

private:
    PluginId current_;
    std::vector<SoftwareType> softwares_;
};

struct BuiltinPlugin {
    PluginIdLiteral id;  // the plugin's kPluginId (checked against its plugin.json when loading)
    void (*registerEditor)(PluginRegistry& registry);
};

// Every plugin built into this exe. Defined in plugins/BuiltinPlugins.cpp.
std::span<const BuiltinPlugin> BuiltinPlugins();

}  // namespace rm::editor
