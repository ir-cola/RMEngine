#include "rm_engine/game_ui/runtime/UiRenderer.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/BakedImage.h"
#include "platform/Image.h"
#include "platform/Shell.h"

#include <d2d1effects.h>
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
        shadow_.Reset();
        glow_.Reset();
        blur_.Reset();
        owner_ = dc;
    }
}

ID2D1SolidColorBrush* UiRenderer::Brush(Color c) {
    if (fresh_) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> own;
        if (FAILED(dc_->CreateSolidColorBrush(ToD2D(c), &own))) return nullptr;
        keep_.push_back(own);
        return own.Get();
    }
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

IDWriteTextFormat* UiRenderer::Format(platform::Gfx& gfx, const std::wstring& family, IDWriteFontCollection* collection, float size, bool bold, bool italic) {
    const auto key = std::make_tuple(family, collection, size, bold, italic);
    if (const auto it = formats_.find(key); it != formats_.end()) return it->second.Get();
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (!gfx.DWrite() || FAILED(gfx.DWrite()->CreateTextFormat(family.c_str(), collection, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                                                italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                                                                L"ja-jp", &fmt))) {
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
        Microsoft::WRL::ComPtr<ID2D1Effect> own;  // (recording: one of its own)
        Microsoft::WRL::ComPtr<ID2D1Effect>& tint = fresh_ ? own : tint_;
        if (!tint && FAILED(dc_->CreateEffect(CLSID_D2D1ColorMatrix, &tint))) tint.Reset();
        if (!tint) return;
        if (fresh_) keep_.push_back(tint);
        // Each colour multiplied by the tint, then moved toward the replacing colour by its amount k (rows:
        // the input's r g b a and 1; columns: the output's). The effect works on colours not multiplied by
        // their alpha, so a replaced colour keeps the picture's shape.
        const float k = std::clamp(paint.amount, 0.f, 1.f), keep = 1 - k;
        const D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(color.r * keep, 0, 0, 0, 0, color.g * keep, 0, 0, 0, 0, color.b * keep, 0, 0, 0, 0, color.a,
                                                     paint.replace.r * k, paint.replace.g * k, paint.replace.b * k, 0);
        tint->SetInput(0, bitmap);
        tint->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
        Microsoft::WRL::ComPtr<ID2D1Image> out;
        tint->GetOutput(&out);
        // The piece drawn at (0, 0) and scaled onto dst.
        const Affine place = full * Translate({dst.x, dst.y}) * Scaling(dst.w / sw, dst.h / sh);
        dc_->SetTransform(ToD2D(place) * old);
        const D2D1_POINT_2F at{0, 0};
        dc_->DrawImage(out.Get(), &at, &src, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        if (!fresh_) tint->SetInput(0, nullptr);
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
    // 切り出し: the part of the picture used (its pixels -> the bitmap's DIPs); 9 スライス counts inside it.
    const UiBox crop = CropRect({static_cast<float>(p->width), static_cast<float>(p->height)}, img.cropOffset, img.cropSize);
    const float cx = crop.x * kx, cy = crop.y * ky, cw = crop.w * kx, ch = crop.h * ky;
    if (cw <= 0 || ch <= 0) return;
    const Vec2 tl = img.sliceTopLeft, br = img.sliceBottomRight;
    if (tl.x <= 0 && tl.y <= 0 && br.x <= 0 && br.y <= 0) {
        DrawPiece(p->bitmap.Get(), D2D1::RectF(cx, cy, cx + cw, cy + ch), n.box, full, picture);
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
    const float sx[4] = {cx, cx + l * kx, cx + cw - r * kx, cx + cw};
    const float sy[4] = {cy, cy + t * ky, cy + ch - b * ky, cy + ch};
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
    const float size = std::max(t.size, 1.f);
    IDWriteTextFormat* fmt = Format(gfx, family, font ? font->collection.Get() : nullptr, size, t.bold, t.italic);
    if (!fmt) return;
    const std::wstring text = Utf8ToWide(t.text);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx.DWrite()->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, n.box.w, n.box.h, &layout))) return;
    const DWRITE_TEXT_RANGE all{0, static_cast<UINT32>(text.size())};
    if (t.vertical) {
        // 縦書き: lines go down, the next one to the left (the flow first: the two must agree).
        layout->SetFlowDirection(DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT);
        layout->SetReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM);
    }
    layout->SetWordWrapping(t.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (t.underline) layout->SetUnderline(TRUE, all);
    if (t.strike) layout->SetStrikethrough(TRUE, all);
    if (t.letterSpacing != 0) {
        Microsoft::WRL::ComPtr<IDWriteTextLayout1> spaced;
        if (SUCCEEDED(layout.As(&spaced))) spaced->SetCharacterSpacing(0, t.letterSpacing, 0, all);
    }
    if (t.lineHeight > 0) layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, t.lineHeight, t.lineHeight * 0.8f);
    // 部分のスタイル (all but the colour - that is for the text itself, not its outline and shadow).
    for (const UiTextRun& run : t.runs) {
        const UiTextRange r = TextRange(text, run.start, run.length);
        if (r.length == 0) continue;
        const DWRITE_TEXT_RANGE range{r.start, r.length};
        if (run.bold != UiRunSwitch::Keep) layout->SetFontWeight(run.bold == UiRunSwitch::On ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, range);
        if (run.italic != UiRunSwitch::Keep) layout->SetFontStyle(run.italic == UiRunSwitch::On ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, range);
        if (run.underline != UiRunSwitch::Keep) layout->SetUnderline(run.underline == UiRunSwitch::On, range);
        if (run.strike != UiRunSwitch::Keep) layout->SetStrikethrough(run.strike == UiRunSwitch::On, range);
    }
    // The sizes (the text's and the runs'), all scaled by `k`: はみ出したら縮める finds the largest that fits.
    auto sizes = [&](float k) {
        layout->SetFontSize(size * k, all);
        for (const UiTextRun& run : t.runs) {
            const UiTextRange r = TextRange(text, run.start, run.length);
            if (run.setSize && r.length > 0) layout->SetFontSize(std::max(run.size, 1.f) * k, {r.start, r.length});
        }
    };
    if (std::any_of(t.runs.begin(), t.runs.end(), [](const UiTextRun& r) { return r.setSize; })) sizes(1);
    if (t.shrinkToFit) {
        auto fits = [&] {
            DWRITE_TEXT_METRICS m{};
            if (FAILED(layout->GetMetrics(&m))) return true;
            return m.widthIncludingTrailingWhitespace <= n.box.w + 0.5f && m.height <= n.box.h + 0.5f;
        };
        if (!fits()) {
            float lo = 0.05f, hi = 1;
            for (int i = 0; i < 10; ++i) {
                const float mid = (lo + hi) * 0.5f;
                sizes(mid);
                (fits() ? lo : hi) = mid;
            }
            sizes(lo);
        }
    }
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
        c.a *= n.alpha;  // (faded with what it is in)
        if (c.a <= 0) return;
        // (文字だけずらす: only where it is drawn - its box, its wrapping and the mouse stay)
        const D2D1_POINT_2F at{n.box.x + t.offset.x + ox, n.box.y + t.offset.y + oy};
        if (ID2D1SolidColorBrush* b = Brush(c)) dc_->DrawTextLayout(at, layout.Get(), b, D2D1_DRAW_TEXT_OPTIONS_NONE);
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
    // The runs' colours, for the text itself (each its own brush: the layout holds it until drawn).
    for (const UiTextRun& run : t.runs) {
        const UiTextRange r = TextRange(text, run.start, run.length);
        if (!run.setColor || r.length == 0) continue;
        Color c = run.color;
        c.a *= n.alpha;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(dc_->CreateSolidColorBrush(ToD2D(c), &brush))) continue;
        layout->SetDrawingEffect(brush.Get(), {r.start, r.length});
        if (fresh_) keep_.push_back(brush);
    }
    draw(0, 0, Rainbowed(t.color, RainbowFor(n, &UiRainbow::text)));
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
    ring(7, 5, Color{1, 1, 1, 0.95f * n.alpha});
    ring(3.5f, 2, Color{0.05f, 0.05f, 0.1f, 0.85f * n.alpha});
    dc_->SetTransform(old);
}

