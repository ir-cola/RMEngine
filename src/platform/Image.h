#pragma once
// Pictures (PNG / JPEG / BMP / GIF / TIFF ... whatever Windows can open) turned into pixels, with
// WIC (Windows Imaging Component - part of every Windows; no library of our own).
//
//   std::string error;
//   auto img = platform::DecodeImage(gfx.Wic(), bytes, platform::PixelOrder::Rgba, {4096, 4096}, &error);
//   // img->pixels: width * height * 4 bytes, rows from the top
//
// Rgba is for a 3D texture (straight alpha, as the file says); PremultipliedBgra is what Direct2D
// draws (BitmapFromImage). A picture bigger than `fit` is scaled down into it, keeping its shape
// (0 = no limit that way). Used by the game too (textures), so it ships in games.

#include "platform/Win.h"

#include <d2d1_1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rm::platform {

enum class PixelOrder { Rgba, PremultipliedBgra };

struct Fit {
    uint32_t maxWidth = 0, maxHeight = 0;
};

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;  // 4 bytes per pixel, rows from the top, no gaps
};

// nullopt: *error says why (Japanese).
std::optional<Image> DecodeImage(IWICImagingFactory* wic, std::span<const uint8_t> bytes, PixelOrder order, Fit fit,
                                 std::string* error);
std::optional<Image> LoadImageFile(IWICImagingFactory* wic, const std::filesystem::path& file, PixelOrder order, Fit fit,
                                   std::string* error);

// A Direct2D bitmap of a PremultipliedBgra image (null when it cannot be made).
Microsoft::WRL::ComPtr<ID2D1Bitmap> BitmapFromImage(ID2D1DeviceContext* dc, const Image& image);

}  // namespace rm::platform
