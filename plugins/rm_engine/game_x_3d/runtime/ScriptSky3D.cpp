// スクリプト API の空の部分 (RM3D.h の rm::Sky: 時刻)。RmApi::ctx はゲーム制作の
// ScriptWorld: そのシーンは遊んでいるもの (エディターではコピー) で、空はそのシーンの設定。
// (天気はプラグイン 天候 のもの: RMWeather.h。)

#include "rm_engine/game_x_3d/runtime/ScriptSky3D.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_3d/runtime/Sky.h"
#include "rm_engine/game_x_3d/runtime/sdk/RM3DAbi.h"

#include <cmath>

namespace rm::game_x_3d {

namespace {

using game_production::ScriptWorld;

Sky* SkyOf(void* ctx) {
    auto* w = static_cast<ScriptWorld*>(ctx);
    return w && w->scene ? w->scene->Setting<Sky>() : nullptr;
}

int SetHour(void* ctx, float hour) {
    Sky* sky = SkyOf(ctx);
    if (!sky || !std::isfinite(hour)) return 0;
    sky->hour = std::fmod(std::fmod(hour, 24.f) + 24.f, 24.f);
    return 1;
}

int Hour(void* ctx, float* out) {
    const Sky* sky = SkyOf(ctx);
    if (!sky || !out) return 0;
    *out = sky->hour;
    return 1;
}

constexpr RmSky3DApi2 kTable{&SetHour, &Hour};

}  // namespace

const game_production::ScriptApiPart& ScriptSky3DPart() {
    static const game_production::ScriptApiPart part{RM_PART_SKY3D, RM_PART_SKY3D_VERSION2, &kTable};
    return part;
}

}  // namespace rm::game_x_3d
