#pragma once
// データ (リフレクション、保存) と描画が共有する小さな値の型。純粋。

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

// 線形の 0..1 の成分。UI が描く色でもある (ui::Color はこの型)。
struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static constexpr Color Hex(uint32_t rgb, float alpha = 1.f) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha};
    }
    constexpr Color Alpha(float na) const { return {r, g, b, na}; }
    bool operator==(const Color&) const = default;
};

// t = 0 で a、t = 1 で b (全成分、アルファも)。
constexpr Color Mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
// 白 (amount > 0) か黒 (amount < 0) へ寄せる。アルファは保つ: 同じ色相で明るく、または暗くする。
constexpr Color Shade(Color c, float amount) {
    const Color to = amount >= 0 ? Color{1, 1, 1, c.a} : Color{0, 0, 0, c.a};
    return Mix(c, to, amount >= 0 ? amount : -amount);
}

}  // namespace rm
