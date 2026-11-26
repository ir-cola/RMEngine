#include "rm_engine/theme_gaming/editor/Motion.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::theme_gaming::motion {

namespace {

float Rand(uint32_t seed, uint32_t index, uint32_t salt) {
    uint32_t h = seed * 747796405u + index * 2891336453u + salt * 277803737u + 0x9E3779B9u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return static_cast<float>(h >> 8) * (1.f / 16777216.f);
}

float Wrap(float v, float lo, float hi) {
    const float span = hi - lo;
    if (!(span > 0)) return lo;
    float r = std::fmod(v - lo, span);
    if (r < 0) r += span;
    if (r >= span) r = 0;
    return lo + r;
}

}  // namespace

Color Rainbow(float t, float alpha) {
    t -= std::floor(t);
    const float h = t * 6.f;  // 60 度ずつ 6 つ
    const float f = h - std::floor(h);
    switch (std::min(static_cast<int>(h), 5)) {
        case 0: return {1.f, f, 0.f, alpha};        // 赤 → 黄
        case 1: return {1.f - f, 1.f, 0.f, alpha};  // 黄 → 緑
        case 2: return {0.f, 1.f, f, alpha};        // 緑 → 水色
        case 3: return {0.f, 1.f - f, 1.f, alpha};  // 水色 → 青
        case 4: return {f, 0.f, 1.f, alpha};        // 青 → 紫
        default: return {1.f, 0.f, 1.f - f, alpha}; // 紫 → 赤
    }
}

float PerimeterAt(const ui::Rect& r, int edge, float along) {
    switch (edge) {
        case 0: return along;
        case 1: return r.w + along;
        case 2: return r.w + r.h + along;
        default: return 2.f * r.w + r.h + along;
    }
}

float EdgeHue(float s, double t, float period, float speed) {
    if (!(period > 0)) return 0.f;
    const double x = static_cast<double>(s) / period - t * speed;
    return static_cast<float>(x - std::floor(x));
}

Mote MoteAt(const ui::Rect& w, uint32_t i, double t, uint32_t seed) {
    Mote m;
    const float rise = 4.f + 10.f * Rand(seed, i, 1);
    const float x = w.x + w.w * Rand(seed, i, 2) + 12.f * static_cast<float>(std::sin(t * (0.2 + 0.3 * Rand(seed, i, 3)) + 6.28 * Rand(seed, i, 4)));
    const float y = w.y + w.h * Rand(seed, i, 5) - static_cast<float>(std::fmod(t * rise, 1.0e6));
    m.at = {Wrap(x, w.x - 4.f, w.x + w.w + 4.f), Wrap(y, w.y - 4.f, w.y + w.h + 4.f)};
    m.radius = 0.8f + 1.4f * Rand(seed, i, 6);
    const float tw = 0.5f + 0.5f * static_cast<float>(std::sin(t * (0.8 + 1.6 * Rand(seed, i, 7)) + 6.28 * Rand(seed, i, 8)));
    m.alpha = 0.25f + 0.75f * tw * tw;
    m.hue = Rand(seed, i, 9);
    return m;
}

}  // namespace rm::theme_gaming::motion
