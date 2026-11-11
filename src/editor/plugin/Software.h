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

#include "base/Json.h"
#include "core/PluginId.h"
#include "core/UndoHistory.h"
#include "core/reflect/FieldCommand.h"
#include "editor/dock/DockLayout.h"
#include "editor/dock/DockSpace.h"
#include "editor/tree/TreeView.h"
#include "platform/Gfx.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace rm::editor {

struct Project;
class PluginRegistry;

// The core panels every software has. A software's default layout places them by these ids.
namespace core_panel {
inline constexpr char kHierarchy[] = "core.hierarchy";
inline constexpr char kInspector[] = "core.inspector";
inline constexpr char kConsole[] = "core.console";
inline constexpr char kAssets[] = "core.assets";
}  // namespace core_panel

// What the core inspector shows: a title and a list of blocks ("lumps of values" - for a game's
// entity, the entity and then each component). Any reflected type works: the inspector draws it
// from its description, and edits go to this software's history as reflect::SetFieldCommand.
// The blocks are asked for every time the inspector is drawn, so after an undo (a component back
// or gone) it shows what is there now. A block whose object is gone is left out.
struct InspectBlock {
    const reflect::TypeDesc* type = nullptr;  // null: only `note` is shown
    std::function<reflect::EditTarget()> resolve;  // where the object is now (+ its unknowns); null object = gone
    std::wstring title;           // header (empty = none)
    std::wstring note;            // a line under the header
    std::vector<w::Action> menu;  // the header's ⋮ menu (e.g. 外す)
    // Ways to show the block (e.g. a Transform as ローカル / ワールド): a switch above its fields.
    // Choosing one calls setMode; the software then gives the block shown that way.
    std::vector<std::wstring> modes;
    int mode = 0;
    std::function<void(int)> setMode;
    // Set when the object shown is not the one edited (values worked out from it - a Transform shown
    // in the world): the software turns the change into edits of its own data and puts them in its
    // history. Unset: the change is a reflect::SetFieldCommand on `resolve`'s object. `merge` != 0:
    // typing into one field (inspector::Edit::merge) - one undo entry.
    struct Change {
        reflect::FieldPath path;
        json::Value before, after;
        std::string label;
        uint64_t merge = 0;
    };
    std::function<void(Change)> onEdit;
    // Several things selected: the same block of the others (same type), edited together with
    // `resolve`'s object. Values that differ show "—"; a change goes to all of them (for a vector
    // only the number typed) as one undo entry.
    std::vector<std::function<reflect::EditTarget()>> also;
    // Shown, not edited (a game in Play shows the copy that plays): the fields take no input.
    bool readOnly = false;
};

struct Inspectable {
    std::wstring title;  // on top; with no blocks it is all that is shown (e.g. "3 個選んでいます")
    std::function<std::vector<InspectBlock>()> blocks;
    std::wstring addLabel;  // a button under the blocks (e.g. "部品を足す") ...
    std::function<std::vector<w::Action>()> addMenu;  // ... and its menu

    // One object, no header (the game settings).
    static Inspectable Single(const reflect::TypeDesc& type, std::function<reflect::EditTarget()> resolve, std::wstring title) {
        Inspectable i;
        i.title = std::move(title);
        i.blocks = [&type, resolve = std::move(resolve)] { return std::vector<InspectBlock>{{&type, resolve, {}, {}, {}}}; };
        return i;
    }
};

// What the editor gives one software.
class SoftwareHost {
public:
    virtual ~SoftwareHost() = default;
    // This software's own history. Pushing an edit marks the whole project unsaved.
    virtual UndoHistory& History() = 0;
    virtual const Project& GetProject() const = 0;
    virtual void Redraw() = 0;
    // Shows the object in this software's インスペクター (an empty Inspectable clears it).
    virtual void Inspect(Inspectable target) = 0;
    // Selects items of this software's 階層 (after creating one, say); rename = start renaming it.
    virtual void SelectInTree(std::vector<tree::ItemId> ids, bool rename = false) = 0;
    // What the enabled plugins registered (their contributions, e.g. the components of a game).
    virtual const PluginRegistry& Registry() const = 0;
    // The GPU (Direct3D 11 + Direct2D), for software that draws with it (a 3D scene view).
    virtual platform::Gfx& Graphics() = 0;
    // The Windows clipboard, as text (UTF-8): copying entities puts them there as JSON, so they can
    // be pasted into another project (or another RM Engine). False / empty when it cannot be used.
    virtual bool SetClipboard(const std::string& textUtf8) = 0;
    virtual std::string Clipboard() = 0;
    // The file an AssetRef names now (in the project's Assets/, wherever it was moved or renamed to);
    // empty when it is not there. A value that is not an id but a path inside the project (data made
    // before references: "Assets/Models/robot.glb") is taken as that path, so old data still works.
    virtual std::filesystem::path AssetFile(const AssetRef& ref) const = 0;
};

