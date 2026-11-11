#include "platform/Gfx.h"

#include "base/Log.h"

using Microsoft::WRL::ComPtr;

namespace rm::platform {

bool Gfx::Init() {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;  // required by Direct2D
    auto create = [&](D3D_DRIVER_TYPE type, UINT f) {
        d3d_.Reset();
        d3dContext_.Reset();
        return D3D11CreateDevice(nullptr, type, nullptr, f, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                 &d3d_, nullptr, &d3dContext_);
    };
    HRESULT hr = E_FAIL;
#ifdef _DEBUG
    hr = create(D3D_DRIVER_TYPE_HARDWARE, flags | D3D11_CREATE_DEVICE_DEBUG);  // fails when the SDK layers are missing
#endif
    if (FAILED(hr)) hr = create(D3D_DRIVER_TYPE_HARDWARE, flags);
    if (FAILED(hr)) {
        log::Warn("ハードウェアの Direct3D 11 を作れませんでした (0x{:08X})。WARP (ソフトウェア) で続けます", static_cast<uint32_t>(hr));
        hr = create(D3D_DRIVER_TYPE_WARP, flags);
    }
    if (FAILED(hr)) {
        log::Error("Direct3D 11 のデバイスを作れませんでした (0x{:08X})", static_cast<uint32_t>(hr));
        return false;
    }

    ComPtr<IDXGIDevice1> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(d3d_.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory_)))) {
        log::Error("DXGI の初期化に失敗しました");
        return false;
    }

    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, d2dFactory_.GetAddressOf())) ||
        FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) {
        log::Error("Direct2D の初期化に失敗しました");
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
        log::Error("DirectWrite の初期化に失敗しました");
        return false;
    }
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_)))) {
        log::Error("WIC の初期化に失敗しました");
        return false;
    }
    log::Info("グラフィックスを初期化しました");
    return true;
}

// ---------------------------------------------------------------------------

bool SwapTarget::Create(Gfx& gfx, HWND hwnd, float dpi) {
    dpi_ = dpi;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    width_ = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    height_ = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = width_;
    d.Height = height_;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.Scaling = DXGI_SCALING_NONE;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = gfx.Dxgi()->CreateSwapChainForHwnd(gfx.D3D(), hwnd, &d, nullptr, nullptr, &swap_);
    if (FAILED(hr)) {
        log::Error("スワップチェーンを作れませんでした (0x{:08X})", static_cast<uint32_t>(hr));
        return false;
    }
    gfx.Dxgi()->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    hr = gfx.D2D()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_);
    if (FAILED(hr)) {
        log::Error("Direct2D のデバイスコンテキストを作れませんでした (0x{:08X})", static_cast<uint32_t>(hr));
        return false;
    }
    return CreateTargetBitmap();
}

void SwapTarget::Reset() {
    if (dc_) dc_->SetTarget(nullptr);
    target_.Reset();
    dc_.Reset();
    swap_.Reset();
}

bool SwapTarget::CreateTargetBitmap() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi_, dpi_);
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &target_))) {
        log::Error("描画先のビットマップを作れませんでした");
        return false;
    }
    dc_->SetTarget(target_.Get());
    dc_->SetDpi(dpi_, dpi_);
    return true;
}

void SwapTarget::Resize(UINT width, UINT height) {
    if (!swap_ || width == 0 || height == 0) return;
    if (width == width_ && height == height_) return;
    dc_->SetTarget(nullptr);
    target_.Reset();  // every reference to the back buffer must be gone before ResizeBuffers
    const HRESULT hr = swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        log::Error("スワップチェーンの大きさを変えられませんでした (0x{:08X})", static_cast<uint32_t>(hr));
        return;
    }
    width_ = width;
    height_ = height;
    CreateTargetBitmap();
}

void SwapTarget::SetDpi(float dpi) {
    dpi_ = dpi;
    if (dc_) dc_->SetDpi(dpi, dpi);
}

bool SwapTarget::Present() {
    if (!swap_) return false;
    const HRESULT hr = swap_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        log::Error("GPU のデバイスが失われました (0x{:08X})", static_cast<uint32_t>(hr));
        return false;
    }
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------

std::span<const uint8_t> ResourceBytes(int id) {
    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res) return {};
    HGLOBAL mem = LoadResource(mod, res);
    if (!mem) return {};
    const void* p = LockResource(mem);
    const DWORD size = SizeofResource(mod, res);
    if (!p || size == 0) return {};
    return {static_cast<const uint8_t*>(p), size};
}

ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(Gfx& gfx, ID2D1DeviceContext* dc, std::span<const uint8_t> data, UINT maxWidth,
                                         std::string* error) {
    ComPtr<ID2D1Bitmap> bmp;
    auto fail = [&](const char* why) {
        if (error) {
            *error = why;
        } else {
            log::Warn("{}", why);
        }
    };
    if (data.empty() || !dc || !gfx.Wic()) {
        fail(data.empty() ? "画像のファイルが空です" : "画像を読む準備ができていません");
        return bmp;
    }
    IWICImagingFactory* wic = gfx.Wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), static_cast<DWORD>(data.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) {
        fail("画像を読めませんでした (WIC が知らない形か、壊れたファイル)");
        return bmp;
    }
    ComPtr<IWICBitmapSource> source = frame;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (maxWidth > 0 && w > maxWidth) {
        ComPtr<IWICBitmapScaler> scaler;
        const UINT nh = std::max<UINT>(1, static_cast<UINT>(static_cast<uint64_t>(h) * maxWidth / w));
        if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
            SUCCEEDED(scaler->Initialize(source.Get(), maxWidth, nh, WICBitmapInterpolationModeHighQualityCubic))) {
            source = scaler;
        }
    }
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeMedianCut)) ||
        FAILED(dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp))) {
        fail("画像を変換できませんでした");
        bmp.Reset();
    }
    return bmp;
}

}  // namespace rm::platform
