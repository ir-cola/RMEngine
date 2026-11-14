#pragma once
// How the editor shows a component type - the editor part of a component (the type itself and what it
// is to the game are the runtime part's: ComponentInfo, runtime/Components.h). Offered by the plugin
// that offers the type, from its RegisterEditor (the words go through Tr, which the game has not):
//
//   static const game_production::ComponentLook kTransformLook{.type = &reflect::TypeOf<space3d::Transform>,
//       .derivedType = ..., .derive = ..., .underive = ..., .ownLabel = TrKey(L"ローカル"), .derivedLabel = TrKey(L"ワールド"), .icon = "cube"};
//   registry.Contribute(game_production::kComponentLookPoint, kTransformLook);
//
// A type without a look is shown plainly (the 階層's "object" picture, its own values only).

#include "core/reflect/Data.h"

#include <span>

namespace rm::game_production {

class Scene;
struct GameSettings;

// PluginRegistry point: items are ComponentLook (static).
inline constexpr char kComponentLookPoint[] = "rm_engine.game_production.component_look";

struct ComponentLook {
    const reflect::TypeDesc& (*type)();  // the component type it is for
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
    // Optional: a field of the component typed in the inspector (one entity's, its own values). `after`
    // is the component with that field changed, `before` as it was; the plugin may change its other
    // fields so that they go together (UI の配置: a new アンカー keeps the thing where it is on the
    // screen). Everything that changes is one undo entry. `scene` is as it was before the edit.
    void (*adjust)(const Scene& scene, const GameSettings& settings, int entity, const reflect::FieldPath& path, const void* before,
                   void* after) = nullptr;

    bool HasDerived() const { return derivedType && derive && underive && ownLabel && derivedLabel; }
};

}  // namespace rm::game_production
