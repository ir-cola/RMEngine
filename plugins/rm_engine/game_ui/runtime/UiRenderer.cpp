#include "rm_engine/game_ui/runtime/UiRenderer.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/BakedImage.h"
#include "platform/Image.h"

#include <dwrite_3.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <string_view>

namespace rm::game_ui {

namespace {

D2D1_COLOR_F ToD2D(Color c) { return {c.r, c.g, c.b, c.a}; }
D2D1_MATRIX_3X2_F ToD2D(const Affine& m) { return D2D1::Matrix3x2F(m.a, m.b, m.c, m.d, m.tx, m.ty); }
D2D1_RECT_F ToD2D(const UiBox& b) { return D2D1::RectF(b.x, b.y, b.x + b.w, b.y + b.h); }
// Only its transparency changes the picture (drawn as it is, faded).
bool OnlyFades(const UiPaint& p) { return p.tint.r == 1.f && p.tint.g == 1.f && p.tint.b == 1.f && p.amount <= 0; }

}  // namespace

UiRenderer::~UiRenderer() {
    if (loaderFactory_ && fontLoader_) {
        Microsoft::WRL::ComPtr<IDWriteFontFileLoader> loader;
        if (SUCCEEDED(fontLoader_.As(&loader))) loaderFactory_->UnregisterFontFileLoader(loader.Get());
    }
}

void UiRenderer::Begin(ID2D1DeviceContext* dc) {
    dc_ = dc;
    if (owner_ != dc) {
        brush_.Reset();
        tint_.Reset();
        owner_ = dc;
    }
}

ID2D1SolidColorBrush* UiRenderer::Brush(Color c) {
    if (!brush_ && FAILED(dc_->CreateSolidColorBrush(ToD2D(c), &brush_))) return nullptr;
    brush_->SetColor(ToD2D(c));
    return brush_.Get();
}

void UiRenderer::Forget() {
    pictures_.clear();
    // Fonts are only looked at again: one made again for the same bytes would be one more copy that the
    // loader keeps for as long as the renderer lives.
    for (auto& [id, f] : fonts_) f.tried = false;
    formats_.clear();
}

Vec2 UiRenderer::ImageSize(const AssetRef& ref) const {
    const auto it = pictures_.find(ref.guid);
    return it == pictures_.end() ? Vec2{0, 0} : Vec2{static_cast<float>(it->second.width), static_cast<float>(it->second.height)};
}

void UiRenderer::Fill(ID2D1DeviceContext* dc, const UiBox& box, Color color) {
    Begin(dc);
    D2D1_MATRIX_3X2_F old;
    dc->GetTransform(&old);
    dc->SetTransform(D2D1::Matrix3x2F::Identity() * old);
    if (ID2D1SolidColorBrush* b = Brush(color)) dc->FillRectangle(ToD2D(box), b);
    dc->SetTransform(old);
}

const UiRenderer::Picture* UiRenderer::PictureOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset) {
    if (ref.Empty()) return nullptr;
    Picture& p = pictures_[ref.guid];
    if (!p.tried) {
        p.tried = true;
        std::string why;
        const auto bytes = asset ? asset(ref) : std::nullopt;
        const auto baked = bytes ? asset::ReadBakedImage(*bytes, &why) : std::nullopt;
        if (baked) {
            const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(baked->picture.data()), baked->picture.size());
            const uint32_t most = baked->info.maxSize > 0 ? std::min(baked->info.maxSize, asset::kMaxImageSize) : asset::kMaxImageSize;
            const auto image = platform::DecodeImage(gfx.Wic(), data, platform::PixelOrder::PremultipliedBgra, {most, most}, &why);
            if (image) {
                p.bitmap = platform::BitmapFromImage(dc_, *image);
                p.width = baked->info.width ? baked->info.width : image->width;
                p.height = baked->info.height ? baked->info.height : image->height;
            }
        }
        if (!p.bitmap) log::Warn("ゲーム UI: 画像 ({}) を読めないので色の四角で描きます{}", ref.guid, why.empty() ? std::string() : ": " + why);
    }
    return p.bitmap ? &p : nullptr;
}

