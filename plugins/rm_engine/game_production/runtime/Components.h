#pragma once
// Components of the game's entities (the C of ECS). The core does not know the word: game
// production defines what a component is, and other plugins - the connectors - offer component
// types without game production knowing them (design RME_PLATFORM 4-f: game production never
// includes 3D 空間; "ゲーム制作 × 3D" offers 3D 空間's Transform).
//
//   // in a connector's runtime part (runtime/Register.cpp - core/RuntimeRegistry.h):
//   static const game_production::ComponentInfo kTransform{&reflect::TypeOf<space3d::Transform>, true};
//   registry.Contribute(game_production::kComponentPoint, kTransform);
//
// A component type is any reflected struct. In the file it is named by its C++ name
// (TypeDesc::name); a component whose type is not offered now (its plugin is missing) is kept
// as it was read and written back unchanged.

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "core/reflect/Object.h"
#include "core/RuntimeRegistry.h"

#include <span>
#include <string>
#include <vector>

namespace rm::game_production {

// PluginRegistry point: items are ComponentInfo (static).
inline constexpr char kComponentPoint[] = "rm_engine.game_production.component";

// What a component type is to the game (the runtime part: an exported game reads the same list).
// How the editor shows it (the 階層's picture, ローカル / ワールド) is the editor part's: ComponentLook
// (game_production/editor/ComponentLook.h), offered by the same plugin's RegisterEditor.
struct ComponentInfo {
    const reflect::TypeDesc& (*type)();  // e.g. &reflect::TypeOf<space3d::Transform>
    bool onNewEntities = false;          // every new entity gets one
    // An entity may have several of it (each its own block in the inspector; they run in order).
    bool multiple = false;
    // Where a placed prefab is (a Transform): on the root of a placed prefab its values always stay
    // the placed one's own when the prefab changes (Prefab.h).
    bool placement = false;
    // The space it belongs to (a SceneViewInfo::space, e.g. "3d"): `onNewEntities` then means "every
    // new entity while the game's space is this one" (a 2D game's new entities get a Transform 2D, not a
    // 3D Transform). Null: any space.
    const char* space = nullptr;
    // The game can see through an entity with it (a 3D Camera, a Camera 2D): a scene switched to picks
    // its camera among these (SceneCamera).
    bool camera = false;
    // The folder it is listed in by the インスペクター's 部品を足す ("3D", "2D", "UI", "物理" ... UTF-8,
    // translated when shown; "/" between folders inside folders). Null: "その他". Scripts are in "スクリプト".
    const char* folder = nullptr;
};

// PluginRegistry point: items are SceneSettingInfo (static).
inline constexpr char kSceneSettingPoint[] = "rm_engine.game_production.scene_setting";

// A value the scene itself has, not one of its entities (物理's 重力, 3D's 空): every scene has one of
// each type offered (its defaults until changed), saved in the scene's file. The インスペクター shows
// them when the scene (the 階層's top row) is chosen; systems and drawers read them with
// Scene::Setting<T>().
struct SceneSettingInfo {
    const reflect::TypeDesc& (*type)();
    // The game's space it is for (a SceneViewInfo::space, e.g. "3d": the sky): the インスペクター shows
    // it only in a game of that space (it is kept, and saved, either way). Null: any space.
    const char* space = nullptr;
};

struct Component {
    std::string type;          // TypeDesc::name, e.g. "rm::space3d::Transform"
    reflect::Object value;     // empty when the type is not offered now
    reflect::Unknowns unknowns;  // keys of a newer version inside "data"
    json::Value raw;           // the component as read (unknown keys - or all of it when the type is not offered)

    bool Known() const { return !value.Empty(); }
};

// The component types written in a scene or a prefab file (Scene::Save's form: "entities", each with
// "components" [{"type": ...}], and a placed prefab's added components), each once, in the order found.
void ComponentTypesIn(const json::Value& sceneOrPrefab, std::vector<std::string>& out);

// The plugins offering the component types `used` (the record of what a project uses: design
// RME_PLATFORM 5-a, condition 1 - an exported game links them). Each once, in the order of `offered`.
// A type nobody offers now (its plugin is missing) gives nothing.
std::vector<PluginId> PluginsOffering(const std::vector<std::string>& used, const std::vector<Contributed<ComponentInfo>>& offered);

}  // namespace rm::game_production
