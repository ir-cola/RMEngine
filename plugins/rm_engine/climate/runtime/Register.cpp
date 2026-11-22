#include "rm_engine/climate/runtime/Register.h"

#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/climate/runtime/ScriptClimate.h"
#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Scripts.h"

namespace rm::climate {

void RegisterRuntime(RuntimeRegistry& registry) {
    // シーンの 気候 (エリアの外の気温と湿り気: 2D でも 3D でも)。
    static const game_production::SceneSettingInfo kSettings{.type = &reflect::TypeOf<ClimateSettings>};
    registry.Contribute(game_production::kSceneSettingPoint, kSettings);
    // 気候エリア: 3D と 2D。
    static const game_production::ComponentInfo kZone{.type = &reflect::TypeOf<ClimateZone>, .space = "3d", .folder = "気候"};
    registry.Contribute(game_production::kComponentPoint, kZone);
    static const game_production::ComponentInfo kZone2D{.type = &reflect::TypeOf<ClimateZone2D>, .space = "2d", .folder = "気候"};
    registry.Contribute(game_production::kComponentPoint, kZone2D);
    // スクリプト: rm::Climate::TemperatureAt ... (sdk/RMClimate.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptClimatePart());
}

}  // namespace rm::climate

RM_RUNTIME_ENTRY(rm_engine, climate, rm::climate::RegisterRuntime)
