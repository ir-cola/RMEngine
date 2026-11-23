#include "rm_engine/game_ui/runtime/UiRenderer.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/BakedImage.h"
#include "rm_engine/game_ui/runtime/UiControls.h"
#include "platform/Image.h"
#include "platform/Shell.h"

#include <d2d1effects.h>
#include <dwrite_3.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace rm::game_ui {

namespace {

D2D1_COLOR_F ToD2D(Color c) { return {c.r, c.g, c.b, c.a}; }
D2D1_MATRIX_3X2_F ToD2D(const Affine& m) { return D2D1::Matrix3x2F(m.a, m.b, m.c, m.d, m.tx, m.ty); }
D2D1_RECT_F ToD2D(const UiBox& b) { return D2D1::RectF(b.x, b.y, b.x + b.w, b.y + b.h); }
// 透明度だけが絵を変える (そのまま描き、薄くする)。
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
    // フォントは見直すだけ: 同じバイト列で作り直すと、ローダーがレンダラーの生きている間ずっと持つ
    // コピーがもう 1 つ増えてしまう。
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
    if (bytes && f.collection && f.hash == hash) return &f;  // 同じファイル: 前に作ったフォント
    f.collection.Reset();
    f.family.clear();
    f.hash = hash;
    Microsoft::WRL::ComPtr<IDWriteFactory5> factory;
    if (!bytes || bytes->empty() || !gfx.DWrite() || FAILED(gfx.DWrite()->QueryInterface(IID_PPV_ARGS(&factory)))) {
        log::Warn("ゲーム UI: フォントのファイル ({}) を読めないので、フォントの名前で描きます", ref.guid);
        return nullptr;
    }
    // このレンダラー用のメモリ上のフォントのローダー 1 つ (ファクトリーに登録し、一緒に手放す)。
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
    // (持ち主の指定なし: DirectWrite はバイト列の自分のコピーを持つ)
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
    if (formats_.size() > 256) formats_.clear();  // (スクリプトで動かすサイズ: 際限なく増やさない)
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
        Microsoft::WRL::ComPtr<ID2D1Effect> own;  // (記録中: 自分専用のもの)
        Microsoft::WRL::ComPtr<ID2D1Effect>& tint = fresh_ ? own : tint_;
        if (!tint && FAILED(dc_->CreateEffect(CLSID_D2D1ColorMatrix, &tint))) tint.Reset();
        if (!tint) return;
        if (fresh_) keep_.push_back(tint);
        // 各色にティントを掛け、次に量 k だけ置き換える色へ寄せる (行: 入力の r g b a と 1、列: 出力の)。
        // エフェクトは alpha を掛けていない色で働くので、置き換えた色は絵の形を保つ。
        const float k = std::clamp(paint.amount, 0.f, 1.f), keep = 1 - k;
        const D2D1_MATRIX_5X4_F m = D2D1::Matrix5x4F(color.r * keep, 0, 0, 0, 0, color.g * keep, 0, 0, 0, 0, color.b * keep, 0, 0, 0, 0, color.a,
                                                     paint.replace.r * k, paint.replace.g * k, paint.replace.b * k, 0);
        tint->SetInput(0, bitmap);
        tint->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, m);
        Microsoft::WRL::ComPtr<ID2D1Image> out;
        tint->GetOutput(&out);
        // (0, 0) に描いた部分を dst に拡大縮小したもの。
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
    const Color color = Paint(img.color, paint);  // (色の箱)
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
    const D2D1_SIZE_F dip = p->bitmap->GetSize();  // (ビットマップ自身の DIP: ファイルは小さく読まれたかもしれない)
    const float kx = p->width ? dip.width / static_cast<float>(p->width) : 1.f, ky = p->height ? dip.height / static_cast<float>(p->height) : 1.f;
    // 切り出し: 使う絵の部分 (そのピクセル -> ビットマップの DIP)。9 スライス はその内側で数える。
    const UiBox crop = CropRect({static_cast<float>(p->width), static_cast<float>(p->height)}, img.cropOffset, img.cropSize);
    const float cx = crop.x * kx, cy = crop.y * ky, cw = crop.w * kx, ch = crop.h * ky;
    if (cw <= 0 || ch <= 0) return;
    const Vec2 tl = img.sliceTopLeft, br = img.sliceBottomRight;
    if (tl.x <= 0 && tl.y <= 0 && br.x <= 0 && br.y <= 0) {
        DrawPiece(p->bitmap.Get(), D2D1::RectF(cx, cy, cx + cw, cy + ch), n.box, full, picture);
        return;
    }
    // 9 スライス: 縁は大きさを保ち (縁の大きさ)、箱が小さいときは一緒に縮む。
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
        // 縦書き: 行は下へ進み、次の行は左へ (先に流れの向き: 2 つは一致していなければならない)。
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
    // 部分のスタイル (色以外すべて - 色はテキスト自身のもので、縁取りや影のものではない)。
    for (const UiTextRun& run : t.runs) {
        const UiTextRange r = TextRange(text, run.start, run.length);
        if (r.length == 0) continue;
        const DWRITE_TEXT_RANGE range{r.start, r.length};
        if (run.bold != UiRunSwitch::Keep) layout->SetFontWeight(run.bold == UiRunSwitch::On ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, range);
        if (run.italic != UiRunSwitch::Keep) layout->SetFontStyle(run.italic == UiRunSwitch::On ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, range);
        if (run.underline != UiRunSwitch::Keep) layout->SetUnderline(run.underline == UiRunSwitch::On, range);
        if (run.strike != UiRunSwitch::Keep) layout->SetStrikethrough(run.strike == UiRunSwitch::On, range);
    }
    // サイズ (テキストのと各部分の) をすべて `k` 倍する: はみ出したら縮める が収まる最大のものを探す。
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
        c.a *= n.alpha;  // (入っているものと一緒に薄くする)
        if (c.a <= 0) return;
        // (文字だけずらす: 描く場所だけ - 箱、折り返し、マウスはそのまま)
        const D2D1_POINT_2F at{n.box.x + t.offset.x + ox, n.box.y + t.offset.y + oy};
        if (ID2D1SolidColorBrush* b = Brush(c)) dc_->DrawTextLayout(at, layout.Get(), b, D2D1_DRAW_TEXT_OPTIONS_NONE);
    };
    if (t.shadow.x != 0 || t.shadow.y != 0) draw(t.shadow.x, t.shadow.y, t.shadowColor);
    if (t.outline > 0) {
        // 縁取り: テキストをそのまわりに描く (8 方向、太いときはその間にさらに 8 方向)。
        const int ways = t.outline > 2 ? 16 : 8;
        for (int i = 0; i < ways; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(ways);
            draw(std::cos(a) * t.outline, std::sin(a) * t.outline, t.outlineColor);
        }
    }
    // 各部分の色、テキスト自身用 (それぞれ自分のブラシ: レイアウトは描くまでそれを持つ)。
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
    // 箱のまわり、外側に (線の真ん中が幅の半分だけ外へ)。
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
    // キー / パッドで選んだボタン: そのまわりの枠で、外が明るく内が暗い。どんな色のボタンの上でも、
    // 後ろがどんな色でも見えるように (枠の色はボタンのものではない)。
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

