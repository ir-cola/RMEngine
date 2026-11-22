// スクリプト API の気候の部分 (RMClimate.h)。RmApi::ctx はゲーム制作の ScriptWorld: そのシーンは
// Play しているもの (エディターではコピー)。

#include "rm_engine/climate/runtime/ScriptClimate.h"

#include "rm_engine/climate/runtime/Climate.h"
#include "rm_engine/climate/runtime/sdk/RMClimateAbi.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <algorithm>
#include <cmath>

namespace rm::climate {

namespace {

using game_production::ScriptWorld;

game_production::Scene* SceneOf(void* ctx) {
    auto* w = static_cast<ScriptWorld*>(ctx);
    return w ? w->scene : nullptr;
}

int At(void* ctx, const float pos[3], int flat, float* temperature, float* humidity) {
    const game_production::Scene* s = SceneOf(ctx);
    if (!s || !pos) return 0;
    const ClimateField field(*s);
    const ClimateHere c = flat ? field.At2D({pos[0], pos[1]}) : field.At({pos[0], pos[1], pos[2]});
    if (temperature) *temperature = c.temperature;
    if (humidity) *humidity = c.humidity;
    return 1;
}

int Set(void* ctx, int which, float value) {
    game_production::Scene* s = SceneOf(ctx);
    ClimateSettings* c = s ? s->Setting<ClimateSettings>() : nullptr;
    if (!c || !std::isfinite(value)) return 0;
    switch (which) {
        case 0: c->temperature = std::clamp(value, -80.f, 80.f); return 1;
        case 1: c->humidity = std::clamp(value, 0.f, 100.f); return 1;
        case 2: c->lapse = std::clamp(value, 0.f, 20.f); return 1;
        case 3: c->baseHeight = std::clamp(value, -100000.f, 100000.f); return 1;
        default: return 0;
    }
}

int Get(void* ctx, int which, float* out) {
    const game_production::Scene* s = SceneOf(ctx);
    if (!s || !out) return 0;
    const ClimateSettings* set = s->Setting<ClimateSettings>();
    const ClimateSettings c = set ? *set : ClimateSettings{};  // (設定が無いシーン: 既定の値)
    switch (which) {
        case 0: *out = c.temperature; return 1;
        case 1: *out = c.humidity; return 1;
        case 2: *out = c.lapse; return 1;
        case 3: *out = c.baseHeight; return 1;
        default: return 0;
    }
}

constexpr RmClimateApi kTable{&At, &Set, &Get};

}  // namespace

const game_production::ScriptApiPart& ScriptClimatePart() {
    static const game_production::ScriptApiPart part{RM_PART_CLIMATE, RM_PART_CLIMATE_VERSION, &kTable};
    return part;
}

}  // namespace rm::climate
