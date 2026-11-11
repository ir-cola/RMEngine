#pragma once
// Drawing for the core UI on top of Direct2D + DirectWrite. All coordinates are DIPs
// (the device context's DPI is set by the window, so 1 unit = 1/96 inch).
// This is the only file of the UI that touches Direct2D: one way to draw each thing
// (constitution 7: "do not write two pieces of code that draw the same thing").

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

// The radius of each corner of a rectangle (0 = square): a panel touching another is square there.
struct Corners {
    float tl = 0, tr = 0, br = 0, bl = 0;
    static constexpr Corners All(float r) { return {r, r, r, r}; }
};

class Painter {
public:
    void Init(IDWriteFactory* dwrite, std::wstring fontFamily = L"Yu Gothic UI");
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
    // Vertical gradients: 	op at the top edge, ottom at the bottom edge.
    void FillRectGradient(const Rect& r, Color top, Color bottom);
    void FillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom);
    // Each corner its own radius; filled top-to-bottom, or outlined with a line whose color runs top-to-bottom.
    void FillRoundRectGradient(const Rect& r, Corners radii, Color top, Color bottom);
    void StrokeRoundRectGradient(const Rect& r, Corners radii, Color top, Color bottom, float width = 1.f);
    // Any shape (ui/Path.h), filled with one color. Figures that overlap and go round the same way
    // join (filled by winding).
    void FillPath(const Path& path, Color c);
    // A soft shadow around a rounded rectangle (a few widening layers; drawn before the box).
    void Shadow(const Rect& r, float radius, Color c, float size);

    void Text(const Rect& r, std::wstring_view text, const TextStyle& style);
    float TextWidth(std::wstring_view text, float size, bool bold = false);
    // How tall the text is when wrapped to `width` (as Text draws it with style.wrap).
    float TextHeight(std::wstring_view text, float size, float width, bool bold = false);
    // x offset of the caret placed before text[index].
    float CaretX(std::wstring_view text, size_t index, float size, bool bold = false);
    // Index of the caret position closest to x.
    size_t HitTest(std::wstring_view text, float x, float size, bool bold = false);

    void Image(ID2D1Bitmap* bmp, const Rect& dst, float opacity = 1.f);
    // A picture in one color: only its shape (its alpha) is kept, every pixel painted `c`
    // (a one-color logo shown white or black to suit the background).
    void ImageTinted(ID2D1Bitmap* bmp, const Rect& dst, Color c);

    void PushClip(const Rect& r);
    void PopClip();

private:
    IDWriteTextFormat* Format(float size, bool bold);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> Layout(std::wstring_view text, float size, bool bold);
    ID2D1SolidColorBrush* Brush(Color c);
    ID2D1LinearGradientBrush* Gradient(Color top, Color bottom, float y0, float y1);
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> RoundRectPath(const Rect& r, Corners radii);
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;  // ImageTinted (made once per device context)
    ID2D1DeviceContext* tintOwner_ = nullptr;

    IDWriteFactory* dwrite_ = nullptr;
    std::wstring family_;
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1DeviceContext* brushOwner_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    // One gradient brush per pair of colors (made once, moved to each rectangle).
    std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush>> gradients_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<IDWriteInlineObject>> ellipsis_;
    int clipDepth_ = 0;
};

}  // namespace rm::ui
