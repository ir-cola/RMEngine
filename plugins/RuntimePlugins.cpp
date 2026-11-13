// The plugins whose RUNTIME part registers something (core/RuntimeRegistry.h): the systems and the
// component types a game runs with. Only runtime/ headers are included here, so an exported game
// (no editor) can link this list; the editor's loader uses it too (before each RegisterEditor).
// tools/check_layers.ps1 checks that nothing here is editor-only.

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/game_x_3d/runtime/Register.h"
#include "rm_engine/game_x_2d/runtime/Id.h"
#include "rm_engine/game_x_2d/runtime/Register.h"
#include "rm_engine/physics/runtime/Id.h"
#include "rm_engine/physics/runtime/Register.h"
#include "rm_engine/game_x_audio/runtime/Id.h"
#include "rm_engine/game_x_audio/runtime/Register.h"

namespace rm {

std::span<const RuntimePlugin> RuntimePlugins() {
    static constexpr RuntimePlugin kPlugins[] = {
        {game_x_3d::kPluginId, &game_x_3d::RegisterRuntime},
        {game_x_2d::kPluginId, &game_x_2d::RegisterRuntime},
        {physics::kPluginId, &physics::RegisterRuntime},
        {game_x_audio::kPluginId, &game_x_audio::RegisterRuntime},
    };
    return kPlugins;
}

}  // namespace rm