// --- UI-5: 背景・枠・立体・影・光・ぼかし ---------------------------------------------------------------

namespace {

// The transform it is drawn with (the node's, then the device context's own).
D2D1_MATRIX_3X2_F Whole(const Affine& full, const D2D1_MATRIX_3X2_F& old) { return ToD2D(full) * old; }
// How many device DIPs one of its units is (for radii: its turn does not count).
float UnitsToDips(const D2D1_MATRIX_3X2_F& m) { return std::sqrt(std::abs(m._11 * m._22 - m._12 * m._21)); }
D2D1_VECTOR_4F Vector(Color c) { return {c.r, c.g, c.b, std::clamp(c.a, 0.f, 1.f)}; }

}  // namespace

ID2D1Effect* UiRenderer::Effect(Microsoft::WRL::ComPtr<ID2D1Effect>& slot, const CLSID& id) {
    if (!slot && FAILED(dc_->CreateEffect(id, &slot))) slot.Reset();
    return slot.Get();
}

void UiRenderer::FillShape(const UiNode& n, const Affine& full, Color color) {
    if (color.a <= 0) return;
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(Whole(full, old));
    const float r = n.style ? Rounding(*n.style, n.box) : 0.f;
    if (ID2D1SolidColorBrush* b = Brush(color)) dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(n.box), r, r), b);
    dc_->SetTransform(old);
}

