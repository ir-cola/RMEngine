#include "rm_engine/space3d/runtime/Lighting.h"

#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::space3d {

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;
constexpr float kSpotFadeStart = 0.75f;  // a spot is full inside 3/4 of its half angle

Vec3 DirectionOr(Vec3 d, Vec3 fallback) {
    const Vec3 n = Normalize(d);
    return Length(n) == 0 ? fallback : n;
}

}  // namespace

LightSetup MakeLights(std::span<const Light> lights, Vec3 defaultDirection) {
    LightSetup s;
    const Light fallback{.direction = defaultDirection};
    const std::span<const Light> used = lights.empty() ? std::span<const Light>(&fallback, 1) : lights;
    const size_t n = std::min(used.size(), kMaxLights);
    s.dropped = used.size() - n;
    s.block.info[0] = static_cast<float>(n);
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
        if (s.shadowLight < 0 && l.type == LightType::Directional && l.castShadow) {
            s.shadowLight = static_cast<int>(i);
            s.shadowDirection = dir;
            s.block.color[i][3] = 1.f;
        }
    }
    return s;
}

}  // namespace rm::space3d
