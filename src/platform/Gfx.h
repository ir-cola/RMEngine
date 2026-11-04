#pragma once
// GPU and 2D drawing setup: one Direct3D 11 device shared by every window,
// Direct2D on top of it (for the UI), DirectWrite (text) and WIC (images).
//
// Rules from the previous engine (design doc RME_PLATFORM 7-1):
//  * every window's swap chain is FLIP_DISCARD + DXGI_SCALING_NONE (never stretched)
//  * the window redraws inside WM_SIZE, so dragging the border never freezes the picture

#include "platform/Win.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>

namespace rm::platform {

class Gfx {
public:
    bool Init();

    ID3D11Device* D3D() const { return d3d_.Get(); }
    ID3D11DeviceContext* D3DContext() const { return d3dContext_.Get(); }
    IDXGIFactory2* Dxgi() const { return dxgiFactory_.Get(); }
    ID2D1Factory1* D2DFactory() const { return d2dFactory_.Get(); }
    ID2D1Device* D2D() const { return d2dDevice_.Get(); }
    IDWriteFactory* DWrite() const { return dwrite_.Get(); }
    IWICImagingFactory* Wic() const { return wic_.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
};

// The swap chain + Direct2D device context of one window.
class SwapTarget {
public:
    bool Create(Gfx& gfx, HWND hwnd, float dpi);
    void Reset();
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);
    bool Present();

    ID2D1DeviceContext* Context() const { return dc_.Get(); }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }
    float Dpi() const { return dpi_; }

private:
    bool CreateTargetBitmap();

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
    UINT width_ = 0;
    UINT height_ = 0;
    float dpi_ = 96.f;
};

// Bytes of an RCDATA resource embedded in the exe (empty when missing).
std::span<const uint8_t> ResourceBytes(int id);

// Decodes an image (PNG/JPEG/BMP...) with WIC. maxWidth > 0 scales down (keeping the aspect).
Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(Gfx& gfx, ID2D1DeviceContext* dc, std::span<const uint8_t> data,
                                                         UINT maxWidth = 0);

}  // namespace rm::platform
