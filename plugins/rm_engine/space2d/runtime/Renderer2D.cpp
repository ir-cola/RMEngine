#include "rm_engine/space2d/runtime/Renderer2D.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

namespace rm::space2d {

namespace {

D2D1_COLOR_F ToD2D(Color c) { return {c.r, c.g, c.b, c.a}; }

// Our matrix (column vectors) as Direct2D's (row vectors: p' = p * M).
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

void Renderer2D::Render(ID2D1DeviceContext* dc, const RenderFrame2D& f) {
    if (!dc || f.viewport.w <= 0 || f.viewport.h <= 0) return;
    D2D1_MATRIX_3X2_F old;
    dc->GetTransform(&old);
    if (ID2D1SolidColorBrush* br = Brush(dc, f.clear)) {
        dc->FillRectangle(D2D1::RectF(f.viewport.x, f.viewport.y, f.viewport.x + f.viewport.w, f.viewport.y + f.viewport.h), br);
    }
    DrawLines(dc, f, f.lines);

    const Mat3 screen = ScreenMatrix(f.camera, f.viewport);
    for (const SpriteDraw& s : f.sprites) {
        const float fx = s.flipX ? -1.f : 1.f, fy = s.flipY ? -1.f : 1.f;
        // What is drawn (the image in its own DIPs, or the unit square) -> the sprite's own space:
        // its middle at the origin, Y up, `size` units across (turned over when flipped).
        float w = 1, h = 1;
        if (s.image) {
            const D2D1_SIZE_F dip = s.image->GetSize();
            w = std::max(dip.width, 1e-3f);
            h = std::max(dip.height, 1e-3f);
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
            dc->DrawBitmap(s.image, rect, s.color.a, mode, nullptr, nullptr);
            continue;
        }
        Brush(dc, s.color);  // (makes sure tint_ belongs to this context)
        if (!tint_ && FAILED(dc->CreateEffect(CLSID_D2D1ColorMatrix, &tint_))) tint_.Reset();
        if (!tint_) {
            dc->DrawBitmap(s.image, rect, s.color.a, mode, nullptr, nullptr);
            continue;
        }
        // Rows: the input's r, g, b, a and 1; columns: the output's. Each colour multiplied by the tint.
        const D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(s.color.r, 0, 0, 0,  //
                                                     0, s.color.g, 0, 0,  //
                                                     0, 0, s.color.b, 0,  //
                                                     0, 0, 0, s.color.a,  //
                                                     0, 0, 0, 0);
        tint_->SetInput(0, s.image);
        tint_->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
        dc->DrawImage(tint_.Get(), mode);
        tint_->SetInput(0, nullptr);
    }
    dc->SetTransform(old);
    DrawLines(dc, f, f.overlay);
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadImageFile(platform::Gfx& gfx, ID2D1DeviceContext* dc, const std::filesystem::path& file,
                                                  std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "ファイルを開けませんでした";
        return {};
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return platform::LoadBitmapFromMemory(gfx, dc, bytes, 0, error);
}

}  // namespace rm::space2d
