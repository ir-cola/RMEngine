#pragma once
// Direct2D + DirectWrite の上の、核の UI の描画。座標はすべて DIP
// (デバイスコンテキストの DPI はウィンドウが決めるので、1 単位 = 1/96 インチ)。
// UI の中で Direct2D に触るのはこのファイルだけ: 1 つのものを描く道は 1 本
// (憲法 7: 「同じものを描くコードを 2 本書かない」)。

#include "ui/Path.h"
#include "ui/Types.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::ui {

// 矩形の各角の半径 (0 = 角張る): ほかに接しているパネルは、その角が四角。
struct Corners {
    float tl = 0, tr = 0, br = 0, bl = 0;
    static constexpr Corners All(float r) { return {r, r, r, r}; }
};

class Painter {
public:
    void Init(IDWriteFactory* dwrite, std::wstring fontFamily = L"Yu Gothic UI");
    // GPU のデバイスが作り直された: (古いデバイスで作った) ブラシとエフェクトを捨てる
    // (次の Begin / 使うときに作り直す)。文字のフォーマットは GPU の上にないので残る。
    void DeviceChanged();
    void Begin(ID2D1DeviceContext* dc);
    void End();
    ID2D1DeviceContext* DC() const { return dc_; }

    void Clear(Color c);
    void FillRect(const Rect& r, Color c);
    void FillRoundRect(const Rect& r, float radius, Color c);
    void StrokeRect(const Rect& r, Color c, float width = 1.f);
    void StrokeRoundRect(const Rect& r, float radius, Color c, float width = 1.f);
    void Line(float x1, float y1, float x2, float y2, Color c, float width = 1.f);
    void FillCircle(float cx, float cy, float radius, Color c);
    // 縦のグラデーション: 上の端が top、下の端が bottom の色。
    void FillRectGradient(const Rect& r, Color top, Color bottom);
    void FillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom);
    // 各角に別々の半径。上から下へ塗る、または、色が上から下へ変わる線で縁取る。
    void FillRoundRectGradient(const Rect& r, Corners radii, Color top, Color bottom);
    void StrokeRoundRectGradient(const Rect& r, Corners radii, Color top, Color bottom, float width = 1.f);
    // どんな形でも (ui/Path.h)、1 色で塗る。重なって同じ向きに回る図形は、
    // つながる (巻き数で塗る)。
    void FillPath(const Path& path, Color c);
    // 角丸の矩形のまわりのやわらかい影 (少しずつ広がる数層。箱より先に描く)。
    void Shadow(const Rect& r, float radius, Color c, float size);

    void Text(const Rect& r, std::wstring_view text, const TextStyle& style);
    float TextWidth(std::wstring_view text, float size, bool bold = false);
    // `width` で折り返したときの文字の高さ (Text が style.wrap で描くのと同じ)。
    float TextHeight(std::wstring_view text, float size, float width, bool bold = false);
    // text[index] の前に置いたキャレットの x のずれ。
    float CaretX(std::wstring_view text, size_t index, float size, bool bold = false);
    // x に一番近いキャレット位置の番号。
    size_t HitTest(std::wstring_view text, float x, float size, bool bold = false);

    void Image(ID2D1Bitmap* bmp, const Rect& dst, float opacity = 1.f);
    // 1 色の画像: 形 (アルファ) だけを残し、すべてのピクセルを `c` で塗る
    // (1 色のロゴを、背景に合わせて白や黒で出す)。
    void ImageTinted(ID2D1Bitmap* bmp, const Rect& dst, Color c);

    void PushClip(const Rect& r);
    void PopClip();

private:
    IDWriteTextFormat* Format(float size, bool bold);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> Layout(std::wstring_view text, float size, bool bold);
    ID2D1SolidColorBrush* Brush(Color c);
    ID2D1LinearGradientBrush* Gradient(Color top, Color bottom, float y0, float y1);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> RoundRectPath(const Rect& r, Corners radii);
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;  // ImageTinted (デバイスコンテキストごとに 1 回作る)
    ID2D1DeviceContext* tintOwner_ = nullptr;

    IDWriteFactory* dwrite_ = nullptr;
    std::wstring family_;
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1DeviceContext* brushOwner_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    // 色の組ごとにグラデーションのブラシを 1 つ (1 回作り、矩形ごとに動かす)。
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>> gradients_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<IDWriteInlineObject>> ellipsis_;
    int clipDepth_ = 0;
};

}  // namespace rm::ui
