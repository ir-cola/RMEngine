#include "ui/Painter.h"

#include <algorithm>
#include <cstring>

#include <d2d1effects.h>

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace rm::ui {

namespace {
D2D1_RECT_F ToD2D(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }
D2D1_COLOR_F ToD2D(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
// 両方の色の各成分 8 ビット: 64 ビット。キーが等しければ、(見える) 色も等しい。
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

void Painter::DeviceChanged() {
    brush_.Reset();
    gradients_.clear();
    grains_.clear();
    brushOwner_ = nullptr;
    tint_.Reset();
    tintOwner_ = nullptr;
}

void Painter::Begin(ID2D1DeviceContext* dc) {
    dc_ = dc;
    clips_.clear();
    if (brushOwner_ != dc) {
        brush_.Reset();
        gradients_.clear();  // ブラシはデバイスコンテキストに属する
        grains_.clear();
        dc_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_);
        brushOwner_ = dc;
    }
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
}

void Painter::End() {
    while (!clips_.empty()) PopClip();
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
        if (gradients_.size() > 512) gradients_.clear();  // 際限なく増えない
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
    // 半径は一辺の半分より大きくしない。
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
    // 線は矩形の内側に入る (その中心が線の幅の半分だけ内側)。角もそれに合わせて縮む。
    const float h = width * 0.5f;
    const Rect in{r.x + h, r.y + h, r.w - width, r.h - width};
    if (in.w <= 0 || in.h <= 0) return;
    auto shrink = [&](float k) { return std::fmax(0.f, k - h); };
    auto path = RoundRectPath(in, {shrink(radii.tl), shrink(radii.tr), shrink(radii.br), shrink(radii.bl)});
    ID2D1Brush* b = Gradient(top, bottom, r.y, r.Bottom());
    if (path && b) dc_->DrawGeometry(path.Get(), b, width);
}

void Painter::FillGrain(const Rect& r, float radius, const GrainLook& look) {
    if (r.w <= 0 || r.h <= 0) return;
    // 見た目の指紋 (色・割合・種)。
    uint64_t key = 1469598103934665603ull;
    auto mix = [&](float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        key = (key ^ bits) * 1099511628211ull;
    };
    for (const Color& c : {look.light, look.dark}) mix(c.r), mix(c.g), mix(c.b), mix(c.a);
    mix(look.lightShare), mix(look.darkShare), mix(look.clumps), mix(static_cast<float>(look.seed));
    auto it = grains_.find(key);
    if (it == grains_.end()) {
        if (grains_.size() > 16) grains_.clear();  // 際限なく増えない
        constexpr int kTile = 128;  // DIP (96 DPI の絵として作る: 1 ピクセル = 1 DIP)
        const std::vector<uint32_t> pixels = GrainPixels(kTile, look);
        ComPtr<ID2D1Bitmap> bmp;
        ComPtr<ID2D1BitmapBrush> brush;
        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(dc_->CreateBitmap(D2D1::SizeU(kTile, kTile), pixels.data(), kTile * 4, props, &bmp)) ||
            FAILED(dc_->CreateBitmapBrush(bmp.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP), &brush))) {
            grains_[key] = nullptr;  // (作れない: 何度も試さない)
            return;
        }
        it = grains_.emplace(key, brush).first;
    }
    if (!it->second) return;
    const D2D1_RECT_F rect = D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
    if (radius > 0) {
        dc_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), it->second.Get());
    } else {
        dc_->FillRectangle(rect, it->second.Get());
    }
}

void Painter::FillPath(const Path& path, Color c) {
    if (path.steps.empty()) return;
    ComPtr<ID2D1Factory> factory;
    dc_->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) return;
    // 重なる図形は 1 つの形につながる (同じ向きに回っているとき)。穴は決してあけない。
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
    // 4 層。それぞれ少しずつ広く、低く、薄く: やわらかいぼかしに見えて、塗りは 4 回で済む。
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

ComPtr<IDWriteTextLayout> Painter::RichLayout(std::wstring_view text, float size, float width, std::span<const TextRun> runs,
                                              std::vector<ComPtr<ID2D1SolidColorBrush>>* brushes) {
    ComPtr<IDWriteTextLayout> layout;
    IDWriteTextFormat* fmt = Format(size, false);
    if (!fmt || width <= 0) return layout;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DWRITE_TRIMMING none{DWRITE_TRIMMING_GRANULARITY_NONE, 0, 0};
    fmt->SetTrimming(&none, nullptr);
    if (FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), fmt, width, 100000.f, &layout))) return nullptr;
    for (const TextRun& run : runs) {
        if (run.length == 0 || run.start >= text.size()) continue;
        const DWRITE_TEXT_RANGE range{run.start, run.length};
        if (run.bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, range);
        if (run.mono) layout->SetFontFamilyName(L"Consolas", range);  // (日本語は DirectWrite が別の書体で補う)
        if (run.underline) layout->SetUnderline(TRUE, range);
        if (run.color && brushes && dc_) {
            ComPtr<ID2D1SolidColorBrush> b;
            if (SUCCEEDED(dc_->CreateSolidColorBrush(ToD2D(*run.color), &b))) {
                layout->SetDrawingEffect(b.Get(), range);
                brushes->push_back(b);
            }
        }
    }
    return layout;
}

float Painter::RichText(const Rect& r, std::wstring_view text, float size, Color color, std::span<const TextRun> runs, bool draw) {
    if (text.empty() || r.w <= 0) return 0.f;
    std::vector<ComPtr<ID2D1SolidColorBrush>> brushes;
    auto layout = RichLayout(text, size, r.w, runs, draw ? &brushes : nullptr);
    if (!layout) return 0.f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    if (draw) dc_->DrawTextLayout({r.x, r.y}, layout.Get(), Brush(color), D2D1_DRAW_TEXT_OPTIONS_NONE);
    return m.height;
}

