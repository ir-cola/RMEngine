#pragma once
// 3D 空間 - pictures compressed the way the GPU reads them directly (BC = block compression: every
// 4 x 4 texels in 8 or 16 bytes - 4 to 8 times less memory than RGBA, and quicker to send).
// Pure (tested in tests/TestSpace3d.cpp): which format a picture gets, and the block codecs.
//
//   BC1 / BC3 / BC4 / BC5   two end colours (or values) and where each texel lies between them
//   BC7                     mode 6 only: one line through RGBA with 16 steps (alpha kept as finely
//                           as colour). All written here - no library (docs/TECHNOLOGIES.md says why)
//
// Slow (a 4096 x 4096 picture takes seconds): only on a worker thread, and the result is kept on
// disk (TextureFile.h) so it is done once.

#include "core/asset/ImportSettings.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"

#include <cstdint>

namespace rm::space3d {

// What a picture is used for (which channels the shader reads, and how).
enum class TextureUse : uint8_t {
    Color,   // rgb (+ a): a colour or emission picture (sRGB)
    Normal,  // xy (+ z): a normal map - BC5 keeps x and y, the shader works out z
    Data,    // numbers (metal / rough): a grey one may be kept in one channel (BC4, read as .rrr)
};

struct PictureTraits {
    bool alpha = false;  // some texel is see-through (alpha < 255)
    bool gray = false;   // every texel has r = g = b (one channel is enough)
};
PictureTraits TraitsOf(const platform::Image& rgba);

// The format for a picture of `width` x `height` (its top level). A size that is not a multiple of 4
// is not compressed (Direct3D 11 wants whole blocks at the top). Auto: colours BC1, or BC7 with alpha
// (BC3 where the GPU has no BC7); normal maps BC5; grey numbers BC4, other numbers BC1. A setting the
// use cannot read (BC4 for a colour, ...) is taken as Auto; BC7 without BC7 is BC3.
TextureFormat ChooseFormat(TextureUse use, asset::TextureCompression setting, PictureTraits traits, bool canBC7, uint32_t width,
                           uint32_t height);

// One 4 x 4 block: `rgba` is 16 texels row by row (64 bytes). BC4 takes / gives 16 single values;
// BC5 reads r and g (decoding gives b = 0, a = 255); BC1 ignores alpha (decodes as 255).
void EncodeBC1(const uint8_t rgba[64], uint8_t out[8]);
void DecodeBC1(const uint8_t in[8], uint8_t rgba[64]);
void EncodeBC4(const uint8_t values[16], uint8_t out[8]);
void DecodeBC4(const uint8_t in[8], uint8_t values[16]);
void EncodeBC5(const uint8_t rgba[64], uint8_t out[16]);
void DecodeBC5(const uint8_t in[16], uint8_t rgba[64]);
void EncodeBC3(const uint8_t rgba[64], uint8_t out[16]);
void DecodeBC3(const uint8_t in[16], uint8_t rgba[64]);
void EncodeBC7(const uint8_t rgba[64], uint8_t out[16]);  // mode 6
void DecodeBC7(const uint8_t in[16], uint8_t rgba[64]);   // mode 6 only (others: magenta)

// Every level of an RGBA picture in `format` (the edge blocks of a level not a multiple of 4 repeat
// its last row / column). Rgba8 gives the picture as it is.
TexturePixels Compress(const TexturePixels& rgba, TextureFormat format);
// One level back to RGBA (to look at it, and to test).
platform::Image Decompress(const platform::Image& level, TextureFormat format);

}  // namespace rm::space3d
