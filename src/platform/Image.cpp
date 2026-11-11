#include "platform/Image.h"

#include "base/Utf.h"

#include <algorithm>
#include <format>
#include <fstream>

namespace rm::platform {

using Microsoft::WRL::ComPtr;

namespace {

// More than this is not a picture to open whole (a broken file that says it is huge).
constexpr uintmax_t kMaxFileBytes = 512ull << 20;
constexpr uint64_t kMaxPixels = 16384ull * 16384ull;

bool Fail(std::string* error, std::string why) {
    if (error) *error = std::move(why);
    return false;
}

}  // namespace

std::optional<Image> DecodeImage(IWICImagingFactory* wic, std::span<const uint8_t> bytes, PixelOrder order, Fit fit,
                                 std::string* error) {
    if (!wic) {
        Fail(error, "画像を開く準備ができていません (WIC)");
        return std::nullopt;
    }
    if (bytes.empty() || bytes.size() > kMaxFileBytes) {
        Fail(error, bytes.empty() ? "画像が空です" : "画像が大きすぎます");
        return std::nullopt;
    }
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) {
        Fail(error, "画像の形式が分からないか、壊れています (PNG / JPEG などが読めます)");
        return std::nullopt;
    }
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0 || static_cast<uint64_t>(w) * h > kMaxPixels) {
        Fail(error, std::format("画像の大きさが正しくありません ({} x {})", w, h));
        return std::nullopt;
    }
    ComPtr<IWICBitmapSource> source = frame;
    const double kw = fit.maxWidth > 0 && w > fit.maxWidth ? static_cast<double>(fit.maxWidth) / w : 1.0;
    const double kh = fit.maxHeight > 0 && h > fit.maxHeight ? static_cast<double>(fit.maxHeight) / h : 1.0;
    if (kw < 1.0 || kh < 1.0) {
        const double k = std::min(kw, kh);
        const UINT nw = std::max<UINT>(1, static_cast<UINT>(w * k + 0.5)), nh = std::max<UINT>(1, static_cast<UINT>(h * k + 0.5));
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) && SUCCEEDED(scaler->Initialize(source.Get(), nw, nh, WICBitmapInterpolationModeFant))) {
            source = scaler;
            w = nw;
            h = nh;
        }
    }
    ComPtr<IWICFormatConverter> conv;
    const GUID format = order == PixelOrder::Rgba ? GUID_WICPixelFormat32bppRGBA : GUID_WICPixelFormat32bppPBGRA;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(source.Get(), format, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) {
        Fail(error, "画像の色の形式を変えられませんでした");
        return std::nullopt;
    }
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(img.pixels.size()), img.pixels.data()))) {
        Fail(error, "画像を読み切れませんでした (途中で壊れています)");
        return std::nullopt;
    }
    return img;
}

std::optional<Image> LoadImageFile(IWICImagingFactory* wic, const std::filesystem::path& file, PixelOrder order, Fit fit,
                                   std::string* error) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(file, ec);
    std::ifstream in(file, std::ios::binary);
    if (ec || !in) {
        Fail(error, std::format("「{}」を開けません (ファイルが無いか、使われています)", PathToUtf8(file.filename())));
        return std::nullopt;
    }
    if (size > kMaxFileBytes) {
        Fail(error, std::format("「{}」が大きすぎます", PathToUtf8(file.filename())));
        return std::nullopt;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
        Fail(error, std::format("「{}」を読めません", PathToUtf8(file.filename())));
        return std::nullopt;
    }
    return DecodeImage(wic, bytes, order, fit, error);
}

IWICImagingFactory* ThreadImagingFactory() {
    struct Holder {
        ComPtr<IWICImagingFactory> factory;
        bool com = false;  // this thread's COM was started here (ended with the thread)
        ~Holder() {
            factory.Reset();
            if (com) CoUninitialize();
        }
    };
    thread_local Holder h;
    if (!h.factory) {
        h.com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));  // a thread that has COM already keeps its own
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&h.factory));
    }
    return h.factory.Get();
}

ComPtr<ID2D1Bitmap> BitmapFromImage(ID2D1DeviceContext* dc, const Image& image) {
    ComPtr<ID2D1Bitmap> bmp;
    if (!dc || image.width == 0 || image.pixels.size() < static_cast<size_t>(image.width) * image.height * 4) return bmp;
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    dc->CreateBitmap(D2D1::SizeU(image.width, image.height), image.pixels.data(), image.width * 4, props, &bmp);
    return bmp;
}

}  // namespace rm::platform
