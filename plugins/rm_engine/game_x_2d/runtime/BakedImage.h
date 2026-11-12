#pragma once
// ゲーム制作 × 2D - a sprite's image as the exported game gets it: the picture file as it is (PNG /
// JPEG ... read by WIC in the game, never compressed further) with what the editor knows of it in
// front - the file's own size (a sprite's size in the world comes from it, even when the picture is
// read smaller), the import settings くっきり and 大きさの上限. Pure - tested in tests/TestBakedImage.cpp.
//
//   "RMIMAGE\0"  JSON length u32  JSON {"width", "height", "sharp", "maxSize", ...}  then the file's bytes
// Keys this version does not know are skipped (self-describing, as the design's rule).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rm::game_x_2d {

// What Direct2D can hold in one bitmap: a bigger picture is read smaller (the sprite keeps its size).
inline constexpr uint32_t kMaxImageSize = 16384;

struct BakedImageInfo {
    uint32_t width = 0, height = 0;  // the file's pixels (0: not known - a picture not baked)
    bool sharp = false;              // drawn with the nearest pixel (the import setting くっきり)
    uint32_t maxSize = 0;            // read no bigger than this (the import setting 大きさの上限; 0: no limit)
};

struct BakedImage {
    BakedImageInfo info;
    std::string_view picture;  // the picture file's bytes (inside the bytes given)
};

std::string WriteBakedImage(const BakedImageInfo& info, std::string_view picture);

// A picture file not baked (no header) is given back as it is (info all 0). nullopt and *error
// (Japanese) when the header is broken.
std::optional<BakedImage> ReadBakedImage(std::string_view bytes, std::string* error);

}  // namespace rm::game_x_2d
