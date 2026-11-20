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

// シーンの 天候 が、その空にすること (地球の空の雲、雷、雲が流れる風)。
bool SkyWeatherFor(const game_production::Scene& scene, double seconds, game_x_3d::SkyWeather& out) {
    const Weather* w = scene.Setting<Weather>();
    if (!w) return false;
    out = SkyWeatherOf(*w, seconds);
    return true;
}

}  // namespace

void RegisterRuntime(RuntimeRegistry& registry) {
    // シーンの 天候 (シーン設定: 3D のゲームで表示する) と、それを進めるシステム。
    static const game_production::SceneSettingInfo kWeather{.type = &reflect::TypeOf<Weather>, .space = "3d"};
    registry.Contribute(game_production::kSceneSettingPoint, kWeather);
    static const game_production::SystemInfo kWeatherSystem{"天候", game_production::SystemPhase::Frame, 0, &RunWeather};
    registry.Contribute(game_production::kSystemPoint, kWeatherSystem);
    // 空の雲、雷、風 (ゲーム制作 × 3D の 地球の空)。
    static const game_x_3d::SkyWeatherInfo kSky{&SkyWeatherFor};
    registry.Contribute(game_x_3d::kSkyWeatherPoint, kSky);
    // 3D の絵の中の雨、雪、濡れ: シーンビュー、ゲームビュー、書き出したゲーム。
    static const game_x_3d::WorldDrawInfo kDraw{&CreateWeatherDraw};
    registry.Contribute(game_x_3d::kWorldDrawPoint, kDraw);
    // スクリプト: rm::Sky::SetWeather / SetRain / SetWind ... (sdk/RMWeather.h)。
    registry.Contribute(game_production::kScriptApiPoint, ScriptWeatherPart());
}

}  // namespace rm::weather

RM_RUNTIME_ENTRY(rm_engine, weather, rm::weather::RegisterRuntime)
