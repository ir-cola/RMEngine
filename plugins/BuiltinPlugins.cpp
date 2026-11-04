// The plugins built into this exe (static linking: design RME_PLATFORM 10 (a)).
//
// The only file that knows every plugin. The core declares BuiltinPlugins() and never includes
// a plugin; tools/check_layers.ps1 checks that every plugin with an editor part is listed here.
// Order does not matter: the loader enables requirements first.

#include "editor/plugin/Software.h"
#include "rm_engine/game_production/editor/GameSoftware.h"
#include "rm_engine/game_production/runtime/Id.h"

namespace rm::editor {

std::span<const BuiltinPlugin> BuiltinPlugins() {
    static constexpr BuiltinPlugin kPlugins[] = {
        {game_production::kPluginId, &game_production::RegisterEditor},
    };
    return kPlugins;
}

}  // namespace rm::editor
