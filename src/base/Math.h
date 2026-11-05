#pragma once
// Small value types shared by data (reflection, saving) and drawing. Pure.

#include <cstdint>

namespace rm {

struct Vec2 {
    float x = 0, y = 0;
    bool operator==(const Vec2&) const = default;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};

// Linear 0..1 components. Also the colour the UI draws with (ui::Color is this type).
struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static constexpr Color Hex(uint32_t rgb, float alpha = 1.f) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha};
    }
    constexpr Color Alpha(float na) const { return {r, g, b, na}; }
    bool operator==(const Color&) const = default;
};

// a at t = 0, b at t = 1 (every component, alpha too).
constexpr Color Mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
// Towards white (amount > 0) or black (amount < 0), keeping alpha: the same hue, lighter or darker.
constexpr Color Shade(Color c, float amount) {
    const Color to = amount >= 0 ? Color{1, 1, 1, c.a} : Color{0, 0, 0, c.a};
    return Mix(c, to, amount >= 0 ? amount : -amount);
}

}  // namespace rm
