#include "ui/Grain.h"

#include <algorithm>
#include <cmath>

namespace rm::ui {

namespace {

// 格子のマス (x, y) ごとに決まる 0 .. 1 の値。
float Hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 0x8DA6B343u ^ y * 0xD8163841u ^ seed * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

uint32_t Premultiplied(Color c) {
    const float a = std::clamp(c.a, 0.f, 1.f);
    auto ch = [&](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.f, 1.f) * a * 255.f)); };
    return ch(c.b) | ch(c.g) << 8 | ch(c.r) << 16 | static_cast<uint32_t>(std::lround(a * 255.f)) << 24;
}

}  // namespace

std::vector<uint32_t> GrainPixels(int size, const GrainLook& look) {
    if (size <= 0) return {};
    const auto n = static_cast<size_t>(size);
    std::vector<uint32_t> out(n * n, 0u);
    const uint32_t light = Premultiplied(look.light), dark = Premultiplied(look.dark);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float h = Hash(static_cast<uint32_t>(x), static_cast<uint32_t>(y), look.seed);
            uint32_t px = 0;
            if (h < look.darkShare) {
                px = dark;
            } else if (h < look.darkShare + look.lightShare) {
                px = light;
            }
            // 2 x 2 の大きめの粒 (マスは端でつながる: size が偶数なら同じ所で切れる)。
            if (Hash(static_cast<uint32_t>(x / 2), static_cast<uint32_t>(y / 2), look.seed + 7u) < look.clumps) px = dark;
            out[static_cast<size_t>(y) * n + static_cast<size_t>(x)] = px;
        }
    }
    return out;
}

}  // namespace rm::ui
