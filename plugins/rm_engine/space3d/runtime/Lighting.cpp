#include "rm_engine/space3d/runtime/Lighting.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::space3d {

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;
constexpr float kSpotFadeStart = 0.75f;  // a spot is full inside 3/4 of its half angle
constexpr float kPointFov = 95.f;        // each side of a point light's cube (a little over 90: soft edges meet)
constexpr float kCascadeMix = 0.75f;     // CascadeSplits: 1 = evenly growing steps, 0 = even steps

Vec3 DirectionOr(Vec3 d, Vec3 fallback) {
    const Vec3 n = Normalize(d);
    return Length(n) == 0 ? fallback : n;
}

// The near plane of a point / spot shadow map (as far as the depth's precision allows).
float LocalNear(float range) { return std::clamp(range * 0.01f, 0.02f, 0.5f); }
float LocalFar(float range) { return std::max(range, LocalNear(range) * 2.f); }
float SpotFov(float spotAngle) { return std::min(std::clamp(spotAngle, 1.f, 179.f) * 1.1f + 4.f, 170.f); }

// Up for a camera looking along `dir` (any up that is not the line of sight).
Vec3 UpFor(Vec3 dir) { return std::fabs(dir.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}; }

// What a perspective shadow map of `fov` degrees reaching `range` puts in LightBlock::shadow (y, z).
void LocalBias(float fov, float range, float out[4]) {
    const float texelPerDistance = 2.f * std::tan(fov * 0.5f * kDeg) / static_cast<float>(kLocalShadowSize);
    const float n = LocalNear(range), f = LocalFar(range);
    out[1] = texelPerDistance * 1.5f;
    // A shift of 1.5 texels (texelPerDistance x d in the world) in the map's depth near d: depth =
    // f / (f - n) x (1 - n / d), whose change per unit of d is f n / ((f - n) d^2).
    out[2] = 1.5f * texelPerDistance * f * n / (f - n);
}

}  // namespace

Mat4 SpotShadowMatrix(Vec3 position, Vec3 direction, float spotAngle, float range) {
    const Vec3 dir = DirectionOr(direction, {0, -1, 0});
    return Perspective(SpotFov(spotAngle), 1.f, LocalNear(range), LocalFar(range)) * LookAt(position, position + dir, UpFor(dir));
}

