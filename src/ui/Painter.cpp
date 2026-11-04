#include "ui/Painter.h"

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace rm::ui {

namespace {
D2D1_RECT_F ToD2D(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }
D2D1_COLOR_F ToD2D(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
uint32_t FormatKey(float size, bool bold) { return (static_cast<uint32_t>(std::lround(size * 4.f)) << 1) | (bold ? 1u : 0u); }
}  // namespace

void Painter::Init(IDWriteFactory* dwrite, std::wstring fontFamily) {
    dwrite_ = dwrite;
    family_ = std::move(fontFamily);
    formats_.clear();
    ellipsis_.clear();
}

void Painter::Begin(ID2D1DeviceContext* dc) {
    dc_ = dc;
    clipDepth_ = 0;
    if (brushOwner_ != dc) {
        brush_.Reset();
        dc_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_);
        brushOwner_ = dc;
    }
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

void Painter::End() {
    while (clipDepth_ > 0) PopClip();
    dc_ = nullptr;
}

ID2D1SolidColorBrush* Painter::Brush(Color c) {
    brush_->SetColor(ToD2D(c));
    return brush_.Get();
}

void Painter::Clear(Color c) { dc_->Clear(ToD2D(c)); }

void Painter::FillRect(const Rect& r, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    dc_->FillRectangle(ToD2D(r), Brush(c));
}

void Painter::FillRoundRect(const Rect& r, float radius, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(r), radius, radius), Brush(c));
}

void Painter::StrokeRect(const Rect& r, Color c, float width) {
    const float h = width * 0.5f;
    dc_->DrawRectangle(D2D1::RectF(r.x + h, r.y + h, r.Right() - h, r.Bottom() - h), Brush(c), width);
}

void Painter::StrokeRoundRect(const Rect& r, float radius, Color c, float width) {
    const float h = width * 0.5f;
    dc_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.x + h, r.y + h, r.Right() - h, r.Bottom() - h), radius, radius),
                              Brush(c), width);
}

void Painter::Line(float x1, float y1, float x2, float y2, Color c, float width) {
    dc_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), Brush(c), width);
}

void Painter::FillCircle(float cx, float cy, float radius, Color c) {
    dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), Brush(c));
}

IDWriteTextFormat* Painter::Format(float size, bool bold) {
    const uint32_t key = FormatKey(size, bold);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(family_.c_str(), nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ja-jp", &fmt);
    ComPtr<IDWriteInlineObject> sign;
    if (fmt) dwrite_->CreateEllipsisTrimmingSign(fmt.Get(), &sign);
    ellipsis_[key] = sign;
    formats_[key] = fmt;
    return fmt.Get();
}

ComPtr<IDWriteTextLayout> Painter::Layout(std::wstring_view text, float size, bool bold) {
    ComPtr<IDWriteTextLayout> layout;
    IDWriteTextFormat* fmt = Format(size, bold);
    if (!fmt) return layout;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    fmt->SetTrimming(&none, nullptr);
    dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), fmt, 100000.f, size * 2.f, &layout);
    return layout;
}

void Painter::Text(const Rect& r, std::wstring_view text, const TextStyle& st) {
    if (text.empty() || r.w <= 0 || r.h <= 0) return;
    IDWriteTextFormat* fmt = Format(st.size, st.bold);
    if (!fmt) return;
    fmt->SetWordWrapping(st.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    fmt->SetTextAlignment(st.h == HAlign::Left     ? DWRITE_TEXT_ALIGNMENT_LEADING
                          : st.h == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                                                   : DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmt->SetParagraphAlignment(st.v == VAlign::Top      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                               : st.v == VAlign::Center ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                        : DWRITE_PARAGRAPH_ALIGNMENT_FAR);
    if (st.ellipsis && !st.wrap) {
        DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        fmt->SetTrimming(&trim, ellipsis_[FormatKey(st.size, st.bold)].Get());
    } else {
        DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
        fmt->SetTrimming(&none, nullptr);
    }
    dc_->DrawText(text.data(), static_cast<UINT32>(text.size()), fmt, ToD2D(r), Brush(st.color),
                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Painter::TextWidth(std::wstring_view text, float size, bool bold) {
    if (text.empty()) return 0.f;
    auto layout = Layout(text, size, bold);
    if (!layout) return 0.f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Painter::CaretX(std::wstring_view text, size_t index, float size, bool bold) {
    if (index == 0 || text.empty()) return 0.f;
    if (index > text.size()) index = text.size();
    auto layout = Layout(text, size, bold);
    if (!layout) return 0.f;
    float x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS hm{};
    layout->HitTestTextPosition(static_cast<UINT32>(index - 1), TRUE, &x, &y, &hm);
    return x;
}

size_t Painter::HitTest(std::wstring_view text, float x, float size, bool bold) {
    if (text.empty() || x <= 0) return 0;
    auto layout = Layout(text, size, bold);
    if (!layout) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm{};
    layout->HitTestPoint(x, size * 0.5f, &trailing, &inside, &hm);
    size_t pos = hm.textPosition + (trailing ? hm.length : 0);
    return pos > text.size() ? text.size() : pos;
}

void Painter::Image(ID2D1Bitmap* bmp, const Rect& dst, float opacity) {
    if (!bmp) return;
    dc_->DrawBitmap(bmp, ToD2D(dst), opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr, nullptr);
}

void Painter::PushClip(const Rect& r) {
    dc_->PushAxisAlignedClip(ToD2D(r), D2D1_ANTIALIAS_MODE_ALIASED);
    ++clipDepth_;
}

void Painter::PopClip() {
    if (clipDepth_ <= 0) return;
    dc_->PopAxisAlignedClip();
    --clipDepth_;
}

}  // namespace rm::ui