// One software inside one open project.
class Software {
public:
    virtual ~Software() = default;
    // Panels this software adds (ids unique within the software, not starting with "core.").
    virtual std::vector<dock::PanelType> Panels() = 0;
    // The first layout (also "reset layout"). May use the core_panel ids.
    virtual dock::Layout DefaultLayout() const = 0;
    // What the core 階層 panel shows (game: entities, 3D model: meshes and bones ...). The core
    // draws the tree; the software lists the items and decides what renaming / moving / the
    // menu rows do. Null: the panel shows a hint.
    virtual tree::Source* Hierarchy() { return nullptr; }

    // The software's part of the project file (.rmpf "softwareData"). LoadData is called once after
    // the software is created - with null when the project has nothing yet. SaveData is called when
    // the project is saved; returning true keeps `data` in the file and records the plugin as used
    // by the project (it will not be opened without it). Unknown keys are the software's to keep.
    virtual void LoadData(const json::Value& data) { (void)data; }
    virtual bool SaveData(json::Value& data) const {
        (void)data;
        return false;
    }

    // How the user looks at this software (a scene view's camera, the gizmo chosen ...): not project
    // data - it is kept next to the layout in UserSettings/Layout.json, never marks the project
    // unsaved and is not undone. LoadView is called once after LoadData (null: nothing kept yet);
    // SaveView whenever the layout is saved (null: nothing to keep).
    virtual void LoadView(const json::Value& view) { (void)view; }
    virtual json::Value SaveView() const { return {}; }

    // Called once a frame, before the panels are drawn, while this software is the one shown (a game
    // that plays moves on here; it asks for the next frame with c.RequestAnimation()).
    virtual void Tick(ui::Context& c) { (void)c; }
    // The software's own controls in the tab row, right of the tabs (a game's ▶ Play ■). `r` is the
    // room there (the full height of the row).
    virtual void DrawToolbar(ui::Context& c, const ui::Rect& r) {
        (void)c;
        (void)r;
    }
    // True while the software runs something that its data must not be changed under (a game in
    // Play: it plays a copy, and goes back when stopped). The editor tints the tab row and does not
    // undo / redo meanwhile.
    virtual bool Running() const { return false; }
};

struct SoftwareType {
    PluginId plugin;      // filled in by the registry: the plugin that added it
    std::string id;       // unique within the plugin, e.g. "game" (same rules as a plugin name part)
    std::wstring title;   // tab label, e.g. L"ゲーム"
    std::function<std::unique_ptr<Software>(SoftwareHost&)> create;
};

// Something a plugin offers to another plugin through the core, which knows neither side nor what
// the item is. The plugin that reads a "point" names it and defines its item type, e.g. game
// production reads "rm_engine.game_production.component" items of game_production::ComponentInfo,
// and the 3D connector offers Transform there.
template <class T>
struct Contributed {
    PluginId plugin;  // who offered it
    const T* item;
};

class PluginRegistry {
public:
    void AddSoftware(SoftwareType type);
    const std::vector<SoftwareType>& Softwares() const { return softwares_; }

    // `item` must live as long as the program (a static). Only enabled plugins register, so only
    // their items are listed; requirements register first.
    template <class T>
    void Contribute(std::string_view point, const T& item) {
        contributions_.push_back({current_, std::string(point), &item, &typeid(T)});
    }
    // Items of `point` of type T, in registration order (an item of another type is skipped).
    template <class T>
    std::vector<Contributed<T>> Contributions(std::string_view point) const {
        std::vector<Contributed<T>> out;
        for (const auto& c : contributions_) {
            if (c.point == point && *c.type == typeid(T)) out.push_back({c.plugin, static_cast<const T*>(c.item)});
        }
        return out;
    }

    // Used by the loader around each plugin's RegisterEditor call.
    void BeginPlugin(const PluginId& id) { current_ = id; }
    void EndPlugin() { current_ = {}; }

private:
    struct Contribution {
        PluginId plugin;
        std::string point;
        const void* item;
        const std::type_info* type;
    };
    PluginId current_;
    std::vector<SoftwareType> softwares_;
    std::vector<Contribution> contributions_;
};

struct BuiltinPlugin {
    PluginIdLiteral id;  // the plugin's kPluginId (checked against its plugin.json when loading)
    void (*registerEditor)(PluginRegistry& registry);  // null for a plugin with only a runtime part
};

// Every plugin built into this exe. Defined in plugins/BuiltinPlugins.cpp.
std::span<const BuiltinPlugin> BuiltinPlugins();

}  // namespace rm::editor
