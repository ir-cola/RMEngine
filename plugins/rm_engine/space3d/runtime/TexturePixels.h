#pragma once
// 3D 空間 - a picture ready to go to the GPU: itself and its smaller copies (mipmaps: no shimmer far
// away), each half the one before, down to 1 x 1. Made on the CPU - on any thread, so the slow part
// never stops the drawing; only Renderer::UploadTexture is on the drawing thread. Pure (tested in
// tests/TestSpace3d.cpp).

#include "platform/Pixels.h"

#include <vector>

namespace rm::space3d {

struct TexturePixels {
    std::vector<platform::Image> levels;  // [0] = the picture (PixelOrder::Rgba)
    bool srgb = true;                     // a colour picture (averaged as light) or numbers
};

// Colour pictures are averaged as light (sRGB -> linear -> back, by tables), so smaller copies do not
// darken; numbers (and alpha) are averaged as they are. Empty levels when the picture is empty.
TexturePixels MakeTexturePixels(platform::Image rgba, bool srgb);

}  // namespace rm::space3d
