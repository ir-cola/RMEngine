#include "rm_engine/space2d/runtime/Renderer2D.h"

#include <algorithm>

namespace rm::space2d {

namespace {

D2D1_COLOR_F ToD2D(Color c) { return {c.r, c.g, c.b, c.a}; }

// こちらの行列 (列ベクトル) を Direct2D の行列 (行ベクトル: p' = p * M) にしたもの。
D2D1_MATRIX_3X2_F ToD2D(const Mat3& m) { return D2D1::Matrix3x2F(m.a, m.b, m.c, m.d, m.tx, m.ty); }

bool IsWhite(Color c) { return c.r == 1.f && c.g == 1.f && c.b == 1.f; }

}  // namespace

ID2D1SolidColorBrush* Renderer2D::Brush(ID2D1DeviceContext* dc, Color c) {
    if (owner_ != dc) {
        brush_.Reset();
        tint_.Reset();
        owner_ = dc;
    }
    if (!brush_ && FAILED(dc->CreateSolidColorBrush(ToD2D(c), &brush_))) return nullptr;
    brush_->SetColor(ToD2D(c));
    return brush_.Get();
}

void Renderer2D::DrawLines(ID2D1DeviceContext* dc, const RenderFrame2D& f, std::span<const Line2D> lines) {
    for (const Line2D& l : lines) {
        const Vec2 a = ToScreen(f.camera, f.viewport, l.a), b = ToScreen(f.camera, f.viewport, l.b);
        if (ID2D1SolidColorBrush* br = Brush(dc, l.color)) dc->DrawLine({a.x, a.y}, {b.x, b.y}, br, l.width);
    }
}

void Renderer2D::DrawBatched(ID2D1DeviceContext* dc, const RenderFrame2D& f, std::span<const Line2D> lines) {
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    dc->GetFactory(&factory);
    if (!factory) return DrawLines(dc, f, lines);
    for (size_t first = 0; first < lines.size();) {
        size_t end = first + 1;
        while (end < lines.size() && lines[end].color == lines[first].color && lines[end].width == lines[first].width) ++end;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        ID2D1SolidColorBrush* br = Brush(dc, lines[first].color);
        if (br && SUCCEEDED(factory->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
            for (size_t k = first; k < end; ++k) {
                const Vec2 a = ToScreen(f.camera, f.viewport, lines[k].a), b = ToScreen(f.camera, f.viewport, lines[k].b);
                sink->BeginFigure({a.x, a.y}, D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddLine({b.x, b.y});
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            }
            if (SUCCEEDED(sink->Close())) dc->DrawGeometry(path.Get(), br, lines[first].width);
        }
        first = end;
    }
}

void Renderer2D::Render(ID2D1DeviceContext* dc, const RenderFrame2D& f) {
    if (!dc || f.viewport.w <= 0 || f.viewport.h <= 0) return;
    D2D1_MATRIX_3X2_F old;
    dc->GetTransform(&old);
    if (f.clear.a <= 0) {
        // 別の絵の上に重ねる層: 塗るものはない
    } else if (ID2D1SolidColorBrush* br = Brush(dc, f.clear)) {
        dc->FillRectangle(D2D1::RectF(f.viewport.x, f.viewport.y, f.viewport.x + f.viewport.w, f.viewport.y + f.viewport.h), br);
    }
    DrawLines(dc, f, f.lines);

    const Mat3 screen = ScreenMatrix(f.camera, f.viewport);
    for (const SpriteDraw& s : f.sprites) {
        const float fx = s.flipX ? -1.f : 1.f, fy = s.flipY ? -1.f : 1.f;
        // 描くもの (画像の DIP のままか、単位の正方形) -> スプライト自身の空間:
        // 中心が原点、Y が上、幅 `size` 単位 (反転しているときは裏返す)。
        // 画像の一部 (アトラスのセル) は、画像の DIP の (0, 0) に描く (ピクセルは 1 インチ 96 でないこともある)。
        float w = 1, h = 1;
        D2D1_RECT_F source{};
        if (s.image) {
            const D2D1_SIZE_F dip = s.image->GetSize();
            const D2D1_SIZE_U px = s.image->GetPixelSize();
            const float kx = px.width > 0 ? dip.width / static_cast<float>(px.width) : 1.f;
            const float ky = px.height > 0 ? dip.height / static_cast<float>(px.height) : 1.f;
            const PixelRect part = s.part.w > 0 && s.part.h > 0 ? s.part : PixelRect{0, 0, px.width, px.height};
            source = D2D1::RectF(static_cast<float>(part.x) * kx, static_cast<float>(part.y) * ky, static_cast<float>(part.x + part.w) * kx,
                                 static_cast<float>(part.y + part.h) * ky);
            w = std::max(source.right - source.left, 1e-3f);
            h = std::max(source.bottom - source.top, 1e-3f);
        }
        const Mat3 own{s.size.x * fx / w, 0, 0, -s.size.y * fy / h, -0.5f * s.size.x * fx, 0.5f * s.size.y * fy};
        dc->SetTransform(ToD2D(screen * s.world * own) * old);
        const D2D1_RECT_F rect = D2D1::RectF(0, 0, w, h);
        if (!s.image) {
            if (ID2D1SolidColorBrush* br = Brush(dc, s.color)) dc->FillRectangle(rect, br);
            continue;
        }
        const D2D1_INTERPOLATION_MODE mode = s.crisp ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
        if (IsWhite(s.color)) {
            dc->DrawBitmap(s.image, rect, s.color.a, mode, &source, nullptr);
            continue;
        }
        Brush(dc, s.color);  // (tint_ がこのコンテキストのものであることを確かめる)
        if (!tint_ && FAILED(dc->CreateEffect(CLSID_D2D1ColorMatrix, &tint_))) tint_.Reset();
        if (!tint_) {
            dc->DrawBitmap(s.image, rect, s.color.a, mode, &source, nullptr);
            continue;
        }
        // 行: 入力の r, g, b, a と 1。列: 出力。各色に色味をかける。
        const D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(s.color.r, 0, 0, 0,  //
                                                     0, s.color.g, 0, 0,  //
                                                     0, 0, s.color.b, 0,  //
                                                     0, 0, 0, s.color.a,  //
                                                     0, 0, 0, 0);
        tint_->SetInput(0, s.image);
        tint_->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
        Microsoft::WRL::ComPtr<ID2D1Image> out;
        tint_->GetOutput(&out);
        const D2D1_POINT_2F at{0, 0};  // 部分の左上が (0, 0) に来る
        dc->DrawImage(out.Get(), &at, &source, mode);
        tint_->SetInput(0, nullptr);
    }
    dc->SetTransform(old);
    DrawLines(dc, f, f.overlay);
    DrawBatched(dc, f, f.debug);
}

}  // namespace rm::space2d
