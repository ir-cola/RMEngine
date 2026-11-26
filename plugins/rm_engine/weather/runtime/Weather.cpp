#include "rm_engine/weather/runtime/Weather.h"

#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/weather/runtime/Falling.h"
#include "rm_engine/weather/runtime/Lightning.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::weather {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.f;

float Smooth(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float Unit(float percent) { return std::isfinite(percent) ? std::clamp(percent / 100.f, 0.f, 1.f) : 0.f; }

Vec3 TurnY(Vec3 d, float degrees) {
    const float c = std::cos(degrees * kDegrees), s = std::sin(degrees * kDegrees);
    return {c * d.x + s * d.z, d.y, -s * d.x + c * d.z};
}

}  // namespace

void ApplyPreset(Weather& w, WeatherPreset preset) {
    struct Look {
        float clouds, precipitation;
        bool snowing;
        float fog;
        bool lightning;
        float wind;
    };
    // (雲の量, 降水量, 雪にする, 霧の量, 雷, 風の強さ)
    static constexpr Look kLooks[kWeatherPresets] = {
        {0, 0, false, 0, false, 1},       // 快晴
        {20, 0, false, 0, false, 2},      // 晴れ
        {75, 0, false, 0, false, 3},      // くもり
        {60, 0, false, 60, false, 0.5f},  // 霧
        {65, 6, false, 0, false, 2},      // ぽつぽつ雨
        {80, 22, false, 5, false, 2},     // 小雨
        {90, 50, false, 10, false, 4},    // 雨
        {100, 80, false, 25, false, 7},   // どしゃぶり
        {100, 100, false, 40, true, 22},  // 嵐
        {70, 20, true, 5, false, 1},      // 小雪
        {85, 50, true, 10, false, 2},     // 雪
        {100, 80, true, 25, false, 4},    // 大雪
        {100, 100, true, 50, false, 18},  // 吹雪
    };
    const int k = static_cast<int>(preset);
    if (k < 0 || k >= kWeatherPresets) return;
    const Look& l = kLooks[k];
    w.cloudAmount = l.clouds;
    w.precipitation = l.precipitation;
    w.snowing = l.snowing;
    w.fogAmount = l.fog;
    w.lightning = l.lightning;
    w.windStrength = l.wind;
}

float SnowInOf(const Weather& w) {
    if (!w.wetCounted) return w.snowing ? 1.f : 0.f;
    return std::isfinite(w.snowIn) ? std::clamp(w.snowIn, 0.f, 1.f) : 0.f;
}

float RainShare(float x) { return x < 0.5f ? 1 - 0.85f * Smooth(0.f, 0.5f, x) : 0.f; }
float SnowShare(float x) { return x >= 0.5f ? 0.15f + 0.85f * Smooth(0.5f, 1.f, x) : 0.f; }

void PlacedShare(float precipitation, const WeatherPlace& place, float& rain, float& snow) {
    const float p = std::isfinite(precipitation) ? std::clamp(precipitation, 0.f, 1.f) : 0.f;
    const float s = std::isfinite(place.snow) ? std::clamp(place.snow, 0.f, 1.f) : 0.f;
    const float more = std::isfinite(place.more) ? std::clamp(place.more, 0.f, 4.f) : 1.f;
    const float local = std::isfinite(place.local) ? std::clamp(place.local, 0.f, 1.f) : 0.f;
    const float amount = std::max(std::min(p * more, 1.f), local);
    rain = amount * (1 - s);
    snow = amount * s;
}

float SnowMostOf(const Weather& w) { return std::isfinite(w.snowDepthCm) ? std::clamp(w.snowDepthCm / 100.f, 0.f, kMostSnowDepth) : 0.f; }

