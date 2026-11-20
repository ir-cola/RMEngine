#pragma once
// The editor part of a scene setting (the type and what it is to the game are the runtime part's:
// SceneSettingInfo, runtime/Components.h): a menu on its block in the インスペクター, offered by the
// plugin that offers the setting, from its RegisterEditor.
//
//   static const game_production::SceneSettingLook kSkyLook{.type = &reflect::TypeOf<Sky>, .menu = &SkyMenu};
//   registry.Contribute(game_production::kSceneSettingLookPoint, kSkyLook);
//
// A setting without a look is shown plainly (its fields; edits go to the history as any field).

#include "core/reflect/Data.h"
#include "editor/plugin/Software.h"
#include "editor/widgets/Widgets.h"

#include <functional>
#include <string>
#include <vector>

namespace rm::game_production {

// PluginRegistry point: items are SceneSettingLook (static).
inline constexpr char kSceneSettingLookPoint[] = "rm_engine.game_production.scene_setting_look";

// What the menu's rows can do. Copied into the rows (they run after the menu closes).
struct SceneSettingMenuHost {
    editor::SoftwareHost* host = nullptr;
    // Play: the setting shown is the copy that plays - the rows that change it are greyed.
    bool play = false;
    // Changes the setting of the open scene: `change` gets it (a value of the look's type) and may
    // change any of its fields - one undo entry named `label` (UTF-8, Japanese).
    std::function<void(std::string label, std::function<void(void* setting)> change)> edit;
    // The same, and in Play the copy that plays, at once (nothing to undo: it goes back at ■).
    std::function<void(std::string label, std::function<void(void* setting)> change)> apply;
};

struct SceneSettingLook {
    const reflect::TypeDesc& (*type)();
    // The rows of the block's ⋮ menu (none: no menu).
    std::vector<editor::w::Action> (*menu)(const SceneSettingMenuHost& host) = nullptr;
    // Ready-made values set at once (天候's テンプレート): a row above its fields named presetLabel(), a
    // drop-down of these rows (each changes the setting through host.apply). None: no row.
    std::vector<editor::w::Action> (*presets)(const SceneSettingMenuHost& host) = nullptr;
    std::wstring (*presetLabel)() = nullptr;
};

}  // namespace rm::game_production