const UiRenderer::Font* UiRenderer::FontOf(platform::Gfx& gfx, const AssetRef& ref, const AssetReader& asset) {
    if (ref.Empty()) return nullptr;
    Font& f = fonts_[ref.guid];
    if (f.tried) return f.collection ? &f : nullptr;
    f.tried = true;
    const auto bytes = asset ? asset(ref) : std::nullopt;
    const size_t hash = bytes ? std::hash<std::string_view>{}(*bytes) : 0;
    if (bytes && f.collection && f.hash == hash) return &f;  // the same file: the font made before
    f.collection.Reset();
    f.family.clear();
    f.hash = hash;
    Microsoft::WRL::ComPtr<IDWriteFactory5> factory;
    if (!bytes || bytes->empty() || !gfx.DWrite() || FAILED(gfx.DWrite()->QueryInterface(IID_PPV_ARGS(&factory)))) {
        log::Warn("ゲーム UI: フォントのファイル ({}) を読めないので、フォントの名前で描きます", ref.guid);
        return nullptr;
    }
    // One loader of fonts in memory for this renderer (registered on the factory, let go with it).
    Microsoft::WRL::ComPtr<IDWriteInMemoryFontFileLoader> loader;
    if (fontLoader_) {
        fontLoader_.As(&loader);
    } else if (SUCCEEDED(factory->CreateInMemoryFontFileLoader(&loader)) && SUCCEEDED(factory->RegisterFontFileLoader(loader.Get()))) {
        fontLoader_ = loader;
        loaderFactory_ = gfx.DWrite();
    }
    Microsoft::WRL::ComPtr<IDWriteFontFile> file;
    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> builder;
    Microsoft::WRL::ComPtr<IDWriteFontSet> set;
    Microsoft::WRL::ComPtr<IDWriteFontCollection1> collection;
    Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
    Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
    // (no owner given: DirectWrite keeps its own copy of the bytes)
    const bool ok = loader && SUCCEEDED(loader->CreateInMemoryFontFileReference(factory.Get(), bytes->data(), static_cast<UINT32>(bytes->size()), nullptr, &file)) &&
                    SUCCEEDED(factory->CreateFontSetBuilder(&builder)) && SUCCEEDED(builder->AddFontFile(file.Get())) &&
                    SUCCEEDED(builder->CreateFontSet(&set)) && SUCCEEDED(factory->CreateFontCollectionFromFontSet(set.Get(), &collection)) &&
                    collection->GetFontFamilyCount() > 0 && SUCCEEDED(collection->GetFontFamily(0, &family)) &&
                    SUCCEEDED(family->GetFamilyNames(&names)) && names->GetCount() > 0;
    if (!ok) {
        log::Warn("ゲーム UI: フォントのファイル ({}) を読めません (.ttf / .otf でないか、壊れています)。フォントの名前で描きます", ref.guid);
        return nullptr;
    }
    UINT32 len = 0;
    names->GetStringLength(0, &len);
    std::wstring name(len + 1, L'\0');
    names->GetString(0, name.data(), len + 1);
    name.resize(len);
    f.collection = collection;
    f.family = name;
    return &f;
}

IDWriteTextFormat* UiRenderer::Format(platform::Gfx& gfx, const std::wstring& family, IDWriteFontCollection* collection, float size, bool bold) {
    const auto key = std::make_tuple(family, collection, size, bold);
    if (const auto it = formats_.find(key); it != formats_.end()) return it->second.Get();
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (!gfx.DWrite() || FAILED(gfx.DWrite()->CreateTextFormat(family.c_str(), collection, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"ja-jp", &fmt))) {
        return nullptr;
    }
    if (formats_.size() > 256) formats_.clear();  // (sizes animated by a script: never without end)
    return formats_.emplace(key, fmt).first->second.Get();
}

