#pragma once
// 3D 空間 - 絵を GPU でどう読み、どう作るか。画像の取り込みの設定から決める
// (asset::ImageImport: フィルタ・端の扱い・ミップマップ・透明部分を保つ・ストリーミング)。ここに
// 1 度だけ書くので、エディターのテクスチャも、書き出しの焼いたモデルも、ゲームも同じ読み方になる。

#include "core/asset/ImportSettings.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"

#include <cstddef>
#include <cstdint>

namespace rm::space3d {

struct TextureSampling {
    asset::TextureFilter filter = asset::TextureFilter::Smooth;
    asset::TextureWrap wrap = asset::TextureWrap::Repeat;
    bool streamed = false;  // ミップマップ・ストリーミング: 画面での大きさに要るレベルだけを GPU に置く
    bool operator==(const TextureSampling&) const = default;
};

inline TextureSampling SamplingOf(const asset::ImageImport& imp) { return {imp.filter, imp.wrap, imp.mipmaps && imp.streaming}; }

inline MipOptions MipOptionsOf(const asset::ImageImport& imp) {
    return {imp.mipmaps, imp.mipmaps && imp.keepCoverage ? imp.coverageCutoff : 0.f};
}

// 1 バイト (焼いたモデルの "SHRP" チャンク): ビット 0 くっきり、ビット 1-2 端の扱い、ビット 3
// ストリーミング。以前のファイルは 0 / 1 (くっきり) を持つ。同じに読む。
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

// レンダラーはフィルタと端の扱いの組ごとにサンプラーを 1 つ持つ: filter * 3 + wrap。
inline constexpr size_t kSamplerCount = 6;
inline size_t SamplerIndex(const TextureSampling& s) {
    return (s.filter == asset::TextureFilter::Sharp ? 3u : 0u) + static_cast<size_t>(s.wrap) % 3u;
}

}  // namespace rm::space3d
