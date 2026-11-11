#pragma once
// A picture as plain pixels (what platform/Image.h decodes into, what the 3D textures are made of).
// Only the standard library: code that must not see Windows' headers (pure code and its tests) can
// hold pictures too.

#include <cstdint>
#include <vector>

namespace rm::platform {

enum class PixelOrder { Rgba, PremultipliedBgra };

// A picture bigger than this is scaled down into it, keeping its shape (0 = no limit that way).
struct Fit {
    uint32_t maxWidth = 0, maxHeight = 0;
};

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;  // 4 bytes per pixel, rows from the top, no gaps
};

}  // namespace rm::platform
