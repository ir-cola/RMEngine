#pragma once
// GPU and 2D drawing setup: one Direct3D 11 device shared by every window,
// Direct2D on top of it (for the UI), DirectWrite (text) and WIC (images).
//
// Rules from the previous engine (design doc RME_PLATFORM 7-1):
//  * every window's swap chain is FLIP_DISCARD + DXGI_SCALING_NONE (never stretched)
//  * the window redraws inside WM_SIZE, so dragging the border never freezes the picture
//
// The GPU can be lost while the editor runs (the driver updated or crashed, the GPU reset after a
// hang - TDR -, an external GPU unplugged). Then every object made on it is dead: Recreate makes new
// devices and counts it in Generation(). Whoever keeps something made on the GPU (a swap chain,
// textures, buffers, shaders, Direct2D bitmaps / brushes / effects) remembers the generation it was
// made in and throws it away - to make it again - when the number has changed:
//
//   if (gfx.Generation() != madeIn_) { DropGpuThings(); madeIn_ = gfx.Generation(); }
//
// The factories (Direct2D, DirectWrite, WIC) are not on the GPU and stay: geometry and text
// formats live on.
#include "platform/Win.h"

#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>
#include <string>

namespace rm::platform {

class Gfx {
public:
    bool Init();

    // How many times the devices were made (1 after Init; one more after each Recreate).
    uint64_t Generation() const { return generation_; }
    // The device is gone (or SimulateLoss was called): nothing drawn reaches the screen until Recreate.
    bool Lost() const;
    // Why, for the console (S_OK when it is not lost; DXGI_ERROR_DEVICE_REMOVED for a simulated loss).
    HRESULT LostReason() const;
    // New Direct3D / Direct2D devices (the old objects are left for their owners to drop). False when
    // they cannot be made now (try again a little later); the old devices are kept then.
    bool Recreate();
    // Something said the device is gone (Direct2D's D2DERR_RECREATE_TARGET), or the way back is being
    // tried without a real GPU reset: Lost() is true until the next Recreate.
    void MarkLost() { markedLost_ = true; }

    ID3D11Device* D3D() const { return d3d_.Get(); }
    ID3D11DeviceContext* D3DContext() const { return d3dContext_.Get(); }
    IDXGIFactory2* Dxgi() const { return dxgiFactory_.Get(); }
    ID2D1Factory1* D2DFactory() const { return d2dFactory_.Get(); }
    ID2D1Device* D2D() const { return d2dDevice_.Get(); }
    IDWriteFactory* DWrite() const { return dwrite_.Get(); }
    IWICImagingFactory* Wic() const { return wic_.Get(); }

private:
    bool CreateDevices();  // Direct3D, its DXGI factory and the Direct2D device on it

    uint64_t generation_ = 0;
    bool markedLost_ = false;
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
    // The Gfx::Generation it was made in (0: not made).
    uint64_t Generation() const { return generation_; }
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
    uint64_t generation_ = 0;
};

// Bytes of an RCDATA resource embedded in the exe (empty when missing).
std::span<const uint8_t> ResourceBytes(int id);

// Decodes an image (PNG/JPEG/BMP...) with WIC. maxWidth > 0 scales down (keeping the aspect).
// When it fails: the reason (Japanese) goes to `error`, or to the log when `error` is null.
Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(Gfx& gfx, ID2D1DeviceContext* dc, std::span<const uint8_t> data,
                                                         UINT maxWidth = 0, std::string* error = nullptr);

}  // namespace rm::platform