void UiRenderer::DrawPiece(ID2D1Bitmap* bitmap, const D2D1_RECT_F& src, const UiBox& dst, const Affine& full, const UiPaint& paint) {
    const float sw = src.right - src.left, sh = src.bottom - src.top;
    if (sw <= 0 || sh <= 0 || dst.w <= 0 || dst.h <= 0) return;
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    const Color color = paint.tint;
    if (OnlyFades(paint)) {
        dc_->SetTransform(ToD2D(full) * old);
        dc_->DrawBitmap(bitmap, ToD2D(dst), color.a, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &src, nullptr);
    } else {
        if (!tint_ && FAILED(dc_->CreateEffect(CLSID_D2D1ColorMatrix, &tint_))) tint_.Reset();
        if (!tint_) return;
        // Each colour multiplied by the tint, then moved toward the replacing colour by its amount k (rows:
        // the input's r g b a and 1; columns: the output's). The effect works on colours not multiplied by
        // their alpha, so a replaced colour keeps the picture's shape.
        const float k = std::clamp(paint.amount, 0.f, 1.f), keep = 1 - k;
        const D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(color.r * keep, 0, 0, 0, 0, color.g * keep, 0, 0, 0, 0, color.b * keep, 0, 0, 0, 0, color.a,
                                                     paint.replace.r * k, paint.replace.g * k, paint.replace.b * k, 0);
        tint_->SetInput(0, bitmap);
        tint_->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
        Microsoft::WRL::ComPtr<ID2D1Image> out;
        tint_->GetOutput(&out);
        // The piece drawn at (0, 0) and scaled onto dst.
        const Affine place = full * Translate({dst.x, dst.y}) * Scaling(dst.w / sw, dst.h / sh);
        dc_->SetTransform(ToD2D(place) * old);
        const D2D1_POINT_2F at{0, 0};
        dc_->DrawImage(out.Get(), &at, &src, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        tint_->SetInput(0, nullptr);
    }
    dc_->SetTransform(old);
}

void UiRenderer::DrawImage(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint) {
    const UiImage& img = *n.image;
    const Color color = Paint(img.color, paint);  // (a box of colour)
    const UiPaint picture{Multiply(img.color, paint.tint), paint.replace, paint.amount};
    const Picture* p = PictureOf(gfx, n.button ? ButtonImage(*n.button, img.image) : img.image, asset);
    if (!p) {
        D2D1_MATRIX_3X2_F old;
        dc_->GetTransform(&old);
        dc_->SetTransform(ToD2D(full) * old);
        if (ID2D1SolidColorBrush* b = Brush(color)) dc_->FillRectangle(ToD2D(n.box), b);
        dc_->SetTransform(old);
        return;
    }
    const D2D1_SIZE_F dip = p->bitmap->GetSize();  // (the bitmap's own DIPs: the file may have been read smaller)
    const float kx = p->width ? dip.width / static_cast<float>(p->width) : 1.f, ky = p->height ? dip.height / static_cast<float>(p->height) : 1.f;
    const Vec2 tl = img.sliceTopLeft, br = img.sliceBottomRight;
    if (tl.x <= 0 && tl.y <= 0 && br.x <= 0 && br.y <= 0) {
        DrawPiece(p->bitmap.Get(), D2D1::RectF(0, 0, dip.width, dip.height), n.box, full, picture);
        return;
    }
    // 9 スライス: the borders keep their size (縁の大きさ), shrunk together when the box is smaller.
    const float l = std::max(0.f, tl.x), t = std::max(0.f, tl.y), r = std::max(0.f, br.x), b = std::max(0.f, br.y);
    const float s = std::max(img.sliceScale, 0.01f);
    float dl = l * s, dt = t * s, dr = r * s, db = b * s;
    if (dl + dr > n.box.w && dl + dr > 0) {
        const float f = n.box.w / (dl + dr);
        dl *= f, dr *= f;
    }
    if (dt + db > n.box.h && dt + db > 0) {
        const float f = n.box.h / (dt + db);
        dt *= f, db *= f;
    }
    const float sx[4] = {0, l * kx, dip.width - r * kx, dip.width};
    const float sy[4] = {0, t * ky, dip.height - b * ky, dip.height};
    const float dx[4] = {n.box.x, n.box.x + dl, n.box.Right() - dr, n.box.Right()};
    const float dy[4] = {n.box.y, n.box.y + dt, n.box.Bottom() - db, n.box.Bottom()};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            DrawPiece(p->bitmap.Get(), D2D1::RectF(sx[col], sy[row], sx[col + 1], sy[row + 1]),
                      {dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]}, full, picture);
        }
    }
}

