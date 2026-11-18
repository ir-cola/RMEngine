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

#include "platform/Pixels.h"
#include "platform/Win.h"

#include <d2d1_1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rm::platform {


// nullopt: *error says why (Japanese).
std::optional<Image> DecodeImage(IWICImagingFactory* wic, std::span<const uint8_t> bytes, PixelOrder order, Fit fit,
                                 std::string* error);
std::optional<Image> LoadImageFile(IWICImagingFactory* wic, const std::filesystem::path& file, PixelOrder order, Fit fit,
                                   std::string* error);

// The picture's size in pixels, read from its header only (nothing decoded); nullopt when it is not
// a picture WIC reads.
std::optional<std::pair<uint32_t, uint32_t>> ImageFileSize(IWICImagingFactory* wic, const std::filesystem::path& file);

// A WIC factory for the calling thread (made the first time, COM started for it): what a worker
// thread decodes with (the UI's factory, Gfx::Wic, stays on the UI thread). Null when it cannot be made.
IWICImagingFactory* ThreadImagingFactory();

// A Direct2D bitmap of a PremultipliedBgra image (null when it cannot be made).
Microsoft::WRL::ComPtr<ID2D1Bitmap> BitmapFromImage(ID2D1DeviceContext* dc, const Image& image);

}  // namespace rm::platform