WeatherNow TargetOf(const Weather& w, bool place) {
    WeatherNow n;
    const float p = Unit(w.precipitation), x = SnowInOf(w);
    n.clouds = Unit(w.cloudAmount);
    // 場所ごとの雲 (つなぎが書いた、カメラのいる所の塗った雲): 塗った割合の分だけその量へ。
    if (place && std::isfinite(w.placeClouds) && w.placeClouds >= 0 && std::isfinite(w.placeCloudShare)) {
        const float share = std::clamp(w.placeCloudShare, 0.f, 1.f);
        n.clouds += (Unit(w.placeClouds) - n.clouds) * share;
    }
    if (std::isfinite(w.placeSnow) && w.placeSnow >= 0) {
        // 場所の降り方 (つなぎが書いた、カメラのいる所): その割合で雨と雪に分ける (みぞれは両方)。
        PlacedShare(p, {w.placeSnow, w.placeMore, place ? w.placeLocal : 0.f}, n.rain, n.snow);
    } else {
        n.rain = p * RainShare(x);
        n.snow = p * SnowShare(x);
    }
    n.fog = Unit(w.fogAmount);
    n.snowSpeed = std::isfinite(w.snowSpeedCm) ? std::clamp(w.snowSpeedCm, 0.f, 1000.f) / 100.f / 60.f : 0.f;
    const Vec3 wind = WindOf(w.windDirection, w.windStrength);
    n.windX = wind.x;
    n.windZ = wind.z;
    return n;
}

WeatherNow NowOf(const Weather& w, bool place) {
    WeatherNow to = TargetOf(w, place);
    if (!(w.fadeSeconds > 0) || !(w.fadeElapsed < w.fadeSeconds)) return to;
    const float t = Smooth(0.f, 1.f, std::max(w.fadeElapsed, 0.f) / w.fadeSeconds);  // 緩急つき
    const WeatherNow& a = w.fadeFrom;
    to.clouds = Lerp(a.clouds, to.clouds, t);
    to.rain = Lerp(a.rain, to.rain, t);
    to.snow = Lerp(a.snow, to.snow, t);
    to.fog = Lerp(a.fog, to.fog, t);
    to.windX = Lerp(a.windX, to.windX, t);  // (風はベクトルとして混ぜる: 近い回り方で向きが変わる)
    to.windZ = Lerp(a.windZ, to.windZ, t);
    return to;
}

float PrecipitationNow(const Weather& w) {
    const float p = Unit(w.precipitation);
    if (!(w.fadeSeconds > 0) || !(w.fadeElapsed < w.fadeSeconds)) return p;
    const float t = Smooth(0.f, 1.f, std::max(w.fadeElapsed, 0.f) / w.fadeSeconds);
    return Lerp(std::clamp(w.fadeFrom.rain + w.fadeFrom.snow, 0.f, 1.f), p, t);
}

void BeginFade(Weather& w, float seconds) {
    w.fadeFrom = NowOf(w);
    w.fadeSeconds = std::isfinite(seconds) && seconds > 0 ? seconds : 0.f;
    w.fadeElapsed = 0;
}

float RainNow(const WeatherNow& w) { return std::isfinite(w.rain) ? std::clamp(w.rain, 0.f, 1.f) : 0.f; }
float SnowNow(const WeatherNow& w) { return std::isfinite(w.snow) ? std::clamp(w.snow, 0.f, 1.f) : 0.f; }

float FogOf(const WeatherNow& w) {
    // 霧の量: 100 で 1 m につき 0.5 (5 m 先で 12 分の 1 になる)、50 で 0.125 (20 m 先で
    // 12 分の 1 になる)。55 あたりからのどしゃぶりと吹雪は、これに足される。
    const float fog = std::isfinite(w.fog) ? std::clamp(w.fog, 0.f, 1.f) : 0.f;
    const float rain = Smooth(0.55f, 1.f, RainNow(w)), snow = Smooth(0.5f, 1.f, SnowNow(w));
    return 0.5f * fog * fog + 0.2f * rain * std::sqrt(rain) + 0.15f * snow * std::sqrt(snow);
}

Color FogColorOf(const WeatherNow& w, Color light) {
    // 晴れやくもりでは淡く、雨では暗く灰色に (少し青く)、雪では白く。
    const float rain = Smooth(0.f, 0.3f, RainNow(w)), snow = Smooth(0.f, 0.3f, SnowNow(w));
    const float k = 0.55f - 0.22f * rain + 0.2f * snow;
    return {light.r * k, light.g * k, light.b * (k + 0.04f * rain), 1};
}