void UiRenderer::DrawText(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset) {
    const UiText& t = *n.text;
    if (t.text.empty() || n.box.w <= 0 || n.box.h <= 0) return;
    const Font* font = FontOf(gfx, t.fontFile, asset);
    const std::wstring family = font ? font->family : Utf8ToWide(t.font.empty() ? std::string("Yu Gothic UI") : t.font);
    IDWriteTextFormat* fmt = Format(gfx, family, font ? font->collection.Get() : nullptr, std::max(t.size, 1.f), t.bold);
    if (!fmt) return;
    const std::wstring text = Utf8ToWide(t.text);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx.DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, n.box.w, n.box.h, &layout))) return;
    layout->SetWordWrapping(t.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(t.alignX == UiAlignX::Left ? DWRITE_TEXT_ALIGNMENT_LEADING
                             : t.alignX == UiAlignX::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                           : DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(t.alignY == UiAlignY::Top ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : t.alignY == UiAlignY::Bottom ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                                 : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(ToD2D(full) * old);
    auto draw = [&](float ox, float oy, Color c) {
        if (c.a <= 0) return;
        if (ID2D1SolidColorBrush* b = Brush(c)) dc_->DrawTextLayout({n.box.x + ox, n.box.y + oy}, layout.Get(), b, D2D1_DRAW_TEXT_OPTIONS_NONE);
    };
    if (t.shadow.x != 0 || t.shadow.y != 0) draw(t.shadow.x, t.shadow.y, t.shadowColor);
    if (t.outline > 0) {
        // The outline: the text drawn round itself (8 ways, and 8 more between them when thick).
        const int ways = t.outline > 2 ? 16 : 8;
        for (int i = 0; i < ways; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(ways);
            draw(std::cos(a) * t.outline, std::sin(a) * t.outline, t.outlineColor);
        }
    }
    draw(0, 0, t.color);
    dc_->SetTransform(old);
}

void UiRenderer::DrawOutline(const UiNode& n, const Affine& full, const UiStroke& stroke) {
    if (stroke.width <= 0 || stroke.color.a <= 0) return;
    // Round the box, outside it (the line's middle half its width out).
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(ToD2D(full) * old);
    const float h = stroke.width / 2;
    if (ID2D1SolidColorBrush* b = Brush(stroke.color)) {
        dc_->DrawRectangle(D2D1::RectF(n.box.x - h, n.box.y - h, n.box.Right() + h, n.box.Bottom() + h), b, stroke.width);
    }
    dc_->SetTransform(old);
}

void UiRenderer::DrawFocus(const UiNode& n, const Affine& full) {
    // The button chosen by the keys / a pad: a frame round it, light outside and dark inside, so it shows
    // on any colour of button and of what is behind it (the colours of the frame are not the button's).
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(ToD2D(full) * old);
    auto ring = [&](float out, float width, Color c) {
        const D2D1_ROUNDED_RECT rr{D2D1::RectF(n.box.x - out, n.box.y - out, n.box.Right() + out, n.box.Bottom() + out), 6 + out, 6 + out};
        if (ID2D1SolidColorBrush* b = Brush(c)) dc_->DrawRoundedRectangle(rr, b, width);
    };
    ring(7, 5, Color{1, 1, 1, 0.95f});
    ring(3.5f, 2, Color{0.05f, 0.05f, 0.1f, 0.85f});
    dc_->SetTransform(old);
}

void UiRenderer::Draw(ID2D1DeviceContext* dc, platform::Gfx& gfx, const UiTree& tree, const Affine& toDc, const AssetReader& asset) {
    if (!dc) return;
    if (gfx.Generation() != generation_) {
        // New devices (the GPU was reset, the driver updated): what was made on the old ones goes.
        generation_ = gfx.Generation();
        pictures_.clear();
        brush_.Reset();
        tint_.Reset();
        owner_ = nullptr;
    }
    Begin(dc);
    // Text turned or scaled cannot be ClearType: greyscale for all of it (put back afterwards).
    const D2D1_TEXT_ANTIALIAS_MODE aa = dc->GetTextAntialiasMode();
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    for (const UiNode& n : tree.nodes) {
        const Affine full = toDc * n.self;
        const UiButtonLook look = n.button ? ButtonNow(*n.button) : UiButtonLook{};
        const UiPaint paint = PaintOf(look);
        if (n.image) DrawImage(n, full, gfx, asset, paint);
        if (n.bar) {
            D2D1_MATRIX_3X2_F old;
            dc->GetTransform(&old);
            dc->SetTransform(ToD2D(full) * old);
            if (ID2D1SolidColorBrush* b = Brush(Paint(n.bar->back, paint))) dc->FillRectangle(ToD2D(n.box), b);
            if (ID2D1SolidColorBrush* b = Brush(Paint(n.bar->fill, paint))) dc->FillRectangle(ToD2D(BarFill(*n.bar, n.box)), b);
            dc->SetTransform(old);
        }
        if (n.button) DrawOutline(n, full, ButtonOutline(look));
        if (n.text) DrawText(n, full, gfx, asset);
        if (n.button && n.button->focused && n.button->interactable) DrawFocus(n, full);
    }
    dc->SetTextAntialiasMode(aa);
}

}  // namespace rm::game_ui
