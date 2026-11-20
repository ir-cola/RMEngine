#include "rm_engine/weather/editor/Register.h"

#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "rm_engine/game_production/editor/SceneSettingLook.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <string>

namespace rm::weather {

namespace {

// テンプレート: 出来合いの天候ごとの行 - その名前。選ぶと、欄がそれに設定される (Undo 1 回分。
// Play 中は、再生しているコピーに即座に反映する - 雨と雲はその場で変わり、雪は 10 秒ほどかけて
// 入ってくる)。
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