Vec3 WindOf(float directionDegrees, float strength) {
    if (!std::isfinite(directionDegrees) || !(strength > 0) || !std::isfinite(strength)) return {0, 0, 0};
    // 0: 正面 (+Z)、+ は左 (-X へ)。左手系 (2026-11-20 から): 前の右手系の向きを Z で写したもの。
    const Vec3 r = TurnY(Vec3{0, 0, -1}, directionDegrees);
    const Vec3 d{r.x, r.y, -r.z};
    return {d.x * strength, 0, d.z * strength};
}

namespace {

// 雨や雪が降ると、頼んだ雲が少なくても雲が出る。
float CoverOf(const WeatherNow& n) {
    const float c = std::clamp(n.clouds, 0.f, 1.f), falls = std::max(RainNow(n), SnowNow(n));
    return std::max(c, Smooth(0.f, 0.15f, falls) * (0.75f + 0.25f * falls));
}

}  // namespace

float SkyCoverOf(const Weather& w) { return CoverOf(NowOf(w, false)); }

bool LightningOn(const Weather& w) { return w.lightning && SkyCoverOf(w) > 0.5f; }

game_x_3d::SkyWeather SkyWeatherOf(const Weather& w, double seconds, const PlaceClouds* clouds) {
    // 雨は雲を暗い雨雲にし、雪の雲は淡くかすむ。雲が多いと太陽が暗くなる。
    const auto coverOf = [](const WeatherNow& n) { return CoverOf(n); };
    const WeatherNow scene = NowOf(w, false);  // シーン全体: 空の雲・暗さ・雨雲 (場所ごとのものは地図で)
    const WeatherNow now = NowOf(w);           // カメラのいる所: 日の光の弱まり・もや
    const float r = RainNow(scene), sn = SnowNow(scene);
    const float cover = coverOf(scene), coverHere = coverOf(now);
    const float rHere = RainNow(now), snHere = SnowNow(now);
    game_x_3d::SkyWeather s;
    s.cover = cover;
    s.dark = std::clamp((0.15f * cover + 0.45f * Smooth(0.45f, 1.f, cover) + 0.5f * r * (0.5f + 0.5f * cover)) * (1 - 0.35f * sn), 0.f, 1.f);
    s.haze = std::clamp(0.02f + 0.15f * coverHere * coverHere + 0.45f * rHere * rHere + 0.5f * snHere + 0.6f * std::clamp(now.fog, 0.f, 1.f), 0.f, 1.f);
    s.through = std::clamp(1 - 0.75f * Smooth(0.3f, 1.f, coverHere) - 0.35f * rHere - 0.15f * snHere, 0.06f, 1.f);
    s.rain = std::clamp(r + sn, 0.f, 1.f);  // 雨雲 (降るものの上)
    s.snow = r + sn > 0 ? std::clamp(sn / (r + sn), 0.f, 1.f) : 0.f;
    // 雷: ときどき光る (稲妻と同じ時: Lightning.h)。時間が進んでいる間だけ。
    if (w.lightning && cover > 0.5f) {
        if (const auto strike = StrikeAt(seconds)) s.flash = strike->strength * FlashGlow(strike->age);
    }
    s.driftX = w.driftX;
    s.driftZ = w.driftZ;
    s.windX = now.windX;
    s.windZ = now.windZ;
    // 場所ごとの雲と雨雲 (つなぎ 天候 × 気候 の地図): 空がその向きの雲を描く。塗っていないマスは
    // シーン全体と同じ値 (割合 0・降る量 0 なら cover / rain そのもの)。
    constexpr size_t kCount = static_cast<size_t>(kCloudMapCells) * kCloudMapCells;
    if (clouds && clouds->shares.size() == kCount && clouds->amounts.size() == kCount && clouds->rains.size() == kCount &&
        std::isfinite(clouds->firstX) && std::isfinite(clouds->firstZ)) {
        auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; };
        s.cloudCovers.resize(kCount);
        s.cloudRains.resize(kCount);
        for (size_t k = 0; k < kCount; ++k) {
            s.cloudCovers[k] = cover + (unit(clouds->amounts[k]) - cover) * unit(clouds->shares[k]);
            s.cloudRains[k] = std::max(s.rain, unit(clouds->rains[k]));
        }
        s.cloudCells = kCloudMapCells;
        s.cloudCell = kCloudMapCell;
        s.cloudFirstX = clouds->firstX;
        s.cloudFirstZ = clouds->firstZ;
    }
    return s;
}

