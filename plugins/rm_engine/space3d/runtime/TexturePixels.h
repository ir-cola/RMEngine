#pragma once
// 3D 空間 - a picture ready to go to the GPU: itself and its smaller copies (mipmaps: no shimmer far
// away), each half the one before, down to 1 x 1, as plain RGBA or block-compressed (BlockCompress.h).
// Made on the CPU - on any thread, so the slow part never stops the drawing; only
// Renderer::UploadTexture is on the drawing thread. Pure (tested in tests/TestSpace3d.cpp).

#include "platform/Pixels.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rm::space3d {

// How the texels are kept on the GPU. The BC formats hold 4 x 4 texels in one block (8 or 16 bytes):
//   BC1  RGB (+ 1 bit of alpha)  8 bytes  - colours without see-through parts
//   BC3  RGBA                   16 bytes  - colours with alpha, on GPUs without BC7
//   BC4  one channel            8 bytes  - a grey picture of numbers (read as .r)
//   BC5  two channels           16 bytes  - normal maps (x, y; z is worked out)
//   BC7  RGBA, better            16 bytes  - colours with alpha (feature level 11.0)
enum class TextureFormat : uint8_t { Rgba8, BC1, BC3, BC4, BC5, BC7 };

bool IsBlockCompressed(TextureFormat f);
size_t BlockBytes(TextureFormat f);  // a 4 x 4 block (0 for Rgba8)
// The bytes of one row of the level (of texels, or of blocks), and of the whole level.
size_t RowPitch(TextureFormat f, uint32_t width);
size_t LevelBytes(TextureFormat f, uint32_t width, uint32_t height);
// Channels the format keeps (4 / 3 / 1 / 2): what the shader may read.
int ChannelsOf(TextureFormat f);

struct TexturePixels {
    // [0] = the picture. Each level's `pixels` holds LevelBytes(format, width, height) bytes: RGBA
    // texels (PixelOrder::Rgba) or blocks, row after row.
    std::vector<platform::Image> levels;
    bool srgb = true;  // a colour picture (averaged as light; read as linear) or numbers
    TextureFormat format = TextureFormat::Rgba8;
};

// Colour pictures are averaged as light (sRGB -> linear -> back, by tables), so smaller copies do not
// darken; numbers (and alpha) are averaged as they are. Empty levels when the picture is empty.
// The image's import settings for its smaller copies (ImageImport: ミップマップ・透明部分を保つ).
struct MipOptions {
    bool mipmaps = true;     // false: only the picture itself (one level)
    float keepCoverage = 0;  // > 0: each smaller copy's alpha is scaled so that as many texels as in the
                             // picture are at least this (cut-out leaves and fences do not fade away)
};
TexturePixels MakeTexturePixels(platform::Image rgba, bool srgb, const MipOptions& options = {});

// How much of the picture (0 .. 1) has alpha at least `cutoff` (0 .. 1).
float AlphaCoverage(const platform::Image& rgba, float cutoff);

}  // namespace rm::space3d
