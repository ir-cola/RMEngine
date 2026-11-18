#pragma once
// 3D 空間 - how a picture is read on the GPU and made, from the image's import settings
// (asset::ImageImport: フィルタ・端の扱い・ミップマップ・透明部分を保つ・ストリーミング). Written once here, so
// the editor's textures, the export's baked models and the game read them the same way.

#include "core/asset/ImportSettings.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"

#include <cstddef>
#include <cstdint>

namespace rm::space3d {

struct TextureSampling {
    asset::TextureFilter filter = asset::TextureFilter::Smooth;
    asset::TextureWrap wrap = asset::TextureWrap::Repeat;
    bool streamed = false;  // ミップマップ・ストリーミング: only the levels its size on screen needs are on the GPU
    bool operator==(const TextureSampling&) const = default;
};

inline TextureSampling SamplingOf(const asset::ImageImport& imp) { return {imp.filter, imp.wrap, imp.mipmaps && imp.streaming}; }

inline MipOptions MipOptionsOf(const asset::ImageImport& imp) {
    return {imp.mipmaps, imp.mipmaps && imp.keepCoverage ? imp.coverageCutoff : 0.f};
}

// One byte (a baked model's "SHRP" chunk): bit 0 くっきり, bits 1-2 the wrap, bit 3 streamed. Files
// from before held 0 / 1 (くっきり) - read the same.
inline uint8_t PackSampling(const TextureSampling& s) {
    return static_cast<uint8_t>((s.filter == asset::TextureFilter::Sharp ? 1u : 0u) | (static_cast<unsigned>(s.wrap) & 3u) << 1 |
                                (s.streamed ? 8u : 0u));
}
inline TextureSampling UnpackSampling(uint8_t b) {
    TextureSampling s;
    s.filter = (b & 1u) ? asset::TextureFilter::Sharp : asset::TextureFilter::Smooth;
    const unsigned wrap = (b >> 1) & 3u;
    s.wrap = wrap <= 2 ? static_cast<asset::TextureWrap>(wrap) : asset::TextureWrap::Repeat;
    s.streamed = (b & 8u) != 0;
    return s;
}

// The renderer keeps one sampler for each filter and wrap: filter * 3 + wrap.
inline constexpr size_t kSamplerCount = 6;
inline size_t SamplerIndex(const TextureSampling& s) {
    return (s.filter == asset::TextureFilter::Sharp ? 3u : 0u) + static_cast<size_t>(s.wrap) % 3u;
}

}  // namespace rm::space3d