void UiRenderer::DrawBorder(const UiNode& n, const Affine& full, const UiStroke& stroke) {
    if (stroke.width <= 0 || stroke.color.a <= 0) return;
    // Inside the box (its line's middle half its width in), the corners as the 背景's.
    const float w = std::min({stroke.width, n.box.w * 0.5f, n.box.h * 0.5f}), h = w * 0.5f;
    if (w <= 0) return;
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(Whole(full, old));
    const float r = std::max(0.f, (n.style ? Rounding(*n.style, n.box) : 0.f) - h);
    if (ID2D1SolidColorBrush* b = Brush(stroke.color)) {
        dc_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(n.box.x + h, n.box.y + h, n.box.Right() - h, n.box.Bottom() - h), r, r), b, w);
    }
    dc_->SetTransform(old);
}

void UiRenderer::DrawBevel(const UiNode& n, const Affine& full, bool inset) {
    const UiBevel& bv = n.style->bevel;
    const float w = std::min({bv.width, n.box.w * 0.5f, n.box.h * 0.5f}), h = w * 0.5f;
    if (w <= 0) return;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    dc_->GetFactory(&factory);
    if (!factory) return;
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(Whole(full, old));
    const float r = std::max(0.f, Rounding(*n.style, n.box) - h);
    const D2D1_ROUNDED_RECT ring = D2D1::RoundedRect(D2D1::RectF(n.box.x + h, n.box.y + h, n.box.Right() - h, n.box.Bottom() - h), r, r);
    // The ring of its edge: the top left half lit, the bottom right half in shade (the other way pressed in),
    // split along the line from its top right corner to its bottom left one.
    const UiBox& b = n.box;
    auto half = [&](bool topLeft, Color color) {
        if (color.a <= 0) return;
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> tri;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(&tri)) || FAILED(tri->Open(&sink))) return;
        const D2D1_POINT_2F corner = topLeft ? D2D1::Point2F(b.x - w, b.y - w) : D2D1::Point2F(b.Right() + w, b.Bottom() + w);
        sink->BeginFigure(D2D1::Point2F(b.Right() + w, b.y - w), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(corner);
        sink->AddLine(D2D1::Point2F(b.x - w, b.Bottom() + w));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return;
        dc_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), tri.Get()), nullptr);
        if (ID2D1SolidColorBrush* br = Brush(color)) dc_->DrawRoundedRectangle(ring, br, w);
        dc_->PopLayer();
        if (fresh_) keep_.push_back(tri);
    };
    Color lit = bv.highlight, dark = bv.shade;
    if (inset) std::swap(lit, dark);
    lit.a *= n.alpha;
    dark.a *= n.alpha;
    half(true, lit);
    half(false, dark);
    dc_->SetTransform(old);
}

void UiRenderer::DrawBody(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint) {
    if (n.style && n.style->background.enabled) FillShape(n, full, Paint(Rainbowed(n.style->background.color, RainbowFor(n, &UiRainbow::background)), paint));
    if (n.image) DrawImage(n, full, gfx, asset, paint);
    if (n.video) DrawVideo(n, full, gfx, asset, paint);
    if (n.bar) {
        D2D1_MATRIX_3X2_F old;
        dc_->GetTransform(&old);
        dc_->SetTransform(Whole(full, old));
        if (ID2D1SolidColorBrush* b = Brush(Paint(n.bar->back, paint))) dc_->FillRectangle(ToD2D(n.box), b);
        if (ID2D1SolidColorBrush* b = Brush(Paint(n.bar->fill, paint))) dc_->FillRectangle(ToD2D(BarFill(*n.bar, n.box)), b);
        dc_->SetTransform(old);
    }
    if (n.text) DrawText(n, full, gfx, asset);
}

