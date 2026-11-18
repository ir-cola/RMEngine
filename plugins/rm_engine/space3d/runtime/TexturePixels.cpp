#include "rm_engine/space3d/runtime/TexturePixels.h"

#include "rm_engine/space3d/runtime/Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace rm::space3d {

bool IsBlockCompressed(TextureFormat f) { return f != TextureFormat::Rgba8; }

size_t BlockBytes(TextureFormat f) {
    switch (f) {
        case TextureFormat::BC1:
        case TextureFormat::BC4: return 8;
        case TextureFormat::BC3:
        case TextureFormat::BC5:
        case TextureFormat::BC7: return 16;
        case TextureFormat::Rgba8: break;
    }
    return 0;
}

size_t RowPitch(TextureFormat f, uint32_t width) {
    if (!IsBlockCompressed(f)) return static_cast<size_t>(width) * 4;
    return static_cast<size_t>(std::max(1u, (width + 3) / 4)) * BlockBytes(f);
}

size_t LevelBytes(TextureFormat f, uint32_t width, uint32_t height) {
    const size_t rows = IsBlockCompressed(f) ? std::max(1u, (height + 3) / 4) : height;
    return RowPitch(f, width) * rows;
}

int ChannelsOf(TextureFormat f) {
    switch (f) {
        case TextureFormat::BC1: return 3;
        case TextureFormat::BC4: return 1;
        case TextureFormat::BC5: return 2;
        default: return 4;
    }
}

float AlphaCoverage(const platform::Image& rgba, float cutoff) {
    const size_t n = static_cast<size_t>(rgba.width) * rgba.height;
    if (n == 0 || rgba.pixels.size() < n * 4) return 0;
    const int at = std::clamp(static_cast<int>(std::ceil(cutoff * 255.f - 1e-4f)), 0, 255);
    size_t in = 0;
    for (size_t i = 0; i < n; ++i) in += rgba.pixels[i * 4 + 3] >= at;
    return static_cast<float>(in) / static_cast<float>(n);
}

namespace {

// The level's alpha times `scale` (to 255 at most).
platform::Image ScaledAlpha(const platform::Image& level, float scale) {
    platform::Image out = level;
    for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = static_cast<uint8_t>(std::min(255.f, out.pixels[i] * scale + 0.5f));
    return out;
}

// The scale (found by halving the range) that makes the level cover as much as `target`. Counted on
// the level's alpha histogram (no copy of the picture per try).
void KeepCoverage(platform::Image& level, float cutoff, float target) {
    const size_t n = static_cast<size_t>(level.width) * level.height;
    if (n == 0) return;
    std::array<size_t, 256> histogram{};
    for (size_t i = 3; i < level.pixels.size(); i += 4) ++histogram[level.pixels[i]];
    const int at = std::clamp(static_cast<int>(std::ceil(cutoff * 255.f - 1e-4f)), 0, 255);
    auto coverageAt = [&](float scale) {
        size_t in = 0;
        for (int a = 0; a < 256; ++a) {
            if (std::min(255.f, static_cast<float>(a) * scale + 0.5f) >= static_cast<float>(at)) in += histogram[static_cast<size_t>(a)];
        }
        return static_cast<float>(in) / static_cast<float>(n);
    };
    float lo = 0.f, hi = 8.f, best = 1.f, bestMiss = std::abs(coverageAt(1.f) - target);
    for (int step = 0; step < 20; ++step) {
        const float mid = (lo + hi) * 0.5f;
        const float got = coverageAt(mid);
        if (std::abs(got - target) < bestMiss) {
            bestMiss = std::abs(got - target);
            best = mid;
        }
        (got < target ? lo : hi) = mid;
    }
    if (best != 1.f) level = ScaledAlpha(level, best);
}

}  // namespace

TexturePixels MakeTexturePixels(platform::Image rgba, bool srgb, const MipOptions& options) {
    TexturePixels out;
    out.srgb = srgb;
    if (rgba.width == 0 || rgba.height == 0 || rgba.pixels.size() < static_cast<size_t>(rgba.width) * rgba.height * 4) return out;
    if (!options.mipmaps) {
        out.levels.push_back(std::move(rgba));
        return out;
    }
    const float cutoff = std::min(options.keepCoverage, 1.f);
    const float coverage = cutoff > 0 ? AlphaCoverage(rgba, cutoff) : 0.f;
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
    // (each smaller copy from the one before as it was averaged: the scaled alpha is only what is kept)
    if (cutoff > 0) {
        for (size_t i = 1; i < out.levels.size(); ++i) KeepCoverage(out.levels[i], cutoff, coverage);
    }
    return out;
}

}  // namespace rm::space3d
