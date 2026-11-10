#include "ui/Painter.h"

#include <d2d1effects.h>

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace rm::ui {

namespace {
D2D1_RECT_F ToD2D(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }
D2D1_COLOR_F ToD2D(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
// 8 bits per component of both colors: 64 bits, so equal keys mean equal (shown) colors.
uint64_t GradientKey(Color a, Color b) {
    auto q = [](float v) { return static_cast<uint64_t>(std::lround(std::fmin(std::fmax(v, 0.f), 1.f) * 255.f)); };
    return q(a.r) | q(a.g) << 8 | q(a.b) << 16 | q(a.a) << 24 | q(b.r) << 32 | q(b.g) << 40 | q(b.b) << 48 | q(b.a) << 56;
}
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
        gradients_.clear();  // brushes belong to a device context
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

ID2D1LinearGradientBrush* Painter::Gradient(Color top, Color bottom, float y0, float y1) {
    const uint64_t key = GradientKey(top, bottom);
    auto it = gradients_.find(key);
    if (it == gradients_.end()) {
        if (gradients_.size() > 512) gradients_.clear();  // never grows without bound
        const D2D1_GRADIENT_STOP stops[2] = {{0.f, ToD2D(top)}, {1.f, ToD2D(bottom)}};
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1LinearGradientBrush> brush;
        if (FAILED(dc_->CreateGradientStopCollection(stops, 2, &collection)) ||
            FAILED(dc_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)),
                                                  collection.Get(), &brush))) {
            return nullptr;
        }
        it = gradients_.emplace(key, brush).first;
    }
    it->second->SetStartPoint(D2D1::Point2F(0, y0));
    it->second->SetEndPoint(D2D1::Point2F(0, y1));
    return it->second.Get();
}

void Painter::FillRectGradient(const Rect& r, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    if (ID2D1Brush* b = Gradient(top, bottom, r.y, r.Bottom())) {
        dc_->FillRectangle(ToD2D(r), b);
    } else {
        FillRect(r, top);
    }
}

void Painter::FillRoundRectGradient(const Rect& r, float radius, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    if (ID2D1Brush* b = Gradient(top, bottom, r.y, r.Bottom())) {
        dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(r), radius, radius), b);
    } else {
        FillRoundRect(r, radius, top);
    }
}