void UiRenderer::DrawWithEffects(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint) {
    const UiStyle& st = *n.style;
    // Its shape, recorded once: the 影 and the 光 are made from it, the ぼかし blurs it.
    Microsoft::WRL::ComPtr<ID2D1Image> target;
    dc_->GetTarget(&target);
    Microsoft::WRL::ComPtr<ID2D1CommandList> list;
    if (!target || FAILED(dc_->CreateCommandList(&list))) {
        DrawBody(n, full, gfx, asset, paint);
        return;
    }
    dc_->SetTarget(list.Get());
    fresh_ = true;
    DrawBody(n, full, gfx, asset, paint);
    fresh_ = false;
    dc_->SetTarget(target.Get());
    if (FAILED(list->Close())) {
        keep_.clear();
        return;
    }
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    const D2D1_MATRIX_3X2_F m = Whole(full, old);
    const float k = UnitsToDips(m);
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());  // (the list holds where it was drawn)
    if (st.shadow.enabled) {
        Color c = st.shadow.color;
        c.a *= n.alpha;
        if (ID2D1Effect* fx = Effect(shadow_, CLSID_D2D1Shadow); fx && c.a > 0) {
            fx->SetInput(0, list.Get());
            fx->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, std::max(st.shadow.blur, 0.f) * k * 0.5f);
            fx->SetValue(D2D1_SHADOW_PROP_COLOR, Vector(c));
            Microsoft::WRL::ComPtr<ID2D1Image> out;
            fx->GetOutput(&out);
            // Its offset turns and scales with it.
            const D2D1_POINT_2F at{st.shadow.offset.x * m._11 + st.shadow.offset.y * m._21, st.shadow.offset.x * m._12 + st.shadow.offset.y * m._22};
            dc_->DrawImage(out.Get(), &at);
            fx->SetInput(0, nullptr);
        }
    }
    if (st.blur.enabled && st.blur.radius > 0) {
        if (ID2D1Effect* fx = Effect(blur_, CLSID_D2D1GaussianBlur)) {
            fx->SetInput(0, list.Get());
            fx->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, st.blur.radius * k * 0.5f);
            Microsoft::WRL::ComPtr<ID2D1Image> out;
            fx->GetOutput(&out);
            dc_->DrawImage(out.Get());
            fx->SetInput(0, nullptr);
        } else {
            dc_->DrawImage(list.Get());
        }
    } else {
        dc_->DrawImage(list.Get());
    }
    if (st.glow.enabled) {
        Color c = Rainbowed(st.glow.color, RainbowFor(n, &UiRainbow::glow));
        c.a *= n.alpha;
        if (ID2D1Effect* fx = Effect(glow_, CLSID_D2D1Shadow); fx && c.a > 0) {
            // A blurred silhouette of its colour, its light added to what is under it.
            fx->SetInput(0, list.Get());
            fx->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, std::max(st.glow.radius, 0.f) * k * 0.5f);
            fx->SetValue(D2D1_SHADOW_PROP_COLOR, Vector(c));
            Microsoft::WRL::ComPtr<ID2D1Image> out;
            fx->GetOutput(&out);
            dc_->DrawImage(out.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
            fx->SetInput(0, nullptr);
        }
    }
    dc_->SetTransform(old);
    keep_.clear();
}

std::optional<Color> UiRenderer::RainbowFor(const UiNode& n, bool UiRainbow::* part) const {
    if (!n.style || !n.style->rainbow.enabled || !(n.style->rainbow.*part)) return std::nullopt;
    return RainbowColor(n.style->rainbow, time_);
}

Color UiRenderer::Rainbowed(Color c, const std::optional<Color>& rainbow) { return rainbow ? Color{rainbow->r, rainbow->g, rainbow->b, c.a} : c; }