// 描くときの変換 (ノードのもの、次にデバイスコンテキスト自身のもの)。
D2D1_MATRIX_3X2_F Whole(const Affine& full, const D2D1_MATRIX_3X2_F& old) { return ToD2D(full) * old; }
// そのものの 1 単位がデバイスの何 DIP か (半径用: 回転は数えない)。
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
    // 箱の内側 (線の真ん中が幅の半分だけ内へ)、角は 背景 のもの。
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
    // 縁のリング: 左上の半分が明るく、右下の半分が影 (押し込まれたときは逆)。右上の角から左下の角への
    // 線で分ける。
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
    if (n.slider) DrawSlider(n, full, paint);
    if (n.toggle) DrawToggle(n, full, paint);
    if (n.input) {
        DrawInput(n, full, gfx, asset);  // (同じエンティティの文字は、その見た目として使う)
    } else if (n.text) {
        DrawText(n, full, gfx, asset);
    }
}

// --- 入力の部品 ---------------------------------------------------------------------------------

void UiRenderer::DrawSlider(const UiNode& n, const Affine& full, const UiPaint& paint) {
    const UiSlider& s = *n.slider;
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(ToD2D(full) * old);
    auto bar = [&](const UiBox& b, Color c) {
        if (b.w <= 0 || b.h <= 0) return;
        const float r = std::min(b.w, b.h) * 0.5f;
        if (ID2D1SolidColorBrush* br = Brush(Paint(c, paint))) dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(b), r, r), br);
    };
    bar(SliderTrack(s, n.box), s.back);
    bar(SliderFill(s, n.box), s.fill);
    const UiBox h = SliderHandle(s, n.box);
    if (h.w > 0) {
        if (ID2D1SolidColorBrush* br = Brush(Paint(s.handle, paint))) {
            dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(h.x + h.w * 0.5f, h.y + h.h * 0.5f), h.w * 0.5f, h.h * 0.5f), br);
        }
    }
    dc_->SetTransform(old);
}