namespace {

// 今降っている、濡らすもの (雨: 最後は全部。雪: 溶ける所で少し)。それぞれについて、降る向き、
// 濡らす程度の上限とその速さ (1 秒あたり)、覆いの下へしみこむ距離の上限とその速さ
// (1 秒あたりのメートル) を持つ。
struct Wetting {
    Vec3 way;
    float most, speed;
    float seepMost, seepSpeed;
};
std::vector<Wetting> WettingNow(const WeatherNow& now) {
    std::vector<Wetting> out;
    const Vec3 wind{now.windX, 0, now.windZ};
    // 雨: 最後は全部濡れる。強く降るほど早い (RainWetSpeed: どしゃぶりで 25 秒ほど、霧雨で 1 分半ほど)。
    // 覆いの下へしみこむ: どしゃぶりは 2 分足らずで 1 m、霧雨は 7 分ほどで (2026-11-20)。
    if (const float rain = RainNow(now); rain > 0) {
        out.push_back({FallDirection(Precipitation::Kind::Rain, wind), 1.f, RainWetSpeed(rain), kSeepRain, 0.002f + rain * 0.008f});
    }
    // 雪: 溶ける所で少し濡れる。ゆっくりで (2026-11-19)、しみこむ距離も短い。
    if (const float snow = SnowNow(now); snow > 0) {
        out.push_back({FallDirection(Precipitation::Kind::Snow, wind), kSnowWet, 0.002f + snow * 0.012f, kSeepSnow, 0.001f + snow * 0.003f});
    }
    return out;
}

}  // namespace

float RainWetSpeed(float rain) {
    // 弱い雨ほど前より少し速く (2026-11-22)、どしゃぶり (1) は前と同じ 0.04。
    const float r = std::isfinite(rain) ? std::clamp(rain, 0.f, 1.f) : 0.f;
    return 0.006f + 0.034f * std::pow(r, 0.75f);
}