std::array<Mat4, 6> PointShadowMatrices(Vec3 position, float range) {
    const Vec3 sides[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const Mat4 projection = Perspective(kPointFov, 1.f, LocalNear(range), LocalFar(range));
    std::array<Mat4, 6> out;
    for (size_t i = 0; i < 6; ++i) out[i] = projection * LookAt(position, position + sides[i], UpFor(sides[i]));
    return out;
}

LightSetup MakeLights(std::span<const Light> lights, Vec3 defaultDirection, const Ambient& ambient) {
    LightSetup s;
    const Light fallback{.direction = defaultDirection};
    const std::span<const Light> used = lights.empty() ? std::span<const Light>(&fallback, 1) : lights;
    const size_t n = std::min(used.size(), kMaxLights);
    s.dropped = used.size() - n;
    s.block.info[0] = static_cast<float>(n);
    const float a = std::max(ambient.intensity, 0.f);
    const Color sky = ambient.sky, ground = ambient.ground;
    s.block.ambientSky[0] = sky.r * a, s.block.ambientSky[1] = sky.g * a, s.block.ambientSky[2] = sky.b * a;
    s.block.ambientGround[0] = ground.r * a, s.block.ambientGround[1] = ground.g * a, s.block.ambientGround[2] = ground.b * a;
    for (size_t i = 0; i < n; ++i) {
        const Light& l = used[i];
        const Vec3 dir = DirectionOr(l.direction, {0, -1, 0});
        const float k = std::max(l.intensity, 0.f);
        const float half = std::clamp(l.spotAngle, 1.f, 179.f) * 0.5f;
        s.block.position[i][0] = l.position.x;
        s.block.position[i][1] = l.position.y;
        s.block.position[i][2] = l.position.z;
        s.block.position[i][3] = static_cast<float>(static_cast<int>(l.type));
        s.block.direction[i][0] = dir.x;
        s.block.direction[i][1] = dir.y;
        s.block.direction[i][2] = dir.z;
        s.block.direction[i][3] = std::max(l.range, 0.001f);
        s.block.color[i][0] = l.color.r * k;
        s.block.color[i][1] = l.color.g * k;
        s.block.color[i][2] = l.color.b * k;
        s.block.spot[i][0] = std::cos(half * kDeg);
        s.block.spot[i][1] = std::cos(half * kSpotFadeStart * kDeg);
        s.block.shadow[i][0] = -1.f;
        if (!l.castShadow) continue;
        if (l.type == LightType::Directional) {
            if (s.shadowLight < 0) {
                s.shadowLight = static_cast<int>(i);
                s.shadowDirection = dir;
                s.block.color[i][3] = 1.f;
            }
            continue;
        }
        // A point / spot light: its maps, while there are free ones.
        const size_t need = l.type == LightType::Point ? 6 : 1;
        if (s.localSlices + need > kLocalShadowSlices) {
            ++s.shadowsLeftOut;
            continue;
        }
        s.block.shadow[i][0] = static_cast<float>(s.localSlices);
        if (l.type == LightType::Point) {
            for (const Mat4& m : PointShadowMatrices(l.position, l.range)) s.localViewProj[s.localSlices++] = m;
            LocalBias(kPointFov, l.range, s.block.shadow[i]);
        } else {
            s.localViewProj[s.localSlices++] = SpotShadowMatrix(l.position, dir, l.spotAngle, l.range);
            LocalBias(SpotFov(l.spotAngle), l.range, s.block.shadow[i]);
        }
    }
    return s;
}

std::array<float, kCascades> CascadeSplits(float nearZ, float farZ) {
    const float n = std::max(nearZ, 1e-4f), f = std::max(farZ, n * 1.001f);
    std::array<float, kCascades> out;
    for (int i = 1; i <= kCascades; ++i) {
        const float t = static_cast<float>(i) / kCascades;
        const float growing = n * std::pow(f / n, t), even = n + (f - n) * t;
        out[static_cast<size_t>(i - 1)] = kCascadeMix * growing + (1 - kCascadeMix) * even;
    }
    out[kCascades - 1] = f;
    return out;
}

namespace {

// The texel and depth of an orthographic light matrix (it only turns and moves, so each row is the
// scale of that axis).
void Measure(const Mat4& m, uint32_t mapSize, float* texel, float* depthRange) {
    auto rowLength = [&](int r) { return Length({m.m[0][r], m.m[1][r], m.m[2][r]}); };
    const float width = 2.f / std::max(rowLength(0), 1e-12f), height = 2.f / std::max(rowLength(1), 1e-12f);
    *depthRange = 1.f / std::max(rowLength(2), 1e-12f);
    *texel = std::max(width, height) / static_cast<float>(std::max(mapSize, 1u));
}

}  // namespace

SunShadowPlan PlanSunShadows(Vec3 direction, const Bounds& casters, const Mat4& view, const Mat4& projection, uint32_t mapSize) {
    SunShadowPlan plan;
    const Vec3 dir = DirectionOr(direction, {0, -1, 0});
    auto single = [&] {
        plan.count = 1;
        plan.viewProj[0] = DirectionalShadowMatrix(dir, casters);
        plan.splitFar[0] = 1e30f;
        Measure(plan.viewProj[0], mapSize, &plan.texel[0], &plan.depthRange[0]);
        return plan;
    };
    if (Length(casters.max - casters.min) * 0.5f <= kSingleMapRadius) return single();

    // What the camera sees, and how far its near and far planes are in front of the eye.
    const auto toWorld = Inverse(view);
    const auto corners = FrustumCorners(projection * view);
    if (!toWorld || !corners) return single();
    const Vec3 eye = TransformPoint(*toWorld, {0, 0, 0});
    const Vec3 forward = Normalize(TransformDirection(*toWorld, {0, 0, -1}));
    const float d0 = Dot((*corners)[0] - eye, forward), d1 = Dot((*corners)[4] - eye, forward);
    const float reach = std::min(d1, kShadowDistance);
    if (!(d1 > d0) || !(reach > d0 + 1e-3f)) return single();
    auto at = [&](size_t edge, float d) {  // the point of an edge of the view at distance d
        const Vec3 a = (*corners)[edge], b = (*corners)[edge + 4];
        return a + (b - a) * ((d - d0) / (d1 - d0));
    };

    // The light's turn (no move: the cascades are moved in whole texels in it).
    const Mat4 turn = LookAt({0, 0, 0}, dir, UpFor(dir));
    float casterHigh = -1e30f;  // the casters' side nearest the light (view space looks along -z: higher z)
    for (int i = 0; i < 8; ++i) {
        const Vec3 p = TransformPoint(turn, {(i & 1) ? casters.max.x : casters.min.x, (i & 2) ? casters.max.y : casters.min.y,
                                             (i & 4) ? casters.max.z : casters.min.z});
        casterHigh = std::max(casterHigh, p.z);
    }
    const auto splits = CascadeSplits(d0, reach);
    float start = d0;
    plan.count = kCascades;
    for (size_t c = 0; c < static_cast<size_t>(kCascades); ++c) {
        const float end = splits[c];
        Vec3 pts[8];
        for (size_t e = 0; e < 4; ++e) {
            pts[e] = at(e, start);
            pts[e + 4] = at(e, end);
        }
        Vec3 center{0, 0, 0};
        for (const Vec3& p : pts) center = center + p * 0.125f;
        float radius = 0;
        for (const Vec3& p : pts) radius = std::max(radius, Length(p - center));
        radius = std::ceil(radius * 16.f) / 16.f + 0.01f;  // the same every frame while the view keeps its shape
        Vec3 lc = TransformPoint(turn, center);
        const float texel = radius * 2.f / static_cast<float>(std::max(mapSize, 1u));
        lc.x = std::floor(lc.x / texel) * texel;
        lc.y = std::floor(lc.y / texel) * texel;
        // Deep enough for its ball, and towards the light for everything that casts into it.
        const float pad = radius * 0.02f + 0.01f;
        const float high = std::max(lc.z + radius, casterHigh) + pad, low = lc.z - radius - pad;
        plan.viewProj[c] = Orthographic(lc.x - radius, lc.x + radius, lc.y - radius, lc.y + radius, -high, -low) * turn;
        plan.splitFar[c] = end;
        plan.texel[c] = texel;
        plan.depthRange[c] = high - low;
        start = end;
    }
    return plan;
}

}  // namespace rm::space3d
