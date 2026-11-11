#pragma once
// What the plugins' RUNTIME parts offer each other - the part that also goes into an exported game,
// which has no editor. A game's systems (game_production::SystemInfo) and the component types a
// game needs are offered here, so the game exe and the editor's Play read the same list.
//
//   // plugins/<author>/<name>/runtime/Register.cpp
//   void RegisterRuntime(rm::RuntimeRegistry& r) {
//       static const game_production::SystemInfo kSpin{...};
//       r.Contribute(game_production::kSystemPoint, kSpin);
//   }
//   // listed once in plugins/RuntimePlugins.cpp: {kPluginId, &RegisterRuntime}
//
// The reader names the point and the item type; the core knows neither (design RME_PLATFORM 4-f).
// The editor's PluginRegistry (editor/plugin/Software.h) IS one of these: the loader calls a plugin's
// RegisterRuntime first, then its RegisterEditor, into the same registry.

#include "core/PluginId.h"

#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace rm {

template <class T>
struct Contributed {
    PluginId plugin;  // who offered it
    const T* item;
};

class RuntimeRegistry {
public:
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

    // Used by the loader around each plugin's registration.
    void BeginPlugin(const PluginId& id) { current_ = id; }
    void EndPlugin() { current_ = {}; }

protected:
    const PluginId& Current() const { return current_; }  // the plugin registering now

private:
    struct Contribution {
        PluginId plugin;
        std::string point;
        const void* item;
        const std::type_info* type;
    };
    PluginId current_;
    std::vector<Contribution> contributions_;
};

struct RuntimePlugin {
    PluginIdLiteral id;
    void (*registerRuntime)(RuntimeRegistry& registry);
};

// The plugins whose runtime part registers something. Defined in plugins/RuntimePlugins.cpp (it
// includes runtime parts only, so a game exe can link it).
std::span<const RuntimePlugin> RuntimePlugins();

// Registers the runtime part of each plugin of `enabled` (requirements first - the order given) that
// has one. The game exe calls it with the plugins it was exported with; the editor's loader with
// the enabled ones.
inline void RegisterRuntimeParts(RuntimeRegistry& registry, std::span<const PluginId> enabled) {
    for (const PluginId& id : enabled) {
        for (const RuntimePlugin& p : RuntimePlugins()) {
            if (p.id.Id() != id || !p.registerRuntime) continue;
            registry.BeginPlugin(id);
            p.registerRuntime(registry);
            registry.EndPlugin();
        }
    }
}

}  // namespace rm
