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

#include <span>
#include <string>

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
};

struct Component {
    std::string type;          // TypeDesc::name, e.g. "rm::space3d::Transform"
    reflect::Object value;     // empty when the type is not offered now
    reflect::Unknowns unknowns;  // keys of a newer version inside "data"
    json::Value raw;           // the component as read (unknown keys - or all of it when the type is not offered)

    bool Known() const { return !value.Empty(); }
};

}  // namespace rm::game_production