std::optional<size_t> Painter::RichHitTest(const Rect& r, std::wstring_view text, float size, std::span<const TextRun> runs, float x, float y) {
    if (text.empty() || r.w <= 0) return std::nullopt;
    auto layout = RichLayout(text, size, r.w, runs, nullptr);
    if (!layout) return std::nullopt;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm{};
    if (FAILED(layout->HitTestPoint(x - r.x, y - r.y, &trailing, &inside, &hm)) || !inside) return std::nullopt;
    return static_cast<size_t>(hm.textPosition);
}

float Painter::TextWidth(std::wstring_view text, float size, bool bold) {
    if (text.empty()) return 0.f;
    auto layout = Layout(text, size, bold);
    if (!layout) return 0.f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Painter::TextHeight(std::wstring_view text, float size, float width, bool bold) {
    if (text.empty() || width <= 0) return 0.f;
    auto layout = Layout(text, size, bold);
    if (!layout) return 0.f;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    layout->SetMaxWidth(width);
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.height;
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
    // 行: 入力の r, g, b, a と 1。列: 出力の r, g, b, a (ストレートアルファ - エフェクトは、
    // 先に乗算済みの色をほどく)。色は固定で、アルファは残る。
    const D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(0, 0, 0, 0,  //
                                                 0, 0, 0, 0,  //
                                                 0, 0, 0, 0,  //
                                                 0, 0, 0, c.a,  //
                                                 c.r, c.g, c.b, 0);
    tint_->SetInput(0, bmp);
    tint_->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
    // 変形で拡大縮小されるので、Image() と同じようにきれいに補間される。
    // 大きさは効果の出力をこの画面で測って合わせる: 効果は画素で動き、出力の 1 画素が画面の DPI で
    // DIP に直される。bmp->GetSize() (ビットマップ自身の DPI) で割ると、125 % の画面で 0.8 倍に縮み、
    // 左上に寄った (2026-11-24)。
    Microsoft::WRL::ComPtr<ID2D1Image> out;
    tint_->GetOutput(&out);
    D2D1_RECT_F bounds{};
    if (!out || FAILED(dc_->GetImageLocalBounds(out.Get(), &bounds))) return;
    const float bw = bounds.right - bounds.left, bh = bounds.bottom - bounds.top;
    if (bw <= 0 || bh <= 0) return;
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(D2D1::Matrix3x2F::Translation(-bounds.left, -bounds.top) * D2D1::Matrix3x2F::Scale(dst.w / bw, dst.h / bh) *
                      D2D1::Matrix3x2F::Translation(dst.x, dst.y) * old);
    dc_->DrawImage(tint_.Get(), D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    dc_->SetTransform(old);
}

void Painter::PushClip(const Rect& r) {
    dc_->PushAxisAlignedClip(ToD2D(r), D2D1_ANTIALIAS_MODE_ALIASED);
    clips_.push_back(false);
}

void Painter::PushClipExcept(const Rect& r, std::span<const Rect> holes) {
    // 穴が無い: ふつうの四角の切り取り (速い)。
    const auto inside = [&](const Rect& h) { return h.x < r.x + r.w && h.x + h.w > r.x && h.y < r.y + r.h && h.y + h.h > r.y; };
    if (std::none_of(holes.begin(), holes.end(), inside)) {
        PushClip(r);
        return;
    }
    // 外の四角と穴の四角を 1 つの形にし、交互の塗り (ALTERNATE) で穴をあけて、それをマスクにしたレイヤー。
    ComPtr<ID2D1Factory> factory;
    dc_->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> geo;
    ComPtr<ID2D1GeometrySink> sink;
    if (!factory || FAILED(factory->CreatePathGeometry(&geo)) || FAILED(geo->Open(&sink))) {
        PushClip(r);
        return;
    }
    sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
    auto box = [&](float x0, float y0, float x1, float y1) {
        sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
        const D2D1_POINT_2F pts[3] = {D2D1::Point2F(x1, y0), D2D1::Point2F(x1, y1), D2D1::Point2F(x0, y1)};
        sink->AddLines(pts, 3);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    };
    box(r.x, r.y, r.x + r.w, r.y + r.h);
    for (const Rect& h : holes) {
        if (!inside(h)) continue;
        box(std::max(h.x, r.x), std::max(h.y, r.y), std::min(h.x + h.w, r.x + r.w), std::min(h.y + h.h, r.y + r.h));
    }
    if (FAILED(sink->Close())) {
        PushClip(r);
        return;
    }
    // 窓の面はアルファを持たない (D2D1_ALPHA_MODE_IGNORE) ので、レイヤーは今の絵から始める (INITIALIZE_FROM_BACKGROUND)。
    // 付けないとレイヤーは透明な黒から始まり、半透明の物が黒くなった (2026-11-26: 夏の夜の海が真っ黒)。
    dc_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), geo.Get(), D2D1_ANTIALIAS_MODE_ALIASED, D2D1::IdentityMatrix(), 1.f, nullptr,
                                          D2D1_LAYER_OPTIONS1_INITIALIZE_FROM_BACKGROUND | D2D1_LAYER_OPTIONS1_IGNORE_ALPHA),
                   nullptr);
    clips_.push_back(true);
}

void Painter::PopClip() {
    if (clips_.empty()) return;
    if (clips_.back()) {
        dc_->PopLayer();
    } else {
        dc_->PopAxisAlignedClip();
    }
    clips_.pop_back();
}

}  // namespace rm::ui