void UiRenderer::DrawToggle(const UiNode& n, const Affine& full, const UiPaint& paint) {
    const UiToggle& t = *n.toggle;
    const UiBox m = ToggleMark(t, n.box);
    if (m.w <= 0 || m.h <= 0) return;
    const float on = ToggleShown(t);
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(ToD2D(full) * old);
    const Color body = Paint(Mix(t.back, t.onColor, on), paint);
    if (t.look == UiToggleLook::Switch) {
        // 溝 (地の色からオンの色へ) と、左 (オフ) から右 (オン) へ動く丸いつまみ。
        const float r = m.h * 0.5f;
        if (ID2D1SolidColorBrush* br = Brush(body)) dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(m), r, r), br);
        const float knob = m.h * 0.38f;
        const float cx = m.x + r + (m.w - 2 * r) * on;
        if (ID2D1SolidColorBrush* br = Brush(Paint(t.mark, paint))) dc_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, m.y + r), knob, knob), br);
    } else {
        // 角の丸い箱と、オンのときのチェックの印 (薄くなって出る)。
        const float r = m.h * 0.2f;
        if (ID2D1SolidColorBrush* br = Brush(body)) dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(m), r, r), br);
        Color c = Paint(t.mark, paint);
        c.a *= on;
        if (c.a > 0.001f) {
            if (ID2D1SolidColorBrush* br = Brush(c)) {
                const float w = std::max(1.f, m.h * 0.12f);
                const D2D1_POINT_2F a{m.x + m.w * 0.24f, m.y + m.h * 0.52f}, b{m.x + m.w * 0.43f, m.y + m.h * 0.71f}, e{m.x + m.w * 0.77f, m.y + m.h * 0.3f};
                dc_->DrawLine(a, b, br, w);
                dc_->DrawLine(b, e, br, w);
            }
        }
    }
    dc_->SetTransform(old);
}

void UiRenderer::DrawInput(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset) {
    const UiInputField& f = *n.input;
    const UiText style = n.text ? *n.text : UiText{};
    const UiInputView v = InputView(f);
    const float pad = std::clamp(f.padding, 0.f, n.box.w * 0.5f);
    const UiBox in{n.box.x + pad, n.box.y, n.box.w - 2 * pad, n.box.h};
    if (in.w <= 0 || in.h <= 0) return;
    const Font* font = FontOf(gfx, style.fontFile, asset);
    const std::wstring family = font ? font->family : Utf8ToWide(style.font.empty() ? std::string("Yu Gothic UI") : style.font);
    IDWriteTextFormat* fmt = Format(gfx, family, font ? font->collection.Get() : nullptr, std::max(style.size, 1.f), style.bold, style.italic);
    if (!fmt) return;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx.DWrite()->CreateTextLayout(v.shown.c_str(), static_cast<UINT32>(v.shown.size()), fmt, 100000.f, in.h, &layout))) return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(style.alignY == UiAlignY::Top      ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR
                                  : style.alignY == UiAlignY::Bottom ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                                     : DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if (v.compLength > 0) layout->SetUnderline(TRUE, {v.compStart, v.compLength});  // (IME の変換中)
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    const float width = tm.widthIncludingTrailingWhitespace;
    FLOAT caretX = 0, caretY = 0;
    DWRITE_HIT_TEST_METRICS hm{};
    layout->HitTestTextPosition(v.caret, FALSE, &caretX, &caretY, &hm);
    // 横の場所: 入るなら揃えのとおり。入らなければ、打っている間はカーソルが見えるように左へずらす。
    float x = 0;
    if (width <= in.w) {
        x = style.alignX == UiAlignX::Center ? (in.w - width) * 0.5f : style.alignX == UiAlignX::Right ? in.w - width : 0.f;
    } else if (f.typing) {
        x = -std::clamp(caretX - (in.w - 2.f), 0.f, width - in.w + 2.f);
    }
    const D2D1_POINT_2F at{in.x + x, in.y};
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(ToD2D(full) * old);
    dc_->PushAxisAlignedClip(ToD2D(in), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    auto ranges = [&](uint32_t start, uint32_t length) {
        std::vector<DWRITE_HIT_TEST_METRICS> out;
        UINT32 count = 0;
        layout->HitTestTextRange(start, length, at.x, at.y, nullptr, 0, &count);
        if (count == 0) return out;
        out.resize(count);
        if (FAILED(layout->HitTestTextRange(start, length, at.x, at.y, out.data(), count, &count))) out.clear();
        out.resize(std::min<size_t>(out.size(), count));
        return out;
    };
    auto faded = [&](Color c) {
        c.a *= n.alpha;
        return c;
    };
    if (f.typing && v.selEnd > v.selStart) {
        if (ID2D1SolidColorBrush* b = Brush(faded(f.selectionColor))) {
            for (const auto& r : ranges(v.selStart, v.selEnd - v.selStart)) dc_->FillRectangle(D2D1::RectF(r.left, r.top, r.left + r.width, r.top + r.height), b);
        }
    }
    if (ID2D1SolidColorBrush* b = Brush(faded(v.placeholder ? f.placeholderColor : style.color))) dc_->DrawTextLayout(at, layout.Get(), b, D2D1_DRAW_TEXT_OPTIONS_NONE);
    // カーソル: 打っている間、0.5 秒ごとに点滅する。
    if (f.typing && std::fmod(time_, 1.0) < 0.5) {
        const float cx = at.x + caretX;
        const float top = at.y + hm.top, h = hm.height > 0 ? hm.height : std::max(style.size, 1.f);
        if (ID2D1SolidColorBrush* b = Brush(faded(f.caretColor))) dc_->DrawLine({cx, top}, {cx, top + h}, b, std::max(1.5f, style.size * 0.05f));
    }
    dc_->PopAxisAlignedClip();
    dc_->SetTransform(old);
}

void UiRenderer::DrawScrollBars(const UiNode& n, const Affine& full) {
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    dc_->SetTransform(ToD2D(full) * old);
    for (const bool vertical : {true, false}) {
        const UiScrollBar bar = ScrollBar(*n.scroll, n.box, n.content, n.scroll->offset, vertical);
        if (!bar.shown) continue;
        const float r = std::min(bar.track.w, bar.track.h) * 0.5f;
        Color back = n.scroll->barBack, thumb = n.scroll->barThumb;
        back.a *= n.alpha;
        thumb.a *= n.alpha;
        if (ID2D1SolidColorBrush* b = Brush(back)) dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(bar.track), r, r), b);
        if (ID2D1SolidColorBrush* b = Brush(thumb)) dc_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(bar.thumb), r, r), b);
    }
    dc_->SetTransform(old);
}

