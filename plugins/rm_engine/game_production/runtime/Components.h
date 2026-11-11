#pragma once
// Components of the game's entities (the C of ECS). The core does not know the word: game
// production defines what a component is, and other plugins - the connectors - offer component
// types without game production knowing them (design RME_PLATFORM 4-f: game production never
// includes 3D 空間; "ゲーム制作 × 3D" offers 3D 空間's Transform).
//
//   // in a connector's RegisterEditor:
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

struct ComponentInfo {
    const reflect::TypeDesc& (*type)();  // e.g. &reflect::TypeOf<space3d::Transform>
    bool onNewEntities = false;          // every new entity gets one
    // Optional: a second way to show a child's component - values worked out from it and the same
    // component of every parent (a Transform in the world). The inspector puts a switch ownLabel /
    // derivedLabel above its fields (only for a child: at the top level both are the same); it starts
    // with its own values (game production does not know what they mean). All five or none.
    //   derive:   `chain` = the top parent's value first, this entity's last (parents without the
    //             component are skipped) -> `out`, a value of derivedType().
    //   underive: the derived values the user typed -> `component` (a copy of chain's last, changed
    //             so that it derives them).
    const reflect::TypeDesc& (*derivedType)() = nullptr;
    void (*derive)(std::span<const void* const> chain, void* out) = nullptr;
    void (*underive)(std::span<const void* const> chain, const void* derived, void* component) = nullptr;
    const wchar_t* ownLabel = nullptr;      // e.g. ローカル (a TrKey string: translated when shown)
    const wchar_t* derivedLabel = nullptr;  // e.g. ワールド

    // The picture of an entity that has this component in the 階層 (a name the core knows: "cube").
    // When several components have one, the last of the entity's components wins; none: "object".
    const char* icon = nullptr;

    // An entity may have several of it (each its own block in the inspector; they run in order).
    bool multiple = false;
    // Where a placed prefab is (a Transform): on the root of a placed prefab its values always stay
    // the placed one's own when the prefab changes (Prefab.h).
    bool placement = false;
    // The space it belongs to (a SceneViewInfo::space, e.g. "3d"): `onNewEntities` then means "every
    // new entity while the game's space is this one" (a 2D game's new entities get a Transform 2D, not a
    // 3D Transform). Null: any space.
    const char* space = nullptr;

    bool HasDerived() const { return derivedType && derive && underive && ownLabel && derivedLabel; }
};

struct Component {
    std::string type;          // TypeDesc::name, e.g. "rm::space3d::Transform"
    reflect::Object value;     // empty when the type is not offered now
    reflect::Unknowns unknowns;  // keys of a newer version inside "data"
    json::Value raw;           // the component as read (unknown keys - or all of it when the type is not offered)

    bool Known() const { return !value.Empty(); }
};

}  // namespace rm::game_production
