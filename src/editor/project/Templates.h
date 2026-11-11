#pragma once
// Project templates and their tree (design RME_PLATFORM 1-b-2 / 5-a). Pure - tested in RMTests.
//
// A template is data, not code: a plugin offers one by having a file in its folder,
//
//   plugins/<author>/<name>/templates/<id>/template.json
//   {
//     "kind": "rm_engine.template", "format": 1,
//     "id": "game_3d",                       unique within the plugin (recorded in the new .rmpf)
//     "path": ["game", "3d"],                where in the tree: the branches above, then its own node
//     "name": "3D のゲーム", "description": "…",   Japanese (Tr when drawn; English in the plugin's lang/en.json)
//     "order": 0,                            among its siblings (smaller first; then by name)
//     "plugins": [ { "author": …, "name": … } ],   what its content needs (its own plugin is added)
//     "open": { "author": …, "plugin": …, "software": "game" },   the software shown first
//     "project": { "softwareData": [ … ] },  keys put into the new .rmpf (the first scene: camera, light, floor …)
//     "userSettings": { "softwares": [ { "author": …, "plugin": …, "software": …, "layout": …, "view": … } ] }
//   }                                        the first layout / view, put into UserSettings/Layout.json
//
// The core reads the files of the plugins this engine has - installed or not - so a template is in
// the tree before its plugins are installed (the Hub then offers to install them). The core knows
// neither the plugins nor what their data means: it copies "project" and "userSettings" over.
//
// The tree is made from the paths; a branch is only a way of showing (a node without a template of
// its own shows its id). Choosing a node needs every plugin of its template and of every template
// below it (design 09-29: "ゲーム" brings both 3D and 2D) - its own template is what the project starts as.

#include "base/Json.h"
#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor {

inline constexpr char kTemplateKind[] = "rm_engine.template";
inline constexpr int64_t kTemplateFormat = 1;

struct ProjectTemplate {
    std::optional<PluginId> plugin;  // who offers it; none: the core's own (白紙)
    std::string id;
    std::vector<std::string> path;   // the last part is its own node
    std::string name;                // UTF-8 Japanese
    std::string description;
    int order = 0;
    std::vector<PluginId> plugins;   // what its content needs (`plugin` first)
    json::Value open;                // { author, plugin, software } or null
    json::Value project;             // object or null
    json::Value userSettings;        // object or null
};

// The core's own template: a project with nothing in it (no plugin needed).
const ProjectTemplate& BlankTemplate();

// One template.json of `owner`'s folder. Unknown keys are allowed (a newer engine's); a newer
// format, a wrong shape, a bad id or path is refused with the reason (Japanese) in *error.
std::optional<ProjectTemplate> ParseTemplate(const json::Value& v, const PluginId& owner, std::string* error);

struct TemplateNode {
    std::vector<std::string> path;
    size_t depth = 0;                       // path.size() - 1
    size_t parent = static_cast<size_t>(-1);  // index of the parent node; npos at the top
    int tmpl = -1;                          // index into TemplateTree::templates; -1: a branch only
};

struct TemplateTree {
    std::vector<ProjectTemplate> templates;
    std::vector<TemplateNode> nodes;  // parents before their children, siblings in order (as shown)
    std::vector<std::string> warnings;  // templates left out (two at one place, a bad id ...)

    const ProjectTemplate* At(size_t node) const {
        return node < nodes.size() && nodes[node].tmpl >= 0 ? &templates[static_cast<size_t>(nodes[node].tmpl)] : nullptr;
    }
    // The text a node shows: its template's name, or (a branch only) its id. UTF-8, before Tr.
    std::string Label(size_t node) const;
    size_t Find(const std::vector<std::string>& path) const;  // npos when not there
};

// The tree of these templates. Two at the same place: the first stays, the other is a warning.
// Two of one plugin with the same id: the same.
TemplateTree BuildTemplateTree(std::vector<ProjectTemplate> templates);

// What making a project from `node` needs: the plugins of its template and of every template below
// it, each once, in tree order.
std::vector<PluginId> TemplatePlugins(const TemplateTree& tree, size_t node);

// What to copy before a project can be made from templates needing `needed`: each one's install
// plan (requirements and connectors, InstallPlan) put together, each once. Plugins this engine does
// not have go to *unavailable.
std::vector<PluginId> TemplateInstallPlan(const std::vector<PluginId>& needed, const std::vector<PluginManifest>& available,
                                          const std::vector<PluginId>& ready, std::vector<PluginId>* unavailable);

// A new project's .rmpf (kind, name ... already set) made from the template: the template is recorded
// ("template": { author, plugin, id }), its "project" keys are put in - except the core's own keys
// (kind, formatVersion, name, engineVersion, template, plugins) - and the plugin of each
// "softwareData" entry is recorded in "plugins", as saving that software's data would.
void ApplyTemplate(json::Value& doc, const ProjectTemplate& t);

// A new project's UserSettings/Layout.json from the template ("active" from "open", the
// "softwares" entries from "userSettings"); null when the template says nothing about either.
json::Value TemplateLayout(const ProjectTemplate& t);

// --- "ソフトで絞る" (the Hub's plugin list) --------------------------------------------------
// A software = a top-level node of the template tree. Its plugins: every plugin the templates below
// it need, with their requirements and connectors (as installing would bring them), and every
// plugin whose plugin.json says "software": [its id]. A software in a plugin.json with no node of
// that id comes after the others, named by its id. A node needing no plugin (白紙) is not one.
// A plugin in no software is 共通.
struct SoftwareGroup {
    std::string id;
    std::string name;  // UTF-8, before Tr
    std::vector<PluginId> plugins;
};
std::vector<SoftwareGroup> GroupPluginsBySoftware(const TemplateTree& tree, const std::vector<PluginManifest>& available);

}  // namespace rm::editor
