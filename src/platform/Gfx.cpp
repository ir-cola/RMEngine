#include "platform/Gfx.h"

#include "base/Log.h"
#include "platform/Image.h"

using Microsoft::WRL::ComPtr;

namespace rm::platform {

bool Gfx::CreateDevices() {
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
    // (VIDEO_SUPPORT: the UI's video decoded on the GPU - Media Engine's DXVA; without it, when the driver
    // will not make such a device)
    if (FAILED(hr)) hr = create(D3D_DRIVER_TYPE_HARDWARE, flags | D3D11_CREATE_DEVICE_VIDEO_SUPPORT);
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

    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) {
        log::Error("Direct2D のデバイスを作れませんでした");
        return false;
    }
    ++generation_;
    return true;
}

bool Gfx::Lost() const { return LostReason() != S_OK; }

HRESULT Gfx::LostReason() const {
    if (!d3d_) return DXGI_ERROR_DEVICE_REMOVED;
    const HRESULT hr = d3d_->GetDeviceRemovedReason();
    if (hr != S_OK) return hr;
    return markedLost_ ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
}

bool Gfx::Recreate() {
    // The old devices go first: objects still holding them keep them alive, but nothing new is made on them.
    ComPtr<ID3D11Device> oldD3d = d3d_;
    ComPtr<ID3D11DeviceContext> oldContext = d3dContext_;
    ComPtr<IDXGIFactory2> oldDxgi = dxgiFactory_;
    ComPtr<ID2D1Device> oldD2d = d2dDevice_;
    if (d3dContext_) {
        d3dContext_->ClearState();
        d3dContext_->Flush();
    }
    d2dDevice_.Reset();
    dxgiFactory_.Reset();
    if (!CreateDevices()) {
        d3d_ = oldD3d;  // nothing changed: tried again later
        d3dContext_ = oldContext;
        dxgiFactory_ = oldDxgi;
        d2dDevice_ = oldD2d;
        return false;
    }
    markedLost_ = false;
    log::Info("GPU のデバイスを作り直しました (作り直し {} 回目)", generation_ - 1);
    return true;
}

bool Gfx::Init() {
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, d2dFactory_.GetAddressOf()))) {
        log::Error("Direct2D の初期化に失敗しました");
        return false;
    }
    if (!CreateDevices()) return false;
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
    generation_ = gfx.Generation();
    hr = gfx.D2D()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_);
    if (FAILED(hr)) {
        log::Error("Direct2D のデバイスコンテキストを作れませんでした (0x{:08X})", static_cast<uint32_t>(hr));
        return false;
    }
    return CreateTargetBitmap();
}

void SwapTarget::Reset() {
    // Direct3D destroys objects lazily: the swap chain is really gone (and the window free for a new
    // one - a window has one at a time) only after its device's context is flushed.
    ComPtr<ID3D11Device> device;
    if (swap_) swap_->GetDevice(IID_PPV_ARGS(&device));
    if (dc_) dc_->SetTarget(nullptr);
    target_.Reset();
    dc_.Reset();
    swap_.Reset();
    generation_ = 0;
    if (device) {
        ComPtr<ID3D11DeviceContext> ctx;
        device->GetImmediateContext(&ctx);
        if (ctx) ctx->Flush();
    }
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

bool SwapTarget::Present(UINT syncInterval) {
    if (!swap_) return false;
    const HRESULT hr = swap_->Present(syncInterval, 0);
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
    // The one way pictures are decoded (platform/Image: WIC), then made a Direct2D bitmap.
    std::string why;
    std::optional<Image> image;
    if (!dc) {
        why = "画像を読む準備ができていません";
    } else {
        image = DecodeImage(gfx.Wic(), data, PixelOrder::PremultipliedBgra, {maxWidth, 0}, &why);
    }
    ComPtr<ID2D1Bitmap> bmp = image ? BitmapFromImage(dc, *image) : nullptr;
    if (image && !bmp) why = "画像を変換できませんでした";
    if (!bmp) {
        if (error) {
            *error = why;
        } else {
            log::Warn("画像を読めませんでした: {}", why);
        }
    }
    return bmp;
}

}  // namespace rm::platform
