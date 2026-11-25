#include "rm_engine/weather_x_climate/editor/Register.h"

#include "editor/lang/Lang.h"
#include "rm_engine/climate/editor/ClimatePages.h"
#include "rm_engine/game_production/editor/SceneSettingLook.h"
#include "rm_engine/weather/runtime/Weather.h"

namespace rm::weather_x_climate {

void RegisterEditor(editor::PluginRegistry& registry) {
    // 気候のタブの「天候」: シーンの天気 (雲・降水量・霧・雷・風。テンプレートの行も)。
    static const climate::ClimatePageInfo kWeather{editor::TrKey(L"天候"), &reflect::TypeOf<weather::Weather>};
    registry.Contribute(climate::kClimatePagePoint, kWeather);
    // 天候の「雪にする」は出さない: 雨か雪かは、その場所の気温 (気候の地図・インスペクターの気候) で決まる。
    static const game_production::SceneSettingHide kSnowing{&reflect::TypeOf<weather::Weather>, "snowing"};
    registry.Contribute(game_production::kSceneSettingHidePoint, kSnowing);
}

}  // namespace rm::weather_x_climate
