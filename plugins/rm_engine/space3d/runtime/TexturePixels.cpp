#include "rm_engine/space3d/runtime/TexturePixels.h"

#include "rm_engine/space3d/runtime/Mesh.h"

#include <algorithm>
#include <cstdint>

namespace rm::space3d {

TexturePixels MakeTexturePixels(platform::Image rgba, bool srgb) {
    TexturePixels out;
    out.srgb = srgb;
    if (rgba.width == 0 || rgba.height == 0 || rgba.pixels.size() < static_cast<size_t>(rgba.width) * rgba.height * 4) return out;
    // sRGB <-> linear by tables (a pow per texel made a 4096 x 4096 picture take seconds).
    static const auto tables = [] {
        struct Tables {
            float toLinear[256];
            uint8_t toScreen[4096];  // linear 0..1 in 4096 steps -> the screen's byte
        } t{};
        for (int v = 0; v < 256; ++v) t.toLinear[v] = ScreenToLinear(static_cast<float>(v) / 255.f);
        for (int i = 0; i < 4096; ++i) t.toScreen[i] = static_cast<uint8_t>(LinearToScreen(static_cast<float>(i) / 4095.f) * 255.f + 0.5f);
        return t;
    }();
    out.levels.push_back(std::move(rgba));
    while (out.levels.back().width > 1 || out.levels.back().height > 1) {
        const platform::Image& a = out.levels.back();
        platform::Image b;
        b.width = std::max(1u, a.width / 2);
        b.height = std::max(1u, a.height / 2);
        b.pixels.resize(static_cast<size_t>(b.width) * b.height * 4);
        for (uint32_t y = 0; y < b.height; ++y) {
            const uint32_t y0 = std::min(y * 2, a.height - 1), y1 = std::min(y * 2 + 1, a.height - 1);
            for (uint32_t x = 0; x < b.width; ++x) {
                const uint32_t x0 = std::min(x * 2, a.width - 1), x1 = std::min(x * 2 + 1, a.width - 1);
                const uint8_t* p00 = &a.pixels[(static_cast<size_t>(y0) * a.width + x0) * 4];
                const uint8_t* p10 = &a.pixels[(static_cast<size_t>(y0) * a.width + x1) * 4];
                const uint8_t* p01 = &a.pixels[(static_cast<size_t>(y1) * a.width + x0) * 4];
                const uint8_t* p11 = &a.pixels[(static_cast<size_t>(y1) * a.width + x1) * 4];
                uint8_t* o = &b.pixels[(static_cast<size_t>(y) * b.width + x) * 4];
                for (uint32_t k = 0; k < 4; ++k) {
                    if (k < 3 && srgb) {
                        const float sum = tables.toLinear[p00[k]] + tables.toLinear[p10[k]] + tables.toLinear[p01[k]] + tables.toLinear[p11[k]];
                        o[k] = tables.toScreen[std::min(4095, static_cast<int>(sum * 0.25f * 4095.f + 0.5f))];
                    } else {
                        o[k] = static_cast<uint8_t>((uint32_t{p00[k]} + p10[k] + p01[k] + p11[k] + 2) / 4);
                    }
                }
            }
        }
        out.levels.push_back(std::move(b));
    }
    return out;
}

}  // namespace rm::space3d
