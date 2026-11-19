#include "rm_engine/weather/runtime/Register.h"

#include "rm_engine/game_production/runtime/Components.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/game_x_3d/runtime/WorldDraw.h"
#include "rm_engine/weather/runtime/ScriptWeather.h"
#include "rm_engine/weather/runtime/Weather.h"
#include "rm_engine/weather/runtime/WeatherDraw.h"

namespace rm::weather {

namespace {

// What the scene's 天候 does to its sky (地球の空's clouds, lightning, the wind they drift with).
bool SkyWeatherFor(const game_production::Scene& scene, double seconds, game_x_3d::SkyWeather& out) {
    const Weather* w = scene.Setting<Weather>();
    if (!w) return false;
    out = SkyWeatherOf(*w, seconds);
    return true;
}

}  // namespace

void RegisterRuntime(RuntimeRegistry& registry) {
    // The scene's 天候 (a scene setting: shown in a 3D game), and the system that moves it on.
    static const game_production::SceneSettingInfo kWeather{.type = &reflect::TypeOf<Weather>, .space = "3d"};
    registry.Contribute(game_production::kSceneSettingPoint, kWeather);
    static const game_production::SystemInfo kWeatherSystem{"天候", game_production::SystemPhase::Frame, 0, &RunWeather};
    registry.Contribute(game_production::kSystemPoint, kWeatherSystem);
    // The sky's clouds, lightning and wind (ゲーム制作 × 3D's 地球の空).
    static const game_x_3d::SkyWeatherInfo kSky{&SkyWeatherFor};
    registry.Contribute(game_x_3d::kSkyWeatherPoint, kSky);
    // Rain, snow and 濡れ in the 3D pictures: the scene view, the game view and the exported game.
    static const game_x_3d::WorldDrawInfo kDraw{&CreateWeatherDraw};
    registry.Contribute(game_x_3d::kWorldDrawPoint, kDraw);
    // Scripts: rm::Sky::SetWeather / SetRain / SetWind ... (sdk/RMWeather.h).
    registry.Contribute(game_production::kScriptApiPoint, ScriptWeatherPart());
}

}  // namespace rm::weather

RM_RUNTIME_ENTRY(rm_engine, weather, rm::weather::RegisterRuntime)
