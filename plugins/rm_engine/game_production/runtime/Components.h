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
    // Optional: values worked out from this component and the same component of every parent, shown
    // read-only under it (a Transform's place in the world - game production does not know what they
    // mean). `chain`: the top parent's value first, this entity's last (parents without the component
    // are skipped); `out`: a value of derivedType(). The block's title is derivedType()'s label.
    const reflect::TypeDesc& (*derivedType)() = nullptr;
    void (*derive)(std::span<const void* const> chain, void* out) = nullptr;
};

struct Component {
    std::string type;          // TypeDesc::name, e.g. "rm::space3d::Transform"
    reflect::Object value;     // empty when the type is not offered now
    reflect::Unknowns unknowns;  // keys of a newer version inside "data"
    json::Value raw;           // the component as read (unknown keys - or all of it when the type is not offered)

    bool Known() const { return !value.Empty(); }
};

}  // namespace rm::game_production
