#pragma once
// Drawing for the core UI on top of Direct2D + DirectWrite. All coordinates are DIPs
// (the device context's DPI is set by the window, so 1 unit = 1/96 inch).
// This is the only file of the UI that touches Direct2D: one way to draw each thing
// (constitution 7: "do not write two pieces of code that draw the same thing").

#include "ui/Types.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::ui {

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

    void Text(const Rect& r, std::wstring_view text, const TextStyle& style);
    float TextWidth(std::wstring_view text, float size, bool bold = false);
    // x offset of the caret placed before text[index].
    float CaretX(std::wstring_view text, size_t index, float size, bool bold = false);
    // Index of the caret position closest to x.
    size_t HitTest(std::wstring_view text, float x, float size, bool bold = false);

    void Image(ID2D1Bitmap* bmp, const Rect& dst, float opacity = 1.f);

    void PushClip(const Rect& r);
    void PopClip();

private:
    IDWriteTextFormat* Format(float size, bool bold);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> Layout(std::wstring_view text, float size, bool bold);
    ID2D1SolidColorBrush* Brush(Color c);

    IDWriteFactory* dwrite_ = nullptr;
    std::wstring family_;
    ID2D1DeviceContext* dc_ = nullptr;
    ID2D1DeviceContext* brushOwner_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<IDWriteTextFormat>> formats_;
    std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<IDWriteInlineObject>> ellipsis_;
    int clipDepth_ = 0;
};

}  // namespace rm::ui