ComPtr<ID2D1PathGeometry> Painter::RoundRectPath(const Rect& r, Corners k) {
    ComPtr<ID2D1Factory> factory;
    dc_->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return nullptr;
    // No radius larger than half a side.
    const float lim = std::fmin(r.w, r.h) * 0.5f;
    k = {std::fmin(k.tl, lim), std::fmin(k.tr, lim), std::fmin(k.br, lim), std::fmin(k.bl, lim)};
    const float x0 = r.x, y0 = r.y, x1 = r.Right(), y1 = r.Bottom();
    auto arc = [&](float x, float y, float radius) {
        if (radius > 0)
            sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(radius, radius), 0.f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
    };
    sink->BeginFigure(D2D1::Point2F(x0 + k.tl, y0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(x1 - k.tr, y0));
    arc(x1, y0 + k.tr, k.tr);
    sink->AddLine(D2D1::Point2F(x1, y1 - k.br));
    arc(x1 - k.br, y1, k.br);
    sink->AddLine(D2D1::Point2F(x0 + k.bl, y1));
    arc(x0, y1 - k.bl, k.bl);
    sink->AddLine(D2D1::Point2F(x0, y0 + k.tl));
    arc(x0 + k.tl, y0, k.tl);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return nullptr;
    return path;
}

void Painter::FillRoundRectGradient(const Rect& r, Corners radii, Color top, Color bottom) {
    if (r.w <= 0 || r.h <= 0) return;
    auto path = RoundRectPath(r, radii);
    ID2D1Brush* b = Gradient(top, bottom, r.y, r.Bottom());
    if (path && b) dc_->FillGeometry(path.Get(), b);
}

void Painter::StrokeRoundRectGradient(const Rect& r, Corners radii, Color top, Color bottom, float width) {
    // The line sits inside the rectangle (its centre half a width in), the corners shrink with it.
    const float h = width * 0.5f;
    const Rect in{r.x + h, r.y + h, r.w - width, r.h - width};
    if (in.w <= 0 || in.h <= 0) return;
    auto shrink = [&](float k) { return std::fmax(0.f, k - h); };
    auto path = RoundRectPath(in, {shrink(radii.tl), shrink(radii.tr), shrink(radii.br), shrink(radii.bl)});
    ID2D1Brush* b = Gradient(top, bottom, r.y, r.Bottom());
    if (path && b) dc_->DrawGeometry(path.Get(), b, width);
}

void Painter::FillPath(const Path& path, Color c) {
    if (path.steps.empty()) return;
    ComPtr<ID2D1Factory> factory;
    dc_->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return;
    // Figures that overlap join into one shape (when they go round the same way), never cut holes.
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    auto pt = [](Vec2 v) { return D2D1::Point2F(v.x, v.y); };
    bool open = false;
    for (const Path::Step& s : path.steps) {
        switch (s.op) {
            case Path::Op::Begin:
                if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->BeginFigure(pt(s.a), D2D1_FIGURE_BEGIN_FILLED);
                open = true;
                break;
            case Path::Op::Line:
                if (open) sink->AddLine(pt(s.a));
                break;
            case Path::Op::Cubic:
                if (open) sink->AddBezier(D2D1::BezierSegment(pt(s.a), pt(s.b), pt(s.c)));
                break;
        }
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    if (ID2D1Brush* b = Brush(c)) dc_->FillGeometry(geo.Get(), b);
}

void Painter::Shadow(const Rect& r, float radius, Color c, float size) {
    // Four layers, each a little wider, lower and fainter: reads as a soft blur, costs four fills.
    constexpr float kAlpha[4] = {0.40f, 0.24f, 0.12f, 0.05f};
    for (int i = 0; i < 4; ++i) {
        const float s = size * static_cast<float>(i + 1) / 4.f;
        const Rect layer{r.x - s * 0.5f, r.y + s * 0.35f - s * 0.25f, r.w + s, r.h + s * 0.6f};
        FillRoundRect(layer, radius + s * 0.5f, c.Alpha(c.a * kAlpha[i]));
    }
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

void Painter::ImageTinted(ID2D1Bitmap* bmp, const Rect& dst, Color c) {
    if (!bmp || dst.w <= 0 || dst.h <= 0) return;
    if (tintOwner_ != dc_) {
        tint_.Reset();
        if (FAILED(dc_->CreateEffect(CLSID_D2D1ColorMatrix, &tint_))) tint_.Reset();
        tintOwner_ = dc_;
    }
    if (!tint_) return;
    // Rows: the input's r, g, b, a and 1; columns: the output's r, g, b, a (straight alpha - the
    // effect takes premultiplied colors apart first). The color is fixed, the alpha is kept.
    const D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(0, 0, 0, 0,  //
                                                 0, 0, 0, 0,  //
                                                 0, 0, 0, 0,  //
                                                 0, 0, 0, c.a,  //
                                                 c.r, c.g, c.b, 0);
    tint_->SetInput(0, bmp);
    tint_->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
    // Scaled by the transform, so the picture is smoothed as nicely as Image() does.
    const D2D1_SIZE_F size = bmp->GetSize();
    if (size.width <= 0 || size.height <= 0) return;
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(D2D1::Matrix3x2F::Scale(dst.w / size.width, dst.h / size.height) * D2D1::Matrix3x2F::Translation(dst.x, dst.y) * old);
    dc_->DrawImage(tint_.Get(), D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    dc_->SetTransform(old);
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
