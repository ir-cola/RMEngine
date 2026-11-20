#pragma once
// 画像 (PNG / JPEG / BMP / GIF / TIFF ... Windows が開けるもの全部) を、WIC (Windows Imaging
// Component - どの Windows にも入っている。自前のライブラリは無い) でピクセルにする。
//
//   std::string error;
//   auto img = platform::DecodeImage(gfx.Wic(), bytes, platform::PixelOrder::Rgba, {4096, 4096}, &error);
//   // img->pixels: width * height * 4 バイト。行は上から
//
// Rgba は 3D のテクスチャ用 (ストレートアルファ。ファイルの言うとおり)。PremultipliedBgra は
// Direct2D が描く形 (BitmapFromImage)。`fit` より大きい画像は、形を保ったまま、その中に収まるよう
// 縮小する (0 = その方向に制限なし)。ゲームも使う (テクスチャ) ので、ゲームに入る。

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


// nullopt: *error に理由 (日本語)。
std::optional<Image> DecodeImage(IWICImagingFactory* wic, std::span<const uint8_t> bytes, PixelOrder order, Fit fit,
                                 std::string* error);
std::optional<Image> LoadImageFile(IWICImagingFactory* wic, const std::filesystem::path& file, PixelOrder order, Fit fit,
                                   std::string* error);

// 画像のピクセルでの大きさ。ヘッダーだけを読む (デコードしない)。WIC が読めないものなら nullopt。
std::optional<std::pair<uint32_t, uint32_t>> ImageFileSize(IWICImagingFactory* wic, const std::filesystem::path& file);

// 呼んだスレッド用の WIC ファクトリー (初回に作り、そのために COM を始める): ワーカー
// スレッドがデコードに使うもの (UI のファクトリー Gfx::Wic は UI スレッドに残る)。作れないときは null。
IWICImagingFactory* ThreadImagingFactory();

// PremultipliedBgra の画像の Direct2D ビットマップ (作れないときは null)。
Microsoft::WRL::ComPtr<ID2D1Bitmap> BitmapFromImage(ID2D1DeviceContext* dc, const Image& image);

}  // namespace rm::platform
