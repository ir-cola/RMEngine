#include "rm_engine/weather_x_climate/runtime/Register.h"

#include "rm_engine/game_production/runtime/PlayServices.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/ScriptTransform3D.h"
#include "rm_engine/weather/runtime/Weather.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rm::weather_x_climate {

namespace {

// 天候の口: カメラのまわりの格子の降り方 (気候のエリアは 1 回だけ集める)。
void Grid(const game_production::Scene& scene, Vec3 center, float step, int count, weather::WeatherPlace* out) {
    if (!out || count <= 0) return;
    const climate::ClimateField field(scene);
    const float half = static_cast<float>(count - 1) * 0.5f;
    for (int z = 0; z < count; ++z) {
        for (int x = 0; x < count; ++x) {
            const Vec3 at{center.x + (static_cast<float>(x) - half) * step, center.y, center.z + (static_cast<float>(z) - half) * step};
            out[z * count + x] = PlaceOf(field.At(at));
        }
    }
}

const weather::WeatherPlaceInfo kPlace{&Grid};

// システム: Play の間、ゲームのカメラのいる所の降り方を、天候の今の天候に (天候のシステムの前)。
// カメラのまわりの地図のマスごとの降り方も (天候が場所ごとの濡れと積雪を進める)。
void RunPlace(game_production::SystemContext& ctx) {
    weather::Weather* w = ctx.scene.Setting<weather::Weather>();
    if (!w) return;
    game_production::Pose pose;
    Vec3 at{0, 0, 0};
    if (ctx.camera > 0 && ctx.camera <= static_cast<uint64_t>(std::numeric_limits<int>::max()) &&
        game_x_3d::Pose3D(ctx.scene, static_cast<int>(ctx.camera), pose)) {
        at = pose.position;
    }
    const climate::ClimateField field(ctx.scene);
    Approach(w->placeSnow, w->placeMore, PlaceOf(field.At(at)), std::isfinite(ctx.dt) ? std::max(ctx.dt, 0.f) : 0.f);
    weather::PlaceCellsFor(*w, ctx.scene, at, kPlace);
}

}  // namespace

float SnowShareOf(float celsius) {
    if (!std::isfinite(celsius)) return 0.f;
    const float t = std::clamp((2.f - celsius) / 3.f, 0.f, 1.f);  // (+2 ℃: 0 .. -1 ℃: 1)
    return t * t * (3.f - 2.f * t);
}

float MoreOf(float humidity) {
    const float h = std::isfinite(humidity) ? std::clamp(humidity, 0.f, 100.f) : 50.f;
    return h <= 50.f ? h / 50.f : 1.f + (h - 50.f) / 50.f * (weather::kPlaceMostMore - 1.f);
}

weather::WeatherPlace PlaceOf(const climate::ClimateHere& c) { return {SnowShareOf(c.temperature), MoreOf(c.humidity)}; }

void Approach(float& placeSnow, float& placeMore, const weather::WeatherPlace& to, float dt) {
    if (!std::isfinite(placeSnow) || placeSnow < 0 || !std::isfinite(placeMore)) {
        placeSnow = to.snow;
        placeMore = to.more;
        return;
    }
    const float k = 1.f - std::exp(-dt / 2.f);
    placeSnow += (to.snow - placeSnow) * k;
    placeMore += (to.more - placeMore) * k;
}

void RegisterRuntime(RuntimeRegistry& registry) {
    registry.Contribute(weather::kWeatherPlacePoint, kPlace);
    static const game_production::SystemInfo kSystem{"天候 × 気候", game_production::SystemPhase::Frame, -1, &RunPlace};
    registry.Contribute(game_production::kSystemPoint, kSystem);
}

}  // namespace rm::weather_x_climate

RM_RUNTIME_ENTRY(rm_engine, weather_x_climate, rm::weather_x_climate::RegisterRuntime)
