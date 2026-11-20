#include "rm_engine/weather/editor/Register.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/SceneSettingLook.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <string>

namespace rm::weather {

namespace {

// テンプレート: a row for each ready-made weather - its name; picked, the fields are set to it (one undo
// entry; in Play the copy that plays, at once - the rain and the clouds change on the spot, the snow
// still comes in over about 10 seconds).
std::vector<editor::w::Action> Presets(const game_production::SceneSettingMenuHost& host) {
    std::vector<editor::w::Action> rows;
    const reflect::EnumDesc* kinds = reflect::FindEnum(typeid(WeatherPreset));
    if (!kinds) return rows;
    for (const reflect::EnumItem& item : kinds->items) {
        const auto preset = static_cast<WeatherPreset>(item.value);
        const std::string label = item.label;
        editor::w::Action a;
        a.label = Utf8ToWide(editor::TrU8(item.label));
        a.run = [apply = host.apply, preset, label] {
            if (apply) apply("天気のテンプレート「" + label + "」", [preset](void* setting) { ApplyPreset(*static_cast<Weather*>(setting), preset); });
        };
        rows.push_back(std::move(a));
    }
    return rows;
}

std::wstring PresetLabel() { return editor::Tr(L"テンプレート"); }

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    static const game_production::SceneSettingLook kWeather{.type = &reflect::TypeOf<Weather>, .presets = &Presets, .presetLabel = &PresetLabel};
    registry.Contribute(game_production::kSceneSettingLookPoint, kWeather);
}

}  // namespace rm::weather