int UiRenderer::PushClips(const UiTree& tree, int clip, const Affine& toDc) {
    // 外側の枠から順に。
    std::vector<int> chain;
    for (int c = clip; c >= 0 && c < static_cast<int>(tree.nodes.size()) && chain.size() <= tree.nodes.size(); c = tree.nodes[static_cast<size_t>(c)].clip) chain.push_back(c);
    D2D1_MATRIX_3X2_F old;
    dc_->GetTransform(&old);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const UiNode& s = tree.nodes[static_cast<size_t>(*it)];
        dc_->SetTransform(ToD2D(toDc * s.self) * old);
        dc_->PushAxisAlignedClip(ToD2D(s.box), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
    dc_->SetTransform(old);
    return static_cast<int>(chain.size());
}

void UiRenderer::PopClips(int count) {
    for (int i = 0; i < count; ++i) dc_->PopAxisAlignedClip();
}

void UiRenderer::DrawWithEffects(const UiNode& n, const Affine& full, platform::Gfx& gfx, const AssetReader& asset, const UiPaint& paint) {
    const UiStyle& st = *n.style;
    // その形を 1 度だけ記録する: 影 と 光 はそれから作り、ぼかし はそれをぼかす。
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
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());  // (リストが描いた場所を持っている)
    if (st.shadow.enabled) {
        Color c = st.shadow.color;
        c.a *= n.alpha;
        if (ID2D1Effect* fx = Effect(shadow_, CLSID_D2D1Shadow); fx && c.a > 0) {
            fx->SetInput(0, list.Get());
            fx->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, std::max(st.shadow.blur, 0.f) * k * 0.5f);
            fx->SetValue(D2D1_SHADOW_PROP_COLOR, Vector(c));
            Microsoft::WRL::ComPtr<ID2D1Image> out;
            fx->GetOutput(&out);
            // そのオフセットは一緒に回り、拡大縮小する。
            const D2D1_POINT_2F at{st.shadow.offset.x * m._11 + st.shadow.offset.y * m._21, st.shadow.offset.x * m._12 + st.shadow.offset.y * m._22};
            dc_->DrawImage(out.Get(), &at);
            fx->SetInput(0, nullptr);
        }
    }
    if (st.glow.enabled) {
        Color c = Rainbowed(st.glow.color, RainbowFor(n, &UiRainbow::glow));
        c.a *= n.alpha;
        if (ID2D1Effect* fx = Effect(glow_, CLSID_D2D1Shadow); fx && c.a > 0) {
            // その色のぼやけたシルエットで、その光を下にあるものへ加える - 本体の下に: その周りで光るように
            // (上に重ねると本体そのものが白くなる: 2026-11-17 画面で確認)。
            fx->SetInput(0, list.Get());
            fx->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, std::max(st.glow.radius, 0.f) * k * 0.5f);
            fx->SetValue(D2D1_SHADOW_PROP_COLOR, Vector(c));
            Microsoft::WRL::ComPtr<ID2D1Image> out;
            fx->GetOutput(&out);
            dc_->DrawImage(out.Get(), nullptr, nullptr, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_PLUS);
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
    // ゲームの Play 中だけ再生する (エディター: 最初の絵、音なし)。
    slot.player->Set(playing_ && v.playOnStart, v.loop, std::clamp(v.speed, 0.1f, 8.f), playing_ ? v.volume * std::clamp(v.gain, 0.f, 2.f) : 0.f, v.start, v.end);
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
        if (n.video || (n.style && n.style->rainbow.enabled) || (n.input && n.input->typing)) return true;  // (カーソルの点滅)
    }
    return false;
}