void UiRenderer::DrawVideo(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint) {
    const UiVideo& v = *n.video;
    if (v.video.Empty()) return;
    Video& slot = videos_[std::to_string(n.entity) + "/" + v.video.guid];
    slot.seen = true;
    if (!slot.player && !slot.failed) {
        std::string why;
        const auto bytes = asset ? asset(v.video) : std::nullopt;
        slot.player = bytes ? VideoPlayer::Open(gfx, *bytes, &why) : nullptr;
        if (!slot.player) {
            slot.failed = true;
            log::Warn("ゲーム UI: 動画 ({}) を流せません: {}", v.video.guid, bytes ? why : std::string("ファイルが見つかりません"));
        }
    }
    if (!slot.player) return;
    if (slot.player->Failed()) {
        if (!slot.failed) log::Warn("ゲーム UI: 動画 ({}) を流せません (この PC で読めない形式か、壊れています)", v.video.guid);
        slot.failed = true;
        slot.player.reset();
        return;
    }
    // Played only while the game plays (the editor: its first picture, silent).
    slot.player->Set(playing_ && v.playOnStart, v.loop, std::clamp(v.speed, 0.1f, 8.f), playing_ ? v.volume : 0.f, v.start, v.end);
    ID2D1Bitmap1* frame = slot.player->Frame(dc_);
    if (!frame) return;
    const D2D1_SIZE_F size = frame->GetSize();
    UiPaint p = paint;
    p.tint = Multiply(v.color, paint.tint);
    DrawPiece(frame, D2D1::RectF(0, 0, size.width, size.height), n.box, full, p);
    if (fresh_) keep_.push_back(Microsoft::WRL::ComPtr<IUnknown>(frame));
}

void UiRenderer::SetPlaying(bool playing) { playing_ = playing; }

bool UiRenderer::Animates(const UiTree& tree) {
    for (const UiNode& n : tree.nodes) {
        if (n.video || (n.style && n.style->rainbow.enabled)) return true;
    }
    return false;
}

void UiRenderer::Draw(ID2D1DeviceContext* dc, platform::Gfx& gfx, const UiTree& tree, const Affine& toDc, const AssetReader& asset) {
    if (!dc) return;
    if (gfx.Generation() != generation_) {
        // New devices (the GPU was reset, the driver updated): what was made on the old ones goes.
        generation_ = gfx.Generation();
        pictures_.clear();
        brush_.Reset();
        tint_.Reset();
        shadow_.Reset();
        glow_.Reset();
        blur_.Reset();
        videos_.clear();
        owner_ = nullptr;
    }
    Begin(dc);
    time_ = platform::NowSeconds();  // (虹色 goes round with the clock)
    for (auto& [key, v] : videos_) v.seen = false;
    // Text turned or scaled cannot be ClearType: greyscale for all of it (put back afterwards).
    const D2D1_TEXT_ANTIALIAS_MODE aa = dc->GetTextAntialiasMode();
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    for (const UiNode& n : tree.nodes) {
        const Affine full = toDc * n.self;
        if (n.alpha <= 0.001f) continue;  // faded out whole (its children too: their alpha is under it)
        const UiButtonLook look = n.button ? ButtonNow(*n.button) : UiButtonLook{};
        UiPaint paint = PaintOf(look);
        paint.tint.a *= n.alpha;  // (出る・消える動き / FadeTo: everything it draws)
        if (const auto rb = RainbowFor(n, &UiRainbow::body)) paint.tint = Multiply(paint.tint, *rb);
        // The order (spec §3-4): 影 - its body (背景, 画像, バー, 文字; ぼかし) - 光 - 立体 - 枠 - the keys' choice.
        const UiStyle* st = n.style;
        if (st && (st->shadow.enabled || st->glow.enabled || (st->blur.enabled && st->blur.radius > 0))) {
            DrawWithEffects(n, full, gfx, asset, paint);
        } else {
            DrawBody(n, full, gfx, asset, paint);
        }
        if (st && BevelShown(*st)) DrawBevel(n, full, BevelInset(*st, n.button));
        if (st && st->border.enabled) {
            // Its own 枠, mixed with a button's state outline (UI-2: toward its own frame, never thinner).
            const UiStroke own{st->border.width, Rainbowed(st->border.color, RainbowFor(n, &UiRainbow::border))};
            UiStroke stroke = n.button ? ButtonOutline(look, own) : own;
            stroke.color.a *= n.alpha;
            DrawBorder(n, full, stroke);
        } else if (n.button) {
            UiStroke stroke = ButtonOutline(look);
            stroke.color.a *= n.alpha;
            DrawOutline(n, full, stroke);
        }
        if (n.button && n.button->focused && n.button->interactable) DrawFocus(n, full);
    }
    dc->SetTextAntialiasMode(aa);
    std::erase_if(videos_, [](const auto& kv) { return !kv.second.seen; });  // (not drawn: stopped - its sound too)
}

}  // namespace rm::game_ui
