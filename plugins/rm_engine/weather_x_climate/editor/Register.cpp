#include "rm_engine/weather_x_climate/editor/Register.h"

#include "editor/lang/Lang.h"
#include "rm_engine/climate/editor/ClimatePages.h"
#include "rm_engine/weather/runtime/Weather.h"

namespace rm::weather_x_climate {

void RegisterEditor(editor::PluginRegistry& registry) {
    // 気候のタブの「天候」: シーンの天気 (雲・降水量・霧・雷・風。テンプレートの行も)。
    static const climate::ClimatePageInfo kWeather{editor::TrKey(L"天候"), &reflect::TypeOf<weather::Weather>};
    registry.Contribute(climate::kClimatePagePoint, kWeather);
}

}  // namespace rm::weather_x_climate
