#pragma once
// GPU と 2D 描画の準備: 全ウィンドウで共有する Direct3D 11 デバイス 1 つ、
// その上の Direct2D (UI 用)、DirectWrite (文字)、WIC (画像)。
//
// 前のエンジンからの決まり (設計 RME_PLATFORM 7-1):
//  * どのウィンドウのスワップチェーンも FLIP_DISCARD + DXGI_SCALING_NONE (引き伸ばさない)
//  * ウィンドウは WM_SIZE の中で描き直す。枠をドラッグしても絵が固まらないように
//
// エディターが動いている間に GPU が失われることがある (ドライバーの更新やクラッシュ、フリーズ後の
// GPU のリセット - TDR -、外付け GPU の取り外し)。そうなると、その上で作ったオブジェクトは全部死ぬ:
// Recreate が新しいデバイスを作り、Generation() に数える。GPU の上で作ったもの (スワップチェーン、
// テクスチャ、バッファ、シェーダー、Direct2D のビットマップ / ブラシ / エフェクト) を持っている側は、
// 作ったときの世代を覚えておき、番号が変わったら捨てて作り直す:
//
//   if (gfx.Generation() != madeIn_) { DropGpuThings(); madeIn_ = gfx.Generation(); }
//
// ファクトリー (Direct2D, DirectWrite, WIC) は GPU の上にないので残る: ジオメトリと文字の
// フォーマットは生き続ける。
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

    // デバイスを何回作ったか (Init のあとは 1。Recreate のたびに 1 増える)。
    uint64_t Generation() const { return generation_; }
    // デバイスが無くなった (または SimulateLoss を呼んだ): Recreate まで、描いたものは画面に届かない。
    bool Lost() const;
    // その理由 (コンソール用。失われていなければ S_OK。模擬した喪失なら DXGI_ERROR_DEVICE_REMOVED)。
    HRESULT LostReason() const;
    // 新しい Direct3D / Direct2D デバイス (古いオブジェクトは、持ち主が捨てるまで残す)。今は作れない
    // ときは false (少しあとでもう一度)。そのときは古いデバイスをそのまま使う。
    bool Recreate();
    // 何かがデバイスの喪失を告げた (Direct2D の D2DERR_RECREATE_TARGET)、または本物の GPU リセット無しで
    // 復帰を試している: 次の Recreate まで Lost() は true。
    void MarkLost() { markedLost_ = true; }

    ID3D11Device* D3D() const { return d3d_.Get(); }
    ID3D11DeviceContext* D3DContext() const { return d3dContext_.Get(); }
    IDXGIFactory2* Dxgi() const { return dxgiFactory_.Get(); }
    ID2D1Factory1* D2DFactory() const { return d2dFactory_.Get(); }
    ID2D1Device* D2D() const { return d2dDevice_.Get(); }
    IDWriteFactory* DWrite() const { return dwrite_.Get(); }
    IWICImagingFactory* Wic() const { return wic_.Get(); }

private:
    bool CreateDevices();  // Direct3D、その DXGI ファクトリー、その上の Direct2D デバイス

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

// 1 つのウィンドウのスワップチェーン + Direct2D のデバイスコンテキスト。
class SwapTarget {
public:
    bool Create(Gfx& gfx, HWND hwnd, float dpi);
    void Reset();
    void Resize(UINT width, UINT height);
    void SetDpi(float dpi);
    // syncInterval 1: 画面に合わせる (vsync)。0: すぐに。
    bool Present(UINT syncInterval = 1);

    ID2D1DeviceContext* Context() const { return dc_.Get(); }
    // 作られたときの Gfx::Generation (0: 作られていない)。
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

// exe に埋め込んだ RCDATA リソースのバイト列 (無いときは空)。
std::span<const uint8_t> ResourceBytes(int id);

// 画像 (PNG/JPEG/BMP...) を WIC でデコードする。maxWidth > 0 なら (縦横比を保って) 縮小する。
// 失敗したときは、理由 (日本語) を `error` に入れる。`error` が null ならログに出す。
Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(Gfx& gfx, ID2D1DeviceContext* dc, std::span<const uint8_t> data,
                                                         UINT maxWidth = 0, std::string* error = nullptr);

}  // namespace rm::platform