namespace {

float Finite0(float v) { return std::isfinite(v) ? std::max(v, 0.f) : 0.f; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// `way` とみなせる向き (kWetWayDegrees 以内) の番号 (無い: -1)。
int WayFor(std::span<const WetWay> ways, Vec3 way) {
    const float close = std::cos(kWetWayDegrees * kDegrees);
    for (size_t k = 0; k < ways.size() && k < kWetWays; ++k) {
        if (Dot(ways[k].way, way) >= close) return static_cast<int>(k);
    }
    return -1;
}

// 今の濡らすもの (`wetting`: どこかで降っているもの) の向きを `ways` に用意し、しみこみを進める。
// 新しい向きの入る所がないときは、一番濡れていない向き (`totals`: 向きごとの濡れと積雪の合計) に代わる
// (このフレームで使う向きは除く)。代わった向き・新しい向きは `fresh` に (その濡れは 0 から)。
void PrepareWays(std::vector<WetWay>& ways, const std::vector<Wetting>& wetting, float dt, std::array<float, kWetWays> totals,
                 std::array<bool, kWetWays>& fresh) {
    fresh.fill(false);
    std::array<bool, kWetWays> used{};
    for (const Wetting& w : wetting) {
        int k = WayFor(ways, w.way);
        if (k < 0) {
            if (ways.size() < kWetWays) {
                ways.push_back({w.way, 0, 0});
                k = static_cast<int>(ways.size()) - 1;
            } else {
                for (size_t j = 0; j < kWetWays; ++j) {
                    if (!used[j] && (k < 0 || totals[j] < totals[static_cast<size_t>(k)])) k = static_cast<int>(j);
                }
                if (k < 0) continue;
                ways[static_cast<size_t>(k)] = {w.way, 0, 0};
            }
            fresh[static_cast<size_t>(k)] = true;
            totals[static_cast<size_t>(k)] = 0;
        }
        used[static_cast<size_t>(k)] = true;
        WetWay& it = ways[static_cast<size_t>(k)];
        if (it.seep < w.seepMost) it.seep = std::min(w.seepMost, it.seep + dt * w.seepSpeed);
    }
}

// 1 つの場所の濡れと積雪 (`levels`: `ways` と同じ並び) を、その場所の今の天候 `now` で `dt` 進める
// (向きは PrepareWays が用意したもの)。作れる分まで濡れ、雨や雪が降っている間は何も乾かない。積雪は
// 全部の向きまとめて StepSnowCover のとおりに動かす - 降ったものは降る向きに入り、溶けるものは全部の
// 向きから同じだけ引く。
void StepLevels(std::span<const WetWay> ways, WetLevel* levels, const WeatherNow& now, float dt, float snowMost) {
    const size_t count = std::min(ways.size(), kWetWays);
    float snowBefore = 0;
    for (size_t k = 0; k < count; ++k) snowBefore += Finite0(levels[k].snow);
    const float snowAfter = StepSnowCover(snowBefore, now, snowMost, dt);
    const std::vector<Wetting> wetting = WettingNow(now);
    for (const Wetting& w : wetting) {
        const int k = WayFor(ways, w.way);
        if (k < 0) continue;
        for (float* level : {&levels[k].sun, &levels[k].shade}) {
            if (*level < w.most) *level = std::min(w.most, *level + dt * w.speed);
        }
    }
    if (wetting.empty()) {
        for (size_t k = 0; k < count; ++k) {
            levels[k].sun = std::max(0.f, levels[k].sun - dt / 90.f);
            levels[k].shade = std::max(0.f, levels[k].shade - dt / 300.f);
        }
    }
    if (snowAfter > snowBefore && SnowNow(now) > 0) {
        const int k = WayFor(ways, FallDirection(Precipitation::Kind::Snow, {now.windX, 0, now.windZ}));
        if (k >= 0) levels[k].snow = Finite0(levels[k].snow) + (snowAfter - snowBefore);
    } else if (snowAfter < snowBefore) {
        const float keep = snowBefore > 0 ? snowAfter / snowBefore : 0.f;
        for (size_t k = 0; k < count; ++k) levels[k].snow = Finite0(levels[k].snow) * keep;
    }
}

bool Dry(const WetLevel& l) { return l.sun <= 0 && l.shade <= 0 && !(l.snow > 0); }

}  // namespace

void StepWet(std::vector<WetWay>& ways, const WeatherNow& now, float dt, float snowMost) {
    dt = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
    // シーン全体で 1 つの場所: 向きの濡れと積雪が、その場所の値。
    std::array<float, kWetWays> totals{};
    for (size_t k = 0; k < ways.size() && k < kWetWays; ++k) totals[k] = ways[k].sun + ways[k].shade + ways[k].snow;
    std::array<bool, kWetWays> fresh{};
    const std::vector<Wetting> wetting = WettingNow(now);
    PrepareWays(ways, wetting, dt, totals, fresh);
    WetCell levels{};
    for (size_t k = 0; k < ways.size() && k < kWetWays; ++k) levels[k] = {ways[k].sun, ways[k].shade, ways[k].snow};
    StepLevels(ways, levels.data(), now, dt, snowMost);
    for (size_t k = 0; k < ways.size() && k < kWetWays; ++k) {
        ways[k].sun = levels[k].sun;
        ways[k].shade = levels[k].shade;
        ways[k].snow = levels[k].snow;
    }
    if (wetting.empty()) std::erase_if(ways, [](const WetWay& w) { return Dry({w.sun, w.shade, w.snow}); });
}

void WetMapFirst(Vec3 eye, int& firstX, int& firstZ) {
    auto first = [](float v) {
        const float c = std::isfinite(v) ? std::clamp(std::floor(v / kWetMapCell), -1e8f, 1e8f) : 0.f;
        return static_cast<int>(c) - kWetMapCells / 2;
    };
    firstX = first(eye.x);
    firstZ = first(eye.z);
}

void PlaceCellsFor(Weather& w, const game_production::Scene& scene, Vec3 eye, const WeatherPlaceInfo& info) {
    if (!info.grid) return;
    WetMapFirst(eye, w.placeFirstX, w.placeFirstZ);
    // (一辺が偶数: 真ん中は最初のマスから kWetMapCells / 2 マスの角。点はマスの真ん中)
    const Vec3 center{static_cast<float>(w.placeFirstX + kWetMapCells / 2) * kWetMapCell, std::isfinite(eye.y) ? eye.y : 0.f,
                      static_cast<float>(w.placeFirstZ + kWetMapCells / 2) * kWetMapCell};
    w.placeCells.assign(size_t{kWetMapCells} * kWetMapCells, WeatherPlace{});
    info.grid(scene, center, kWetMapCell, kWetMapCells, w.placeCells.data());
}

void MoveWetMap(WetMap& map, std::span<const WetWay> ways, int firstX, int firstZ) {
    const size_t n = size_t{kWetMapCells};
    if (map.cells.size() != n * n) {
        WetCell start{};
        for (size_t k = 0; k < ways.size() && k < kWetWays; ++k) start[k] = {ways[k].sun, ways[k].shade, ways[k].snow};
        map.cells.assign(n * n, start);
        map.firstX = firstX;
        map.firstZ = firstZ;
        return;
    }
    if (firstX == map.firstX && firstZ == map.firstZ) return;
    std::vector<WetCell> moved(n * n);
    const long long last = kWetMapCells - 1;
    for (long long z = 0; z < kWetMapCells; ++z) {
        const long long fromZ = std::clamp(z + firstZ - map.firstZ, 0LL, last);
        for (long long x = 0; x < kWetMapCells; ++x) {
            const long long fromX = std::clamp(x + firstX - map.firstX, 0LL, last);
            moved[static_cast<size_t>(z) * n + static_cast<size_t>(x)] = map.cells[static_cast<size_t>(fromZ) * n + static_cast<size_t>(fromX)];
        }
    }
    map.cells = std::move(moved);
    map.firstX = firstX;
    map.firstZ = firstZ;
}

void StepWetMap(WetMap& map, std::vector<WetWay>& ways, std::span<const WeatherPlace> places, const WeatherNow& now, float precipitation, float dt,
                float snowMost, Vec3 eye) {
    dt = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
    if (map.cells.empty() || places.size() != map.cells.size()) return;
    // マスごとの今の天候 (雨と雪だけが場所で変わる) と、どこかで降っている一番多いもの (向きとしみこみ)。
    std::vector<WeatherNow> here(map.cells.size(), now);
    WeatherNow most = now;
    most.rain = most.snow = 0;
    for (size_t i = 0; i < here.size(); ++i) {
        PlacedShare(precipitation, places[i], here[i].rain, here[i].snow);
        most.rain = std::max(most.rain, here[i].rain);
        most.snow = std::max(most.snow, here[i].snow);
    }
    std::array<float, kWetWays> totals{};
    for (const WetCell& c : map.cells) {
        for (size_t k = 0; k < kWetWays; ++k) totals[k] = std::max(totals[k], c[k].sun + c[k].shade + c[k].snow);
    }
    std::array<bool, kWetWays> fresh{};
    const std::vector<Wetting> wetting = WettingNow(most);
    PrepareWays(ways, wetting, dt, totals, fresh);
    for (size_t i = 0; i < map.cells.size(); ++i) {
        WetCell& c = map.cells[i];
        for (size_t k = 0; k < kWetWays; ++k) {
            if (fresh[k] || k >= ways.size()) c[k] = {};
        }
        StepLevels(ways, c.data(), here[i], dt, snowMost);
    }
    if (wetting.empty()) {
        // 全部のマスで乾いて溶けた向きは消す (マスの並びも詰める)。
        for (size_t k = std::min(ways.size(), kWetWays); k-- > 0;) {
            const bool dry = std::all_of(map.cells.begin(), map.cells.end(), [k](const WetCell& c) { return Dry(c[k]); });
            if (!dry) continue;
            ways.erase(ways.begin() + static_cast<std::ptrdiff_t>(k));
            for (WetCell& c : map.cells) {
                std::move(c.begin() + static_cast<std::ptrdiff_t>(k) + 1, c.end(), c.begin() + static_cast<std::ptrdiff_t>(k));
                c.back() = {};
            }
        }
    }
    // スクリプトが読む濡れと積雪 (rm::Sky::Wetness など): カメラのいるマス。
    if (const WetCell* at = WetCellAt(map, eye.x, eye.z)) {
        for (size_t k = 0; k < ways.size() && k < kWetWays; ++k) {
            ways[k].sun = (*at)[k].sun;
            ways[k].shade = (*at)[k].shade;
            ways[k].snow = (*at)[k].snow;
        }
    }
}

const WetCell* WetCellAt(const WetMap& map, float x, float z) {
    const size_t n = size_t{kWetMapCells};
    if (map.cells.size() != n * n) return nullptr;
    auto index = [](float v, int first) {
        const float c = std::isfinite(v) ? std::floor(v / kWetMapCell) - static_cast<float>(first) : 0.f;
        return static_cast<size_t>(std::clamp(c, 0.f, static_cast<float>(kWetMapCells - 1)));
    };
    return &map.cells[index(z, map.firstZ) * n + index(x, map.firstX)];
}

float WetMapMostSnow(const WetMap& map) {
    float most = 0;
    for (const WetCell& c : map.cells) {
        float all = 0;
        for (const WetLevel& l : c) all += Finite0(l.snow);
        most = std::max(most, all);
    }
    return most;
}

WetMapInfo WetMapInfoOf(const WetMap& map) {
    if (map.cells.size() != size_t{kWetMapCells} * kWetMapCells) return {};
    return {static_cast<float>(map.firstX) * kWetMapCell, static_cast<float>(map.firstZ) * kWetMapCell, kWetMapCell, static_cast<float>(kWetMapCells)};
}

std::vector<float> WetMapTexels(const WetMap& map) {
    const size_t n = size_t{kWetMapCells};
    if (map.cells.size() != n * n) return {};
    std::vector<float> out(n * 3 * n * 4, 0.f);
    for (size_t z = 0; z < n; ++z) {
        for (size_t x = 0; x < n; ++x) {
            const WetCell& c = map.cells[z * n + x];
            for (size_t k = 0; k < kWetWays; ++k) {
                out[(z * 3 * n + x) * 4 + k] = std::isfinite(c[k].sun) ? std::clamp(c[k].sun, 0.f, 1.f) : 0.f;
                out[(z * 3 * n + n + x) * 4 + k] = std::isfinite(c[k].shade) ? std::clamp(c[k].shade, 0.f, 1.f) : 0.f;
                out[(z * 3 * n + 2 * n + x) * 4 + k] = std::isfinite(c[k].snow) ? std::clamp(c[k].snow, 0.f, kMostSnowDepth) : 0.f;
            }
        }
    }
    return out;
}

namespace {

// 雪の降る向きの番号 (無ければ足す。入る所が無い: -1)。
int SnowWay(std::vector<WetWay>& ways, const WeatherNow& now) {
    const Vec3 way = FallDirection(Precipitation::Kind::Snow, {now.windX, 0, now.windZ});
    int k = WayFor(ways, way);
    if (k < 0 && ways.size() < kWetWays) {
        WetWay fresh;
        fresh.way = way;
        ways.push_back(fresh);
        k = static_cast<int>(ways.size()) - 1;
    }
    return k;
}

}  // namespace

void StartSnow(std::vector<WetWay>& ways, const WeatherNow& now, float most) {
    most = std::isfinite(most) ? std::clamp(most, 0.f, kMostSnowDepth) : 0.f;
    if (!(SnowNow(now) > 0) || !(most > 0)) return;
    if (const int k = SnowWay(ways, now); k >= 0) ways[static_cast<size_t>(k)].snow = std::max(Finite0(ways[static_cast<size_t>(k)].snow), most);
}

void StartSnowMap(WetMap& map, std::vector<WetWay>& ways, std::span<const WeatherPlace> places, const WeatherNow& now, float precipitation,
                  float most) {
    most = std::isfinite(most) ? std::clamp(most, 0.f, kMostSnowDepth) : 0.f;
    if (!(most > 0) || map.cells.empty() || places.size() != map.cells.size()) return;
    bool any = false;
    for (const WeatherPlace& p : places) {
        float rain = 0, snow = 0;
        PlacedShare(precipitation, p, rain, snow);
        any = any || snow > 0;
    }
    if (!any) return;
    const int k = SnowWay(ways, now);
    if (k < 0) return;
    for (size_t i = 0; i < map.cells.size(); ++i) {
        float rain = 0, snow = 0;
        PlacedShare(precipitation, places[i], rain, snow);
        if (!(snow > 0)) continue;
        WetLevel& l = map.cells[i][static_cast<size_t>(k)];
        l.snow = std::max(Finite0(l.snow), most * snow / (rain + snow));
    }
}

const WetMap* WetMapNow(const Weather& w) { return w.wetCounted && !w.wetMap.cells.empty() ? &w.wetMap : nullptr; }

float WetnessOf(const std::vector<WetWay>& ways) {
    float most = 0;
    for (const WetWay& w : ways) most = std::max(most, w.sun);
    return most;
}

std::vector<WetWay> WetNow(const Weather& w) { return w.wetCounted ? w.wetWays : std::vector<WetWay>{}; }

float StepSnowCover(float cover, const WeatherNow& now, float most, float dt) {
    dt = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
    most = std::isfinite(most) ? std::clamp(most, 0.f, kMostSnowDepth) : 0.f;
    cover = std::isfinite(cover) ? std::clamp(cover, 0.f, most) : 0.f;
    if (const float rate = SnowFallRate(now); rate > 0) {
        return std::min(most, cover + dt * rate);  // (降る量に比例: 量 0.5 なら量 1 の半分)
    }
    return std::max(0.f, cover - dt * (0.0002f + RainNow(now) * 0.002f));
}

float SnowFallRate(const WeatherNow& now) { return SnowNow(now) * (std::isfinite(now.snowSpeed) ? std::max(now.snowSpeed, 0.f) : 0.f); }

float SnowCoverNow(const Weather& w) {
    float all = 0;
    for (const WetWay& way : WetNow(w)) all += std::isfinite(way.snow) ? std::max(way.snow, 0.f) : 0.f;
    return all;
}

void RunWeather(game_production::SystemContext& ctx) {
    Weather* w = ctx.scene.Setting<Weather>();
    if (!w) return;
    const float dt = std::isfinite(ctx.dt) ? std::max(ctx.dt, 0.f) : 0.f;
    if (w->fadeSeconds > 0 && w->fadeElapsed < w->fadeSeconds) w->fadeElapsed = std::min(w->fadeElapsed + dt, w->fadeSeconds);
    if (!w->wetCounted) w->snowIn = w->snowing ? 1.f : 0.f;  // (エディターで見えていた状態から。そのあとゆっくり変わる)
    // 雪にする: 雨から雪へ (または戻る) 約 5 秒かけて変わる。
    const float snowTo = w->snowing ? 1.f : 0.f, snowFrom = std::isfinite(w->snowIn) ? std::clamp(w->snowIn, 0.f, 1.f) : snowTo;
    w->snowIn = snowFrom < snowTo ? std::min(snowTo, snowFrom + dt / 5.f) : std::max(snowTo, snowFrom - dt / 5.f);
    const WeatherNow now = NowOf(*w);
    if (!w->wetCounted) {
        w->wetWays.clear();  // (ゲームは乾いて始まり、雪は降るにつれて積もる - 2026-11-19 / 20。はじめから積もっている: 下で置く)
        w->wetMap = {};
        w->wetCounted = true;
        w->snowStartPending = w->snowFromStart;
    }
    if (w->placeCells.size() == size_t{kWetMapCells} * kWetMapCells) {
        // 場所ごと (答える者が今のフレームのマスを書いた): 地図をカメラのまわりへずらし、マスごとに進める。
        MoveWetMap(w->wetMap, w->wetWays, w->placeFirstX, w->placeFirstZ);
        if (w->snowStartPending) StartSnowMap(w->wetMap, w->wetWays, w->placeCells, now, PrecipitationNow(*w), SnowMostOf(*w));
        w->snowStartPending = false;
        const Vec3 eye{static_cast<float>(w->placeFirstX + kWetMapCells / 2) * kWetMapCell, 0,
                       static_cast<float>(w->placeFirstZ + kWetMapCells / 2) * kWetMapCell};
        StepWetMap(w->wetMap, w->wetWays, w->placeCells, now, PrecipitationNow(*w), dt, SnowMostOf(*w), eye);
    } else {
        w->wetMap = {};  // (答える者がいない: シーン全体で 1 つ)
        if (w->snowStartPending) StartSnow(w->wetWays, now, SnowMostOf(*w));
        w->snowStartPending = false;
        StepWet(w->wetWays, now, dt, SnowMostOf(*w));
    }
    w->placeCells.clear();  // (次のフレームも、答える者が書く)
    w->driftX = std::fmod(w->driftX + now.windX * dt, kDriftWrap);
    w->driftZ = std::fmod(w->driftZ + now.windZ * dt, kDriftWrap);
}

}  // namespace rm::weather
