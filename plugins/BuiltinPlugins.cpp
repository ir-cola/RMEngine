// The plugins built into this exe (static linking: design RME_PLATFORM 10 (a)).
//
// The only file that knows every plugin. The core declares BuiltinPlugins() and never includes
// a plugin; tools/check_layers.ps1 checks that every plugin with an editor part is listed here.
// A plugin with nothing to register (2D 空間: its runtime, and an editor part that is only a library
// for other plugins' editors - the gizmo; a theme pack with only theme files) is listed too, with no
// RegisterEditor - it can be installed and required like any other. (3D 空間's editor part is mostly
// such a library too, and registers only the 3D models' thumbnails.)
// Order does not matter: the loader enables requirements first.

#include "editor/plugin/Software.h"
#include "rm_engine/audio/editor/Preview.h"
#include "rm_engine/audio/runtime/Id.h"
#include "rm_engine/particles/runtime/Id.h"
#include "rm_engine/game_x_particles/editor/Connector.h"
#include "rm_engine/game_x_particles/runtime/Id.h"
#include "rm_engine/game_production/editor/GameSoftware.h"
#include "rm_engine/game_production/runtime/Id.h"
#include "rm_engine/game_x_3d/editor/Connector.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/game_x_2d/editor/Connector.h"
#include "rm_engine/game_x_2d/runtime/Id.h"
#include "rm_engine/physics/editor/Connector.h"
#include "rm_engine/physics/runtime/Id.h"
#include "rm_engine/game_x_audio/editor/Connector.h"
#include "rm_engine/game_x_audio/runtime/Id.h"
#include "rm_engine/game_ui/editor/Connector.h"
#include "rm_engine/game_ui/runtime/Id.h"
#include "rm_engine/space2d/runtime/Id.h"
#include "rm_engine/space3d/editor/Thumbnail.h"
#include "rm_engine/space3d/runtime/Id.h"
#include "rm_engine/theme_colors/runtime/Id.h"
#include "rm_engine/theme_paper/runtime/Id.h"
#include "rm_engine/theme_seasons/editor/Seasons.h"
#include "rm_engine/theme_seasons/runtime/Id.h"

namespace rm::editor {

std::span<const BuiltinPlugin> BuiltinPlugins() {
    static constexpr BuiltinPlugin kPlugins[] = {
        {game_production::kPluginId, &game_production::RegisterEditor},
        {audio::kPluginId, &audio::RegisterEditor},
        {particles::kPluginId, nullptr},
        {game_x_particles::kPluginId, &game_x_particles::RegisterEditor},
        {space3d::kPluginId, &space3d::RegisterEditor},
        {game_x_3d::kPluginId, &game_x_3d::RegisterEditor},
        {space2d::kPluginId, nullptr},
        {game_x_2d::kPluginId, &game_x_2d::RegisterEditor},
        {physics::kPluginId, &physics::RegisterEditor},
        {game_x_audio::kPluginId, &game_x_audio::RegisterEditor},
        {game_ui::kPluginId, &game_ui::RegisterEditor},
        {theme_colors::kPluginId, nullptr},  // theme files only (themes/*.json)
        {theme_paper::kPluginId, nullptr},
        {theme_seasons::kPluginId, &theme_seasons::RegisterEditor},
    };
    return kPlugins;
}

}  // namespace rm::editor