void UiRenderer::Draw(ID2D1DeviceContext* dc, platform::Gfx& gfx, const UiTree& tree, const Affine& toDc, const AssetReader& asset) {
    if (!dc) return;
    if (gfx.Generation() != generation_) {
        // 新しいデバイス (GPU がリセットされた、ドライバーが更新された): 古いデバイスで作ったものは捨てる。
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
    time_ = platform::NowSeconds();  // (虹色 は時計に合わせて回る)
    for (auto& [key, v] : videos_) v.seen = false;
    // 回したり拡大縮小したテキストは ClearType にできない: 全部グレースケールにする (あとで戻す)。
    const D2D1_TEXT_ANTIALIAS_MODE aa = dc->GetTextAntialiasMode();
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    std::vector<int> open;  // 中を描いている途中のスクロール (下の物を全部描いたら、その上にバー)
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        const UiNode& n = tree.nodes[i];
        if (n.alpha > 0.001f) {  // (全体が消えた物は描かない。子も: その alpha はこの下にある)
            if (n.clip >= 0 && showClipped_) {
                UiNode ghost = n;  // 枠の外の分も薄く (UI タブ)
                ghost.alpha *= 0.25f;
                DrawNode(ghost, toDc, gfx, asset);
            }
            const int clips = n.clip >= 0 ? PushClips(tree, n.clip, toDc) : 0;
            DrawNode(n, toDc, gfx, asset);
            PopClips(clips);
            if (n.scroll) open.push_back(static_cast<int>(i));
        }
        while (!open.empty() && tree.nodes[static_cast<size_t>(open.back())].end <= static_cast<int>(i) + 1) {
            const UiNode& s = tree.nodes[static_cast<size_t>(open.back())];
            open.pop_back();
            const int clips = s.clip >= 0 ? PushClips(tree, s.clip, toDc) : 0;
            DrawScrollBars(s, toDc * s.self);
            PopClips(clips);
        }
    }
    dc->SetTextAntialiasMode(aa);
    std::erase_if(videos_, [](const auto& kv) { return !kv.second.seen; });  // (描かれていない: 止める - 音も)
}

void UiRenderer::DrawNode(const UiNode& n, const Affine& toDc, platform::Gfx& gfx, const AssetReader& asset) {
    const Affine full = toDc * n.self;
    const UiButtonLook look = n.button ? ButtonNow(*n.button) : UiButtonLook{};
    UiPaint paint = PaintOf(look);
    paint.tint.a *= n.alpha;  // (出る・消える動き / FadeTo: 描くものすべて)
    if (const auto rb = RainbowFor(n, &UiRainbow::body)) paint.tint = Multiply(paint.tint, *rb);
    // 順序: 影 - 発光 - 本体 (背景、画像、バー、文字; ぼかし) - 立体 - 枠 - キーの選択 (仕様 §3-4 は
    // 光を本体の上に置く: 本体そのものが白くなった - 2026-11-17 画面で確認)。
    const UiStyle* st = n.style;
    if (st && (st->shadow.enabled || st->glow.enabled || (st->blur.enabled && st->blur.radius > 0))) {
        DrawWithEffects(n, full, gfx, asset, paint);
    } else {
        DrawBody(n, full, gfx, asset, paint);
    }
    if (st && BevelShown(*st)) DrawBevel(n, full, BevelInset(*st, n.button));
    if (st && st->border.enabled) {
        // その 枠 を、ボタンの状態の縁取りと混ぜる (UI-2: 自分の枠へ向かい、細くはならない)。
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

}  // namespace rm::game_ui
