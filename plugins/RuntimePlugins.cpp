// RUNTIME の部分が何かを登録するプラグイン (core/RuntimeRegistry.h): ゲームが動くときに使う
// システムと部品の型。ここでは runtime/ のヘッダーしか include しないので、書き出したゲーム
// (エディター無し) もこの一覧をリンクできる。エディターのローダーも使う (各 RegisterEditor の前に)。
// エディター専用のものがここに入っていないかは tools/check_layers.ps1 が調べる。

#include "core/RuntimeRegistry.h"
#include "rm_engine/game_production/runtime/Id.h"
#include "rm_engine/game_production/runtime/Register.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/game_x_3d/runtime/Register.h"
#include "rm_engine/game_x_2d/runtime/Id.h"
#include "rm_engine/game_x_2d/runtime/Register.h"
#include "rm_engine/physics/runtime/Id.h"
#include "rm_engine/physics/runtime/Register.h"
#include "rm_engine/game_x_audio/runtime/Id.h"
#include "rm_engine/game_x_audio/runtime/Register.h"
#include "rm_engine/game_ui/runtime/Id.h"
#include "rm_engine/game_ui/runtime/Register.h"
#include "rm_engine/game_x_particles/runtime/Id.h"
#include "rm_engine/game_x_particles/runtime/Register.h"
#include "rm_engine/particles_x_physics/runtime/Id.h"
#include "rm_engine/particles_x_physics/runtime/Register.h"
#include "rm_engine/weather/runtime/Id.h"
#include "rm_engine/weather/runtime/Register.h"
#include "rm_engine/weather_x_physics/runtime/Id.h"
#include "rm_engine/weather_x_physics/runtime/Register.h"
#include "rm_engine/climate/runtime/Id.h"
#include "rm_engine/climate/runtime/Register.h"
#include "rm_engine/weather_x_climate/runtime/Id.h"
#include "rm_engine/weather_x_climate/runtime/Register.h"

namespace rm {

std::span<const RuntimePlugin> RuntimePlugins() {
    static constexpr RuntimePlugin kPlugins[] = {
        {game_production::kPluginId, &game_production::RegisterRuntime},
        {game_x_3d::kPluginId, &game_x_3d::RegisterRuntime},
        {game_x_2d::kPluginId, &game_x_2d::RegisterRuntime},
        {physics::kPluginId, &physics::RegisterRuntime},
        {game_x_audio::kPluginId, &game_x_audio::RegisterRuntime},
        {game_ui::kPluginId, &game_ui::RegisterRuntime},
        {game_x_particles::kPluginId, &game_x_particles::RegisterRuntime},
        {particles_x_physics::kPluginId, &particles_x_physics::RegisterRuntime},
        {weather::kPluginId, &weather::RegisterRuntime},
        {weather_x_physics::kPluginId, &weather_x_physics::RegisterRuntime},
        {climate::kPluginId, &climate::RegisterRuntime},
        {weather_x_climate::kPluginId, &weather_x_climate::RegisterRuntime},
    };
    return kPlugins;
}

}  // namespace rm
