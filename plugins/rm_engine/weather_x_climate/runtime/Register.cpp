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
    const Vec2 drift = CloudDriftOf(scene);
    const float half = static_cast<float>(count - 1) * 0.5f;
    for (int z = 0; z < count; ++z) {
        for (int x = 0; x < count; ++x) {
            const Vec3 at{center.x + (static_cast<float>(x) - half) * step, center.y, center.z + (static_cast<float>(z) - half) * step};
            climate::ClimateHere here = field.At(at);
            DriftedClouds(field, {at.x, at.z}, drift, here);
            out[z * count + x] = PlaceOf(here);
        }
    }
}

float MostLocal(const game_production::Scene& scene) { return MostLocalOf(scene); }

void Clouds(const game_production::Scene& scene, Vec3 eye, weather::PlaceClouds& out) {
    CloudMapFor(out, climate::ClimateField(scene), eye, CloudDriftOf(scene));
}

const weather::WeatherPlaceInfo kPlace{&Grid, &MostLocal, &Clouds};

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
    const float dt = std::isfinite(ctx.dt) ? std::max(ctx.dt, 0.f) : 0.f;
    climate::ClimateHere here = field.At(at);
    DriftedClouds(field, {at.x, at.z}, {w->driftX, w->driftZ}, here);
    Approach(w->placeSnow, w->placeMore, w->placeLocal, PlaceOf(here), dt);
    weather::PlaceCellsFor(*w, ctx.scene, at, kPlace);
    if (field.HasClouds()) {
        ApproachClouds(w->placeClouds, w->placeCloudShare, here.cloudWeight, here.cloudAmount, dt);
    } else {
        w->placeClouds = -1;
        w->placeCloudShare = 0;
    }
}

}  // namespace

Vec2 CloudDriftOf(const game_production::Scene& scene) {
    const weather::Weather* w = scene.Setting<weather::Weather>();
    if (!w || !std::isfinite(w->driftX) || !std::isfinite(w->driftZ)) return {0, 0};
    return {w->driftX, w->driftZ};
}

void DriftedClouds(const climate::ClimateField& field, Vec2 xz, Vec2 drift, climate::ClimateHere& here) {
    const Vec2 from{xz.x - drift.x, xz.y - drift.y};
    field.CloudsAt(from, -1.f, here.cloudWeight, here.cloudAmount);
    field.RainAt(from, 0.f, here.rainWeight, here.rainAmount);
}

float SnowShareOf(float celsius) {
    if (!std::isfinite(celsius)) return 0.f;
    const float t = std::clamp((2.f - celsius) / 3.f, 0.f, 1.f);  // (+2 ℃: 0 .. -1 ℃: 1)
    return t * t * (3.f - 2.f * t);
}

float MoreOf(float humidity) {
    const float h = std::isfinite(humidity) ? std::clamp(humidity, 0.f, 100.f) : 50.f;
    return h <= 50.f ? h / 50.f : 1.f + (h - 50.f) / 50.f * (weather::kPlaceMostMore - 1.f);
}

weather::WeatherPlace PlaceOf(const climate::ClimateHere& c) {
    const float weight = std::isfinite(c.rainWeight) ? std::clamp(c.rainWeight, 0.f, 1.f) : 0.f;
    const float amount = std::isfinite(c.rainAmount) ? std::clamp(c.rainAmount, 0.f, 100.f) : 0.f;
    return {SnowShareOf(c.temperature), MoreOf(c.humidity), weight * amount / 100.f};
}

float MostLocalOf(const game_production::Scene& scene) {
    const climate::ClimateMap* map = scene.Setting<climate::ClimateMap>();
    if (!map) return 0.f;
    const auto tables = climate::PaintTablesOf(*map);
    return tables ? std::clamp(tables->MostRain() / 100.f, 0.f, 1.f) : 0.f;
}

void ApproachValue(float& value, float to, float dt) {
    const float k = 1.f - std::exp(-(std::isfinite(dt) ? std::max(dt, 0.f) : 0.f) / 2.f);
    value += (to - value) * k;
}

void Approach(float& placeSnow, float& placeMore, float& placeLocal, const weather::WeatherPlace& to, float dt) {
    if (!std::isfinite(placeSnow) || placeSnow < 0 || !std::isfinite(placeMore) || !std::isfinite(placeLocal)) {
        placeSnow = to.snow;
        placeMore = to.more;
        placeLocal = to.local;
        return;
    }
    ApproachValue(placeSnow, to.snow, dt);
    ApproachValue(placeMore, to.more, dt);
    ApproachValue(placeLocal, to.local, dt);
}

void ApproachClouds(float& placeClouds, float& placeShare, float weight, float amount, float dt) {
    weight = std::isfinite(weight) ? std::clamp(weight, 0.f, 1.f) : 0.f;
    amount = std::isfinite(amount) ? std::clamp(amount, 0.f, 100.f) : 0.f;
    if (!std::isfinite(placeClouds) || placeClouds < 0 || !std::isfinite(placeShare)) {
        placeClouds = amount;
        placeShare = weight;
        return;
    }
    ApproachValue(placeShare, weight, dt);
    if (weight > 0) ApproachValue(placeClouds, amount, dt);
}

void CloudMapFor(weather::PlaceClouds& out, const climate::ClimateField& field, Vec3 eye, Vec2 drift) {
    out.shares.clear();
    out.amounts.clear();
    out.rains.clear();
    if (!(field.HasClouds() || field.HasRain()) || !std::isfinite(eye.x) || !std::isfinite(eye.z)) return;
    constexpr int n = weather::kCloudMapCells;
    constexpr float cell = weather::kCloudMapCell;
    const float firstX = (std::floor(eye.x / cell) - static_cast<float>(n / 2) + 0.5f) * cell;
    const float firstZ = (std::floor(eye.z / cell) - static_cast<float>(n / 2) + 0.5f) * cell;
    out.firstX = firstX;
    out.firstZ = firstZ;
    out.shares.assign(size_t{n} * n, 0.f);
    out.amounts.assign(size_t{n} * n, 0.f);
    out.rains.assign(size_t{n} * n, 0.f);
    for (int z = 0; z < n; ++z) {
        for (int x = 0; x < n; ++x) {
            const Vec2 at{firstX + static_cast<float>(x) * cell - drift.x, firstZ + static_cast<float>(z) * cell - drift.y};
            float share = 0, amount = 0, rainShare = 0, rain = 0;
            field.CloudsAt(at, cell * 0.5f, share, amount);
            field.RainAt(at, cell * 0.5f, rainShare, rain);
            const size_t k = static_cast<size_t>(z) * n + static_cast<size_t>(x);
            out.shares[k] = share;
            out.amounts[k] = std::clamp(amount / 100.f, 0.f, 1.f);
            out.rains[k] = std::clamp(rainShare * rain / 100.f, 0.f, 1.f);
        }
    }
}

void RegisterRuntime(RuntimeRegistry& registry) {
    registry.Contribute(weather::kWeatherPlacePoint, kPlace);
    static const game_production::SystemInfo kSystem{"天候 × 気候", game_production::SystemPhase::Frame, -1, &RunPlace};
    registry.Contribute(game_production::kSystemPoint, kSystem);
}

}  // namespace rm::weather_x_climate

RM_RUNTIME_ENTRY(rm_engine, weather_x_climate, rm::weather_x_climate::RegisterRuntime)
