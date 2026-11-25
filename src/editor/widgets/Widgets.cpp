#include "editor/widgets/Widgets.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/IconAtlas.h"
#include "editor/widgets/PickTree.h"
#include "editor/widgets/Theme.h"
#include "platform/Gfx.h"
#include "resource.h"
#include "ui/Path.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <map>
#include <unordered_map>

namespace rm::editor::w {

using ui::Color;
using ui::Rect;

// ---------------------------------------------------------------------------
// 面・アイコン・行・タブ: それぞれの描き方は 1 本だけ
// ---------------------------------------------------------------------------

namespace {

void FillGradient(ui::Painter& p, const Rect& r, float radius, const theme::Gradient& g) { p.FillRoundRectGradient(r, radius, g.top, g.bottom); }

// 盛り上がった面の上の縁に当たる光。
void TopLight(ui::Painter& p, const Rect& r, float radius, Color c) {
    const float inset = std::max(radius, 2.f);
    if (r.w > inset * 2) p.FillRect({r.x + inset, r.y + 1, r.w - inset * 2, 1}, c);
}

}  // namespace

void DrawSurface(ui::Context& c, const Rect& r, Surface s, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    auto& p = c.P();
    switch (s) {
        case Surface::Panel:
            DrawPanelFrame(c, r, ui::Corners::All(radius));
            break;
        case Surface::Raised:
            FillGradient(p, r, radius, theme::Raised(theme::PanelHeader));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
        case Surface::Hovered:
            FillGradient(p, r, radius, theme::Raised(theme::Hover));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::BorderHover);
            break;
        case Surface::Pressed:
            FillGradient(p, r, radius, theme::Raised(theme::Pressed).Flipped());
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
        case Surface::Sunken:
            FillGradient(p, r, radius, theme::Sunken(theme::Field));
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
        case Surface::Accent:
        case Surface::AccentHovered:
        case Surface::AccentPressed: {
            const Color base = s == Surface::Accent ? theme::Accent : s == Surface::AccentHovered ? theme::AccentHover : theme::AccentPressed;
            const theme::Gradient g = theme::AccentOf(base);
            FillGradient(p, r, radius, s == Surface::AccentPressed ? g.Flipped() : g);
            if (s != Surface::AccentPressed) TopLight(p, r, radius, theme::AccentHighlight());
            p.StrokeRoundRect(r, radius, theme::AccentPressed);
            break;
        }
        case Surface::Selected:
            FillGradient(p, r, radius, theme::Raised(theme::Selected));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::Accent.Alpha(0.35f));
            break;
        case Surface::Popup:
            p.Shadow(r, radius, theme::Shadow, 14);
            FillGradient(p, r, radius, theme::Raised(theme::PanelHeader));
            TopLight(p, r, radius, theme::Highlight());
            p.StrokeRoundRect(r, radius, theme::Border);
            break;
    }
}

namespace {

// どのウィンドウが独立したパネルのウィンドウか (DrawBackdrop が控える。その絵は明るめ)。
std::unordered_map<const ui::Context*, bool>& FloatingWindows() {
    static std::unordered_map<const ui::Context*, bool> floating;
    return floating;
}

}  // namespace

void DrawBackdrop(ui::Context& c, bool floating) {
    FloatingWindows()[&c] = floating;
    if (const OrnamentInfo* o = theme::CurrentOrnament) o->window(c, c.Viewport(), floating);
}

void DrawBackdropOn(ui::Context& c, const Rect& r, float corner, bool panelThings) {
    const OrnamentInfo* o = theme::CurrentOrnament;
    if (!o || r.w <= 0 || r.h <= 0) return;
    const auto it = FloatingWindows().find(&c);
    // パネルの地 (夏の砂の粒) が先、ウィンドウの絵 (シルエット) がその上 (2026-11-25: 絵が粒に埋もれない)。
    if (panelThings && o->panel) o->panel(c, r, corner);
    c.P().PushClip(r);
    o->window(c, c.Viewport(), it != FloatingWindows().end() && it->second);
    c.P().PopClip();
}

void DrawPanelFrame(ui::Context& c, const Rect& r, ui::Corners k) {
    if (r.w <= 0 || r.h <= 0) return;
    auto& p = c.P();
    auto grow = [](float v) { return v > 0 ? v + 1.f : 0.f; };  // 外の線の角: 同じ曲線を 1px 外に
    const theme::Gradient fill = theme::Gentle(theme::Panel);
    const theme::Gradient inner = theme::EdgeInner();
    const Color outer = theme::EdgeOuter();
    p.StrokeRoundRectGradient(r.Inset(-1), {grow(k.tl), grow(k.tr), grow(k.br), grow(k.bl)}, outer, outer);
    p.FillRoundRectGradient(r, k, fill.top, fill.bottom);
    DrawBackdropOn(c, r, std::max(k.bl, k.br));  // テーマの絵。内容の下に描く
    p.StrokeRoundRectGradient(r, k, inner.top, inner.bottom);
}

void DrawBar(ui::Context& c, const Rect& r, Color base) {
    const theme::Gradient g = theme::Gentle(base);
    c.P().FillRectGradient(r, g.top, g.bottom);
    DrawBackdropOn(c, r, 0, false);  // 絵だけ。細い帯には雪を積もらせない
}

namespace {

// 1 枚にまとめた絵 (IconAtlas.h)。どのウィンドウも同じ GPU のデバイスなので、絵は 1 つでよい。
// 描くときは 1 マスずつ別のビットマップに写したものを使う: 色を塗り替える道 (ColorMatrix のエフェクト) で
// 1 枚の一部を切り出すと、その四角の単位が画面の DPI で変わり、125 % / 150% の画面で隣の絵が出た (2026-11-23)。
struct Icons {
    platform::Gfx* gfx = nullptr;
    std::optional<IconAtlas> atlas;
    bool atlasRead = false;
    std::map<std::string, Microsoft::WRL::ComPtr<ID2D1Bitmap>, std::less<>> pictures;  // 絵の名前 -> その 1 マス
    uint64_t madeIn = 0;  // pictures を作った Gfx::Generation (0: まだ)
    std::set<std::string, std::less<>> missing;  // 1 枚の画像に無かった絵 (ログは 1 回だけ)
};

// 1 枚の画像を 1 マスずつのビットマップに分ける (画素で写す: DPI に関係ない)。
void SplitPictures(Icons& s, ID2D1DeviceContext* dc) {
    s.pictures.clear();
    const Microsoft::WRL::ComPtr<ID2D1Bitmap> sheet = platform::LoadBitmapFromMemory(*s.gfx, dc, platform::ResourceBytes(IDR_ICONS));
    if (!sheet || !dc) return;
    const UINT side = static_cast<UINT>(s.atlas->cell);
    const D2D1_SIZE_U all = sheet->GetPixelSize();
    float dpiX = 96, dpiY = 96;
    sheet->GetDpi(&dpiX, &dpiY);
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(sheet->GetPixelFormat(), dpiX, dpiY);
    for (const auto& [name, cell] : s.atlas->cells) {
        const UINT x = static_cast<UINT>(cell.x), y = static_cast<UINT>(cell.y);
        if (side == 0 || x + side > all.width || y + side > all.height) continue;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> one;
        if (FAILED(dc->CreateBitmap(D2D1::SizeU(side, side), nullptr, 0, props, &one))) continue;
        const D2D1_RECT_U from{x, y, x + side, y + side};
        if (SUCCEEDED(one->CopyFromBitmap(nullptr, sheet.Get(), &from))) s.pictures.emplace(name, std::move(one));
    }
}
Icons& IconStore() {
    static Icons s;
    return s;
}

// 絵があれば描いて true。
bool DrawPicture(ui::Context& c, const Rect& r, Icon icon, Color color, float size) {
    const std::string_view name = IconPicture(icon);
    Icons& s = IconStore();
    if (name.empty() || !s.gfx) return false;
    if (!s.atlasRead) {
        s.atlasRead = true;
        const auto bytes = platform::ResourceBytes(IDR_ICONS_INDEX);
        std::string err;
        json::ParseError pe;
        if (auto doc = json::Parse({reinterpret_cast<const char*>(bytes.data()), bytes.size()}, &pe)) {
            s.atlas = ParseIconAtlas(*doc, &err);
        } else {
            err = std::format("{} 行 {} 文字目: {}", pe.line, pe.column, pe.message);
        }
        if (!s.atlas) log::Error("アイコンの目次を読めませんでした: {}", err);
    }
    if (!s.atlas) return false;
    if (s.madeIn != s.gfx->Generation()) {  // (初めて、または GPU を作り直した: 読めなくても毎フレームは読み直さない)
        s.madeIn = s.gfx->Generation();
        SplitPictures(s, c.P().DC());
    }
    const IconCell* cell = s.atlas->Find(name);
    if (!cell && s.missing.emplace(name).second) log::Error("アイコンの絵「{}」が 1 枚の画像にありません (tools/make_icon_atlas.ps1)", name);
    const auto picture = s.pictures.find(name);
    if (!cell || picture == s.pictures.end()) return false;
    // 画面の画素で置く: 枠の左右・上下の余りがちょうど同じになるよう、絵の一辺の画素数を枠と同じ偶奇に
    // する (2026-11-24: DIP で丸めていたので、44 の枠に 35 の絵だと片側 4、もう片側 5 になり、125 % の画面では
    // さらに画素の端数がずれて、少し左上に寄って見えた)。
    const float want = size > 0 ? size : std::min(std::round(std::min(r.w, r.h) * 0.8f), 16.f);
    float dpiX = 96, dpiY = 96;
    if (ID2D1DeviceContext* dc = c.P().DC()) dc->GetDpi(&dpiX, &dpiY);
    const float k = dpiX > 0 ? dpiX / 96.f : 1.f;
    const float left = std::round(r.x * k), top = std::round(r.y * k);
    const float boxW = std::round((r.x + r.w) * k) - left, boxH = std::round((r.y + r.h) * k) - top;
    float px = std::max(1.f, std::round(want * k));
    if (std::fmod(boxW - px, 2.f) != 0.f) px -= 1;  // (余りを偶数に: 左右に同じだけ)
    px = std::max(1.f, px);
    const float padX = (boxW - px) * 0.5f;
    const float padY = std::round((boxH - px) * 0.5f);
    const Rect dst{(left + padX) / k, (top + padY) / k, px / k, px / k};
    if (cell->color) {
        c.P().Image(picture->second.Get(), dst, color.a);
    } else {
        c.P().ImageTinted(picture->second.Get(), dst, color);
    }
    return true;
}

}  // namespace

void InitIcons(platform::Gfx& gfx) { IconStore().gfx = &gfx; }

void DrawIcon(ui::Context& c, const Rect& r, Icon icon, Color color, float size) {
    if (DrawPicture(c, r, icon, color, size)) return;
    auto& p = c.P();
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    const float s = std::clamp(std::min(r.w, r.h) * 0.22f, 2.5f, 5.f);  // 印の半分の大きさ
    constexpr float kW = 1.6f;
    switch (icon) {
        case Icon::Close:
            p.Line(cx - s, cy - s, cx + s, cy + s, color, kW);
            p.Line(cx - s, cy + s, cx + s, cy - s, color, kW);
            break;
        case Icon::Dots:
            for (int i = -1; i <= 1; ++i) p.FillCircle(cx + static_cast<float>(i) * s * 1.25f, cy, 1.6f, color);
            break;
        case Icon::DotsVertical:
            for (int i = -1; i <= 1; ++i) p.FillCircle(cx, cy + static_cast<float>(i) * s * 1.25f, 1.6f, color);
            break;
        case Icon::ChevronDown:
            p.Line(cx - s, cy - s * 0.5f, cx, cy + s * 0.5f, color, kW);
            p.Line(cx, cy + s * 0.5f, cx + s, cy - s * 0.5f, color, kW);
            break;
        case Icon::ChevronUp:
            p.Line(cx - s, cy + s * 0.5f, cx, cy - s * 0.5f, color, kW);
            p.Line(cx, cy - s * 0.5f, cx + s, cy + s * 0.5f, color, kW);
            break;
        case Icon::ChevronRight:
            p.Line(cx - s * 0.5f, cy - s, cx + s * 0.5f, cy, color, kW);
            p.Line(cx + s * 0.5f, cy, cx - s * 0.5f, cy + s, color, kW);
            break;
        case Icon::ChevronLeft:
            p.Line(cx + s * 0.5f, cy - s, cx - s * 0.5f, cy, color, kW);
            p.Line(cx - s * 0.5f, cy, cx + s * 0.5f, cy + s, color, kW);
            break;
        case Icon::Dash:  // 「一部はいい、一部はだめ」 (まとめて編集しているもののチェックボックス)
            p.Line(cx - s, cy, cx + s, cy, color, 2.f);
            break;
        case Icon::Check:
            p.Line(cx - s, cy, cx - s * 0.3f, cy + s * 0.7f, color, 2.f);
            p.Line(cx - s * 0.3f, cy + s * 0.7f, cx + s, cy - s * 0.8f, color, 2.f);
            break;
        case Icon::Plus:
            p.Line(cx - s, cy, cx + s, cy, color, kW);
            p.Line(cx, cy - s, cx, cy + s, color, kW);
            break;
        case Icon::Grip:  // つかんでドラッグするもの: 短い線が 3 本
            for (int i = -1; i <= 1; ++i) p.Line(cx - s, cy + static_cast<float>(i) * s * 0.75f, cx + s, cy + static_cast<float>(i) * s * 0.75f, color, 1.3f);
            break;
        case Icon::Menu:  // 隠した一覧を出す: 長めの線が 3 本
            for (int i = -1; i <= 1; ++i) p.Line(cx - s * 1.3f, cy + static_cast<float>(i) * s, cx + s * 1.3f, cy + static_cast<float>(i) * s, color, kW);
            break;
        case Icon::Object: {
            const float rr = std::min(r.w, r.h) * 0.27f;
            p.StrokeRoundRect({cx - rr, cy - rr, rr * 2, rr * 2}, rr, color, 1.3f);
            break;
        }
        case Icon::Play: {  // 右向きの三角
            ui::Path tri;
            tri.Begin({cx - s * 0.8f, cy - s * 1.1f});
            tri.Line({cx + s * 1.1f, cy});
            tri.Line({cx - s * 0.8f, cy + s * 1.1f});
            p.FillPath(tri, color);
            break;
        }
        case Icon::Pause:  // 2 本の棒
            p.FillRect({cx - s * 0.95f, cy - s * 1.05f, s * 0.7f, s * 2.1f}, color);
            p.FillRect({cx + s * 0.25f, cy - s * 1.05f, s * 0.7f, s * 2.1f}, color);
            break;
        case Icon::Step: {  // 小さめの三角と棒: 1 フレーム進める
            ui::Path tri;
            tri.Begin({cx - s * 1.0f, cy - s * 1.0f});
            tri.Line({cx + s * 0.5f, cy});
            tri.Line({cx - s * 1.0f, cy + s * 1.0f});
            p.FillPath(tri, color);
            p.FillRect({cx + s * 0.6f, cy - s * 1.0f, s * 0.5f, s * 2.0f}, color);
            break;
        }
        case Icon::Stop:  // 四角
            p.FillRect({cx - s * 0.9f, cy - s * 0.9f, s * 1.8f, s * 1.8f}, color);
            break;
        case Icon::Folder: {
            // 横から見たフォルダー: 本体と、左上のタブ。
            const float h = std::min(r.w, r.h) * 0.34f;
            const float x0 = cx - h * 1.1f, x1 = cx + h * 1.1f, y0 = cy - h * 0.55f, y1 = cy + h * 0.8f;
            p.StrokeRoundRect({x0, y0, x1 - x0, y1 - y0}, 1.5f, color, 1.3f);
            p.Line(x0, y0 + 0.5f, x0 + h * 0.15f, y0 - h * 0.35f, color, 1.3f);
            p.Line(x0 + h * 0.15f, y0 - h * 0.35f, x0 + h * 0.85f, y0 - h * 0.35f, color, 1.3f);
            p.Line(x0 + h * 0.85f, y0 - h * 0.35f, x0 + h * 1.05f, y0, color, 1.3f);
            break;
        }
        case Icon::File:
        case Icon::Text: {
            // 右上の角が折れたページ (+ 文字の行)。
            const float h = std::min(r.w, r.h) * 0.36f;
            const float x0 = cx - h * 0.72f, x1 = cx + h * 0.72f, y0 = cy - h, y1 = cy + h, f = h * 0.5f;
            p.Line(x0, y0, x1 - f, y0, color, 1.3f);
            p.Line(x1 - f, y0, x1, y0 + f, color, 1.3f);
            p.Line(x1, y0 + f, x1, y1, color, 1.3f);
            p.Line(x1, y1, x0, y1, color, 1.3f);
            p.Line(x0, y1, x0, y0, color, 1.3f);
            p.Line(x1 - f, y0, x1 - f, y0 + f, color, 1.1f);
            p.Line(x1 - f, y0 + f, x1, y0 + f, color, 1.1f);
            if (icon == Icon::Text) {
                for (int i = 0; i < 3; ++i) {
                    const float ly = cy - h * 0.05f + static_cast<float>(i) * h * 0.38f;
                    p.Line(x0 + h * 0.3f, ly, x1 - h * 0.3f, ly, color, 1.1f);
                }
            }
            break;
        }
        case Icon::Image: {
            // 絵: 枠と、山と、太陽。
            const float h = std::min(r.w, r.h) * 0.36f;
            const float x0 = cx - h * 1.05f, x1 = cx + h * 1.05f, y0 = cy - h * 0.8f, y1 = cy + h * 0.8f;
            p.StrokeRoundRect({x0, y0, x1 - x0, y1 - y0}, 1.5f, color, 1.3f);
            p.Line(x0 + h * 0.2f, y1 - h * 0.2f, cx - h * 0.25f, cy - h * 0.05f, color, 1.3f);
            p.Line(cx - h * 0.25f, cy - h * 0.05f, cx + h * 0.2f, y1 - h * 0.55f, color, 1.3f);
            p.Line(cx + h * 0.2f, y1 - h * 0.55f, cx + h * 0.45f, cy + h * 0.15f, color, 1.3f);
            p.Line(cx + h * 0.45f, cy + h * 0.15f, x1 - h * 0.2f, y1 - h * 0.2f, color, 1.3f);
            p.FillCircle(cx + h * 0.5f, cy - h * 0.35f, h * 0.17f, color);
            break;
        }
        case Icon::Audio: {
            // 音符: 頭と、棒と、旗。
            const float h = std::min(r.w, r.h) * 0.36f;
            const float sx = cx + h * 0.3f;
            p.FillCircle(cx - h * 0.05f, cy + h * 0.62f, h * 0.36f, color);
            p.Line(sx, cy + h * 0.6f, sx, cy - h, color, 1.3f);
            p.Line(sx, cy - h, sx + h * 0.55f, cy - h * 0.55f, color, 1.3f);
            break;
        }
        case Icon::Font: {
            // 文字の A。
            const float h = std::min(r.w, r.h) * 0.36f;
            p.Line(cx - h * 0.75f, cy + h, cx, cy - h, color, 1.4f);
            p.Line(cx, cy - h, cx + h * 0.75f, cy + h, color, 1.4f);
            p.Line(cx - h * 0.4f, cy + h * 0.25f, cx + h * 0.4f, cy + h * 0.25f, color, 1.3f);
            break;
        }
        case Icon::Camera: {
            // 横から見たフィルム用カメラ: 本体と、その右のレンズの円錐。
            const float h = std::min(r.w, r.h) * 0.36f;
            const float bx = cx - h * 1.0f, bw = h * 1.25f, by = cy - h * 0.55f, bh = h * 1.1f;
            p.StrokeRoundRect({bx, by, bw, bh}, h * 0.15f, color, 1.3f);
            ui::Path lens;
            lens.Begin({bx + bw, cy - h * 0.18f});
            lens.Line({cx + h * 0.95f, cy - h * 0.55f});
            lens.Line({cx + h * 0.95f, cy + h * 0.55f});
            lens.Line({bx + bw, cy + h * 0.18f});
            p.FillPath(lens, color);
            break;
        }
        case Icon::Light: {
            // 太陽: 丸い中心と、短い光が 8 本。
            const float h = std::min(r.w, r.h) * 0.38f;
            p.FillCircle(cx, cy, h * 0.42f, color);
            for (int i = 0; i < 8; ++i) {
                const float a = static_cast<float>(i) * 0.785398f;
                const float ca = std::cos(a), sa = std::sin(a);
                p.Line(cx + ca * h * 0.66f, cy + sa * h * 0.66f, cx + ca * h, cy + sa * h, color, 1.3f);
            }
            break;
        }
        case Icon::Point: {
            // 場所だけで他には何もない印: 輪と、その中の点と、まわりの短い目盛り 4 つ (照準)。
            const float h = std::min(r.w, r.h) * 0.38f, ring = h * 0.62f;
            p.StrokeRoundRect({cx - ring, cy - ring, ring * 2, ring * 2}, ring, color, 1.3f);
            p.FillCircle(cx, cy, h * 0.16f, color);
            p.Line(cx, cy - h, cx, cy - ring, color, 1.3f);
            p.Line(cx, cy + ring, cx, cy + h, color, 1.3f);
            p.Line(cx - h, cy, cx - ring, cy, color, 1.3f);
            p.Line(cx + ring, cy, cx + h, cy, color, 1.3f);
            break;
        }
        case Icon::Scene: {
            // シーン: 2 枚の層 (斜めから見た平らな四角) が、重なっている。
            const float h = std::min(r.w, r.h) * 0.36f;
            for (const float dy : {-h * 0.3f, h * 0.3f}) {
                const float x[4] = {cx, cx + h, cx, cx - h};
                const float y[4] = {cy + dy - h * 0.5f, cy + dy, cy + dy + h * 0.5f, cy + dy};
                for (int i = 0; i < 4; ++i) p.Line(x[i], y[i], x[(i + 1) % 4], y[(i + 1) % 4], color, 1.3f);
            }
            break;
        }
        case Icon::Effect: {
            // きらめき: 大きな 4 つ角の星 (辺がへこんでいる) と、右上の小さな星。
            auto star = [&](float sx, float sy, float big, float waist) {
                ui::Path s;
                s.Begin({sx, sy - big});
                s.Line({sx + waist, sy - waist});
                s.Line({sx + big, sy});
                s.Line({sx + waist, sy + waist});
                s.Line({sx, sy + big});
                s.Line({sx - waist, sy + waist});
                s.Line({sx - big, sy});
                s.Line({sx - waist, sy - waist});
                p.FillPath(s, color);
            };
            const float h = std::min(r.w, r.h) * 0.38f;
            star(cx - h * 0.2f, cy + h * 0.15f, h * 0.85f, h * 0.2f);
            star(cx + h * 0.65f, cy - h * 0.6f, h * 0.38f, h * 0.1f);
            break;
        }
        case Icon::Square: {
            // 平面の上のもの: 少し回した四角 (UI の箱ではない)。
            const float h = std::min(r.w, r.h) * 0.36f;
            const float x[4] = {cx, cx + h, cx, cx - h};
            const float y[4] = {cy - h, cy, cy + h, cy};
            for (int i = 0; i < 4; ++i) p.Line(x[i], y[i], x[(i + 1) % 4], y[(i + 1) % 4], color, 1.3f);
            p.FillCircle(cx, cy, h * 0.18f, color);
            break;
        }
        case Icon::Cube: {
            // 角の上から見た形: 輪郭 (六角形) と、手前の角の 3 本の辺。
            const float h = std::min(r.w, r.h) * 0.36f, k = h * 0.866f;
            const float x[6] = {cx, cx + k, cx + k, cx, cx - k, cx - k};
            const float y[6] = {cy - h, cy - h * 0.5f, cy + h * 0.5f, cy + h, cy + h * 0.5f, cy - h * 0.5f};
            for (int i = 0; i < 6; ++i) p.Line(x[i], y[i], x[(i + 1) % 6], y[(i + 1) % 6], color, 1.3f);
            p.Line(cx, cy, x[1], y[1], color, 1.3f);
            p.Line(cx, cy, x[5], y[5], color, 1.3f);
            p.Line(cx, cy, x[3], y[3], color, 1.3f);
            break;
        }
    }
}

bool Button(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view label, ButtonKind kind, bool enabled) {
    auto& p = c.P();
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;

    Color fg = theme::Text;
    switch (kind) {
        case ButtonKind::Primary:
            DrawSurface(c, r, down ? Surface::AccentPressed : b.hovered ? Surface::AccentHovered : Surface::Accent);
            fg = theme::OnAccent();
            break;
        case ButtonKind::Secondary:
            DrawSurface(c, r, down ? Surface::Pressed : b.hovered ? Surface::Hovered : Surface::Raised);
            break;
        case ButtonKind::Ghost:
            if (b.hovered) DrawSurface(c, r, down ? Surface::Pressed : Surface::Hovered);
            fg = b.hovered ? theme::Text : theme::TextDim;
            break;
    }
    if (!enabled) {
        // 無効なボタンは同じ箱で、背景と混ぜて薄くしたもの。
        p.FillRoundRect(r, theme::Radius, theme::Bg.Alpha(0.55f));
        fg = theme::TextFaint;
    }
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.bold = kind == ButtonKind::Primary;
    ts.h = ui::HAlign::Center;
    ts.color = fg;
    p.Text(r.Inset(6, down ? 1.f : 0.f, 6, down ? -1.f : 0.f), label, ts);
    return enabled && b.clicked;
}

// Button と Segmented の文字の大きさと、その左右の余白 (描くのと幅を測るので同じ値)。
constexpr float kButtonPad = 10.f, kSegmentText = 12.f, kSegmentPad = 8.f;

float ButtonWidth(ui::Context& c, std::wstring_view label) { return std::ceil(c.P().TextWidth(label, theme::FontSize, true)) + 2 * kButtonPad; }

float SegmentedWidth(ui::Context& c, std::span<const std::wstring_view> labels) {
    float widest = 0;
    for (const std::wstring_view l : labels) widest = std::max(widest, c.P().TextWidth(l, kSegmentText, true));
    return (std::ceil(widest) + 2 * kSegmentPad) * static_cast<float>(labels.size()) + 4.f;
}

int Segmented(ui::Context& c, std::string_view idStr, const Rect& r, std::span<const std::wstring_view> labels, int selected,
              std::span<const std::wstring_view> tooltips) {
    if (labels.empty()) return -1;
    DrawSurface(c, r, Surface::Popup);
    const float w = (r.w - 4.f) / static_cast<float>(labels.size());
    int clicked = -1;
    for (size_t i = 0; i < labels.size(); ++i) {
        const Rect seg{r.x + 2.f + w * static_cast<float>(i), r.y + 2.f, w, r.h - 4.f};
        const auto b = c.ButtonBehavior(c.MakeId(std::string(idStr) + "#" + std::to_string(i)), seg);
        const bool on = static_cast<int>(i) == selected;
        const bool down = b.held && b.hovered;
        if (on) {
            DrawSurface(c, seg, down ? Surface::AccentPressed : b.hovered ? Surface::AccentHovered : Surface::Accent, theme::Radius - 1.f);
        } else if (b.hovered) {
            DrawSurface(c, seg, down ? Surface::Pressed : Surface::Hovered, theme::Radius - 1.f);
        }
        ui::TextStyle ts;
        ts.size = kSegmentText;
        ts.bold = on;
        ts.h = ui::HAlign::Center;
        ts.color = on ? theme::OnAccent() : b.hovered ? theme::Text : theme::TextDim;
        c.P().Text(seg.Inset(4, 0, 4, 0), labels[i], ts);
        if (b.hovered && i < tooltips.size() && !tooltips[i].empty()) Tooltip(c, tooltips[i]);
        if (b.clicked) clicked = static_cast<int>(i);
    }
    return clicked;
}

void ProgressBar(ui::Context& c, const Rect& r, float fraction) {
    const float f = std::clamp(fraction, 0.f, 1.f);
    const float radius = std::min(r.h * 0.5f, theme::Radius);
    DrawSurface(c, r, Surface::Sunken, radius);
    if (f > 0.f) DrawSurface(c, {r.x, r.y, std::max(r.h, r.w * f), r.h}, Surface::Accent, radius);
}

namespace {
void InfoRowIn(ui::Context& c, const Rect& r, std::wstring_view label, std::wstring_view value, float labelWidth, ui::Color valueColor) {
    ui::TextStyle ls;
    ls.size = 12;
    ls.color = theme::TextDim;
    ls.v = ui::VAlign::Center;
    const float lw = std::min(labelWidth, r.w * 0.5f);
    c.P().Text({r.x, r.y, lw - 6, r.h}, label, ls);
    ui::TextStyle vs = ls;
    vs.color = valueColor;
    c.P().Text({r.x + lw, r.y, r.w - lw, r.h}, value, vs);
}
}  // namespace

void InfoRow(ui::Context& c, const Rect& r, std::wstring_view label, std::wstring_view value, float labelWidth, bool lit) {
    InfoRowIn(c, r, label, value, labelWidth, lit ? theme::Accent : theme::Text);
}

void InfoRowDanger(ui::Context& c, const Rect& r, std::wstring_view label, std::wstring_view value, float labelWidth) {
    InfoRowIn(c, r, label, value, labelWidth, theme::Danger);
}

bool FolderTab(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view label, bool selected, float radius) {
    const auto b = c.ButtonBehavior(c.MakeId(idStr), r);
    auto& p = c.P();
    const ui::Corners top{radius, radius, 0, 0};
    if (selected) {
        // シート自身の塗りを、少し下まで伸ばす (つなぎ目に線が出ないように)。
        const theme::Gradient fill = theme::Gentle(theme::Panel);
        const Rect joined{r.x, r.y, r.w, r.h + 2};
        p.StrokeRoundRectGradient(joined.Inset(-1, -1, -1, 0), {radius + 1, radius + 1, 0, 0}, theme::EdgeOuter(), theme::EdgeOuter());
        p.FillRoundRectGradient(joined, top, fill.top, fill.top);
        p.StrokeRoundRectGradient(joined, top, theme::EdgeInner().top, fill.top);
        p.FillRect({r.x + 1, r.Bottom() - 1, r.w - 2, 4}, fill.top);
    } else {
        const Color base = b.hovered ? theme::Hover : theme::Sidebar;
        const theme::Gradient fill = theme::Gentle(base);
        p.FillRoundRectGradient(r, top, fill.top, fill.bottom);
        p.StrokeRoundRectGradient(r, top, theme::Border, theme::Border);
    }
    ui::TextStyle ts;
    ts.size = 12;
    ts.bold = selected;
    ts.color = selected ? theme::Text : b.hovered ? theme::Text : theme::TextDim;
    ts.h = ui::HAlign::Center;
    ts.v = ui::VAlign::Center;
    p.Text(r.Inset(6, 2, 6, 0), label, ts);
    return b.clicked;
}

void DrawFolderSheet(ui::Context& c, const Rect& r, float radius, bool squareTopLeft) {
    DrawPanelFrame(c, r, {squareTopLeft ? 0.f : radius, radius, radius, radius});
}

void LineGraph(ui::Context& c, const Rect& r, std::span<const float> a, std::span<const float> b, float top, std::span<const float> guides) {
    DrawSurface(c, r, Surface::Sunken, 4.f);
    if (top <= 0 || r.w < 4 || r.h < 4) return;
    const Rect in = r.Inset(2, 3, 2, 3);
    auto yOf = [&](float v) { return in.Bottom() - std::clamp(v / top, 0.f, 1.f) * in.h; };
    for (const float g : guides) {
        if (g > 0 && g < top) c.P().Line(in.x, yOf(g), in.Right(), yOf(g), theme::TextFaint.Alpha(0.5f), 1.f);
    }
    auto series = [&](std::span<const float> v, ui::Color color) {
        if (v.size() < 2) return;
        const float step = in.w / static_cast<float>(v.size() - 1);
        for (size_t i = 1; i < v.size(); ++i) {
            c.P().Line(in.x + step * static_cast<float>(i - 1), yOf(v[i - 1]), in.x + step * static_cast<float>(i), yOf(v[i]), color, 1.5f);
        }
    };
    series(b, theme::Warn);
    series(a, theme::Accent);
}

bool IconButton(ui::Context& c, std::string_view idStr, const Rect& r, Icon icon, bool enabled) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;
    if (b.hovered) DrawSurface(c, r, down ? Surface::Pressed : Surface::Hovered);
    DrawIcon(c, r, icon, !enabled ? theme::TextFaint : b.hovered ? theme::Text : theme::TextDim);
    return enabled && b.clicked;
}

bool IconToggle(ui::Context& c, std::string_view idStr, const Rect& r, Icon icon, bool on, bool enabled, std::wstring_view tooltip) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;
    if (on) {
        DrawSurface(c, r, down ? Surface::AccentPressed : b.hovered ? Surface::AccentHovered : Surface::Accent);
    } else {
        DrawSurface(c, r, down ? Surface::Pressed : b.hovered ? Surface::Hovered : Surface::Raised);
    }
    DrawIcon(c, r, icon, on ? theme::OnAccent() : !enabled ? theme::TextFaint : b.hovered ? theme::Text : theme::TextDim);
    if (b.hovered && !tooltip.empty()) Tooltip(c, tooltip);
    return enabled && b.clicked;
}

ui::Context::ButtonResult ToolSquare(ui::Context& c, std::string_view idStr, const Rect& r, Icon icon, bool on,
                                     std::wstring_view tooltip) {
    const ui::Context::ButtonResult b = c.ButtonBehavior(c.MakeId(idStr), r);
    const bool down = b.held && b.hovered;
    if (on) {
        DrawSurface(c, r, down ? Surface::AccentPressed : b.hovered ? Surface::AccentHovered : Surface::Accent);
    } else {
        DrawSurface(c, r, down ? Surface::Pressed : b.hovered ? Surface::Hovered : Surface::Raised);
    }
    DrawIcon(c, r, icon, on ? theme::OnAccent() : b.hovered ? theme::Text : theme::TextDim, std::round(std::min(r.w, r.h) * 0.8f));
    if (b.hovered && !tooltip.empty()) Tooltip(c, tooltip);
    return b;
}

void DrawRow(ui::Context& c, const Rect& r, RowLook look) {
    auto& p = c.P();
    const float radius = look.flat ? 0.f : 5.f;
    if (look.selected) {
        const theme::Gradient g = theme::Raised(look.focused ? theme::Selected : theme::Pressed);
        p.FillRoundRectGradient(r, radius, g.top, g.bottom);
        TopLight(p, r, radius, theme::Highlight());
        if (look.focused) {
            // アクセントの棒: 平らな行では端に、角の丸い行では内側に。
            const theme::Gradient a = theme::AccentOf(theme::Accent);
            const Rect bar = look.flat ? Rect{r.x, r.y, 3, r.h} : Rect{r.x, r.y + 4, 3, r.h - 8};
            p.FillRoundRectGradient(bar, look.flat ? 0.f : 1.5f, a.top, a.bottom);
        }
    } else if (look.hovered) {
        const theme::Gradient g = theme::Gentle(theme::Hover);
        p.FillRoundRectGradient(r, radius, g.top, g.bottom);
    }
}

void DrawTile(ui::Context& c, const Rect& r, bool selected, bool hovered, bool focused) {
    auto& p = c.P();
    constexpr float kRadius = 6.f;
    if (selected) {
        const theme::Gradient g = theme::Raised(focused ? theme::Selected : theme::Pressed);
        p.FillRoundRectGradient(r, kRadius, g.top, g.bottom);
        TopLight(p, r, kRadius, theme::Highlight());
        if (focused) p.StrokeRoundRect(r.Inset(0.5f), kRadius, theme::Accent.Alpha(0.8f), 1.f);
    } else if (hovered) {
        const theme::Gradient g = theme::Gentle(theme::Hover);
        p.FillRoundRectGradient(r, kRadius, g.top, g.bottom);
    }
}

void DragTag(ui::Context& c, std::wstring text, bool inside) {
    c.AddOverlay([&c, tag = std::move(text), inside] {
        // (フレームの終わりに: あとに描く参照フィールドが、受け取るかどうかを答えてから)
        const bool ok = TakeDragAccepted() || inside;
        auto& pp = c.P();
        const float wdt = std::min(260.f, pp.TextWidth(tag, 12) + 20);
        const Rect r{c.In().mouseX + 14, c.In().mouseY + 10, wdt, 24};
        DrawSurface(c, r, Surface::Popup, 5.f);
        if (ok) pp.StrokeRoundRect(r, 5, theme::Accent);
        ui::TextStyle s;
        s.size = 12;
        s.color = ok ? theme::Text : theme::TextDim;
        pp.Text(r.Inset(10, 0, 10, 0), tag, s);
    });
}

ui::Context::ButtonResult Selectable(ui::Context& c, std::string_view idStr, const Rect& r, bool selected, bool flat) {
    const auto b = c.ButtonBehavior(c.MakeId(idStr), r);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    RowLook look;
    look.selected = selected;
    look.hovered = b.hovered;
    look.flat = flat;
    DrawRow(c, r, look);
    return b;
}

namespace {
// パレット自身の色で描いた小さなエディターのウィンドウ (使用中のパレットではない): タイトルバー、
// 選んだ項目のあるサイドバー、文字の行とアクセントのボタンのあるパネル。
void ThemePreview(ui::Painter& p, const Rect& r, const theme::Palette& k) {
    constexpr float R = 5.f, barH = 12.f;
    p.FillRoundRect(r, R, k.bg);
    p.FillRoundRectGradient({r.x, r.y, r.w, barH}, ui::Corners{R, R, 0, 0}, k.sidebar, k.sidebar);
    const float sideW = std::round(r.w * 0.26f);
    const Rect side{r.x, r.y + barH, sideW, r.h - barH};
    p.FillRoundRectGradient(side, ui::Corners{0, 0, 0, R}, k.sidebar, k.sidebar);
    float sy = side.y + 10;
    for (int i = 0; i < 3; ++i) {
        const Rect item{side.x + 6, sy, side.w - 12, 9};
        if (i == 1) {
            p.FillRoundRect(item, 2, k.selected);
            p.FillRect({item.x, item.y, 2, item.h}, k.accent);
        }
        p.FillRoundRect({item.x + 5, item.y + 3, (item.w - 10) * (i == 1 ? 0.8f : 0.6f), 3}, 1.5f, i == 1 ? k.text : k.textDim);
        sy += 15;
    }
    const Rect panel{side.Right() + 8, r.y + barH + 8, r.Right() - side.Right() - 16, r.h - barH - 16};
    if (panel.w <= 0 || panel.h <= 0) return;
    p.FillRoundRect(panel, 4, k.panel);
    p.StrokeRoundRect(panel, 4, k.border);
    p.FillRoundRect({panel.x + 8, panel.y + 9, panel.w * 0.55f, 4}, 2, k.text);
    p.FillRoundRect({panel.x + 8, panel.y + 19, panel.w * 0.42f, 3}, 1.5f, k.textDim);
    p.FillRoundRect({panel.x + 8, panel.y + 27, panel.w * 0.34f, 3}, 1.5f, k.textFaint);
    p.FillRoundRect({panel.x + 8, panel.Bottom() - 18, std::min(44.f, panel.w - 16), 11}, 3, k.accent);
}
}  // namespace

ui::Context::ButtonResult ThemeCard(ui::Context& c, std::string_view idStr, const Rect& r, const theme::Palette* palette,
                                    std::wstring_view name, bool selected) {
    auto& p = c.P();
    const auto b = c.ButtonBehavior(c.MakeId(idStr), r);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    DrawSurface(c, r, b.hovered && !selected ? Surface::Hovered : Surface::Panel, 8.f);
    const Rect pr{r.x + 10, r.y + 10, r.w - 20, r.h - 20 - 30};
    if (palette) {
        ThemePreview(p, pr, *palette);
    } else {
        // Windows に従う: 左が暗く、右が明るい。
        const float half = std::round(pr.w * 0.5f);
        p.PushClip({pr.x, pr.y, half, pr.h});
        ThemePreview(p, pr, theme::kDark);
        p.PopClip();
        p.PushClip({pr.x + half, pr.y, pr.w - half, pr.h});
        ThemePreview(p, pr, theme::kLight);
        p.PopClip();
    }
    ui::TextStyle ts;
    ts.size = 13;
    ts.bold = selected;
    ts.color = theme::Text;
    const Rect label{r.x + 12, r.Bottom() - 34, r.w - 24 - 20, 28};
    p.Text(label, name, ts);
    if (selected) {
        DrawIcon(c, {r.Right() - 12 - 16, label.y + 6, 16, 16}, Icon::Check, theme::Accent);
        p.StrokeRoundRect(r.Inset(-1), 9.f, theme::Accent, 2.f);
    }
    return b;
}

bool NavItem(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view label, bool selected) {
    const auto b = Selectable(c, idStr, r, selected, true);
    ui::TextStyle ts;
    ts.size = 15;
    ts.bold = selected;
    ts.color = selected || b.hovered ? theme::Text : theme::TextDim;
    c.P().Text(r.Inset(24, 0, 12, 0), label, ts);
    return b.clicked;
}

void DrawTab(ui::Context& c, const Rect& r, bool selected, bool hovered) {
    auto& p = c.P();
    if (selected) {
        const theme::Gradient g = theme::Raised(theme::PanelHeader);
        p.FillRoundRectGradient(r, theme::Radius, g.top, g.bottom);
        TopLight(p, r, theme::Radius, theme::Highlight());
        const theme::Gradient a = theme::AccentOf(theme::Accent);
        p.FillRoundRectGradient({r.x + 8, r.Bottom() - 2.5f, r.w - 16, 2.5f}, 1.25f, a.top, a.bottom);
    } else if (hovered) {
        const theme::Gradient g = theme::Gentle(theme::Hover);
        p.FillRoundRectGradient(r, theme::Radius, g.top, g.bottom);
    }
}

bool CollapsingHeader(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view title, bool& open, float reserveRight) {
    const Rect hit{r.x, r.y, r.w - reserveRight, r.h};
    const auto b = c.ButtonBehavior(c.MakeId(idStr), hit);
    DrawSurface(c, r, b.held && b.hovered ? Surface::Pressed : b.hovered ? Surface::Hovered : Surface::Raised, 5.f);
    DrawIcon(c, {r.x + 4, r.y, 18, r.h}, open ? Icon::ChevronDown : Icon::ChevronRight, b.hovered ? theme::Text : theme::TextDim);
    ui::TextStyle ts;
    ts.size = 13;
    ts.bold = true;
    ts.color = theme::Text;
    c.P().Text({r.x + 26, r.y, r.w - 26 - reserveRight - 6, r.h}, title, ts);
    if (b.clicked) {
        open = !open;
        return true;
    }
    return false;
}

ui::Context::ButtonResult ComboBox(ui::Context& c, std::string_view idStr, const Rect& r, std::wstring_view text, bool enabled,
                                   std::optional<Icon> icon) {
    ui::Context::ButtonResult b;
    if (enabled) b = c.ButtonBehavior(c.MakeId(idStr), r);
    DrawSurface(c, r, b.hovered ? Surface::Hovered : Surface::Raised);
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.color = enabled ? theme::Text : theme::TextFaint;
    float left = 8;
    if (icon) {
        DrawIcon(c, {r.x + 5, r.y, 18, r.h}, *icon, *icon == Icon::Folder ? theme::Folder : theme::TextDim);
        left = 26;
    }
    c.P().Text(r.Inset(left, 0, 24, 0), text, ts);
    DrawIcon(c, {r.Right() - 22, r.y, 18, r.h}, Icon::ChevronDown, b.hovered ? theme::Text : theme::TextDim);
    return b;
}

void Separator(ui::Context& c, float x, float y, float width) { c.P().FillRect({x, y, width, 1}, theme::Border); }
void VSeparator(ui::Context& c, float x, float y, float height) { c.P().FillRect({x, y, 1, height}, theme::Border); }

void Tooltip(ui::Context& c, std::wstring_view text) {
    const std::wstring tip(text);
    c.AddOverlay([&c, tip] {
        auto& p = c.P();
        const float wdt = std::min(420.f, p.TextWidth(tip, 12) + 20);
        const Rect r{c.In().mouseX + 14, c.In().mouseY + 18, wdt, 28};
        DrawSurface(c, r, Surface::Popup, 5.f);
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::Text;
        p.Text(r.Inset(10, 0, 10, 0), tip, ts);
    });
}

bool TextField(ui::Context& c, std::string_view idStr, const Rect& r, std::string& value, std::wstring_view placeholder,
               bool enabled, bool selectAllOnFocus) {
    auto& p = c.P();
    const auto& in = c.In();
    const ui::Id id = c.MakeId(idStr);
    const float fs = theme::FontSize;
    const Rect inner = r.Inset(8, 0, 8, 0);
    float& scroll = c.FloatState(id ^ 0x5C5C5C5C5C5C5C5Cull);

    const bool hovered = enabled && c.IsHovered(r);
    if (hovered) c.SetCursor(ui::Cursor::IBeam);
    bool focused = c.Focused() == id;
    if (focused && (!c.InputEnabled() || !enabled)) {
        c.ClearFocus();  // このフィールドの上に開いたオーバーレイ
        focused = false;
    }

    // 1) 入力した文字が先: 同じフレームでフォーカスを外す
    //    クリックがあっても、その直前に入力した (または IME が確定した) 文字を落とさない。
    bool changed = false;
    if (focused) {
        auto& e = c.FocusedEdit();
        bool edited = false;
        for (const auto& k : in.keys) {
            switch (k.key) {
                case ui::Key::Left: e.MoveLeft(k.shift, k.ctrl); break;
                case ui::Key::Right: e.MoveRight(k.shift, k.ctrl); break;
                case ui::Key::Home: e.Home(k.shift); break;
                case ui::Key::End: e.End(k.shift); break;
                case ui::Key::Backspace: edited |= e.Backspace(k.ctrl); break;
                case ui::Key::Delete: edited |= e.Delete(k.ctrl); break;
                case ui::Key::A:
                    if (k.ctrl) e.SelectAll();
                    break;
                case ui::Key::C:
                    if (k.ctrl && e.HasSelection() && c.setClipboardText) c.setClipboardText(e.SelectedText());
                    break;
                case ui::Key::X:
                    if (k.ctrl && e.HasSelection()) {
                        if (c.setClipboardText) c.setClipboardText(e.SelectedText());
                        edited |= e.Insert(L"");
                    }
                    break;
                case ui::Key::V:
                    if (k.ctrl && c.getClipboardText) edited |= e.Insert(c.getClipboardText());
                    break;
                default: break;
            }
            c.ResetCaretBlink();
        }
        if (!in.chars.empty()) edited |= e.Insert(in.chars);
        if (!in.imeResult.empty()) edited |= e.Insert(in.imeResult);
        if (edited) {
            c.ResetCaretBlink();
            std::string nv = WideToUtf8(e.Text());
            if (nv != value) {
                value = std::move(nv);
                changed = true;
            }
        }
    }

    // 2) クリック: フォーカス / キャレットの位置、またはフォーカスを外す。フィールドで押した後に
    //    左ボタンを押している間、選択はマウスに付いていく (押した所から)。
    //    drag: 0 = なし、1 = 選択中、2 = フィールドに入るときに押した (マウスが動かなければ
    //    離したときに全部選ぶ: selectAllOnFocus)。
    float& drag = c.FloatState(id ^ 0xD1A6D1A6D1A6D1A6ull);
    float& pressX = c.FloatState(id ^ 0x9E55E59E55E59E55ull);
    if (hovered && c.MousePressed()) {
        const bool entering = !focused;
        if (!focused) {
            c.FocusText(id, Utf8ToWide(value), false);
            focused = true;
        }
        auto& e = c.FocusedEdit();
        e.SetCaret(p.HitTest(e.Text(), in.mouseX - inner.x + scroll, fs), in.shift);
        if (in.doubleClicked) e.SelectAll();
        drag = entering && selectAllOnFocus && !in.shift && !in.doubleClicked ? 2.f : in.doubleClicked ? 0.f : 1.f;
        pressX = in.mouseX;
        if (drag == 2.f) e.SelectAll();  // すぐ表示する。ドラッグすると、覆った範囲に置き換わる
        c.ResetCaretBlink();
    } else if (focused && !c.FocusIsNew() && (in.pressed[0] || in.pressed[1]) && !r.Contains(in.mouseX, in.mouseY)) {
        c.ClearFocus();
        focused = false;
        drag = 0;
    } else if (focused && drag != 0 && in.down[0]) {
        if (drag == 1.f || std::fabs(in.mouseX - pressX) >= 3.f) {
            auto& e = c.FocusedEdit();
            if (drag == 2.f) e.SetCaret(p.HitTest(e.Text(), pressX - inner.x + scroll, fs), false);  // ドラッグは押した所から始まる
            drag = 1.f;
            e.SetCaret(p.HitTest(e.Text(), in.mouseX - inner.x + scroll, fs), true);
            c.ResetCaretBlink();
        }
    }
    if (drag != 0 && (!in.down[0] || !focused)) drag = 0;

    // 3) 何を描くか。
    std::wstring display;
    size_t caret = 0, selA = 0, selB = 0, compStart = 0, compLen = 0;
    if (focused) {
        c.MarkFocusAlive();
        auto& e = c.FocusedEdit();
        display = e.Text();
        caret = e.Caret();
        selA = e.SelStart();
        selB = e.SelEnd();
        if (!in.imeComposition.empty()) {
            compStart = caret;
            compLen = in.imeComposition.size();
            display.insert(caret, in.imeComposition);
            caret = compStart + std::clamp<size_t>(static_cast<size_t>(std::max(0, in.imeCursor)), 0, compLen);
            selA = selB = 0;
        }
    } else {
        display = Utf8ToWide(value);
    }

    // 枠
    if (focused) p.StrokeRoundRect(r.Inset(-2), theme::Radius + 2, theme::Accent.Alpha(0.25f), 2.f);  // やわらかい輪
    DrawSurface(c, r, Surface::Sunken);
    if (focused || hovered) p.StrokeRoundRect(r, theme::Radius, focused ? theme::Accent : theme::BorderHover);
    if (!enabled) p.FillRoundRect(r, theme::Radius, theme::Bg.Alpha(0.45f));

    // キャレットが見えるようにする。
    const float caretX = p.CaretX(display, caret, fs);
    const float textW = p.TextWidth(display, fs);
    if (focused) {
        if (caretX - scroll > inner.w - 2) scroll = caretX - inner.w + 2;
        if (caretX - scroll < 0) scroll = caretX;
        scroll = std::clamp(scroll, 0.f, std::max(0.f, textW - inner.w + 2));
    } else {
        scroll = 0;
    }

    c.PushClip(inner);
    if (display.empty()) {
        ui::TextStyle ps;
        ps.size = fs;
        ps.color = theme::TextFaint;
        p.Text(inner, placeholder, ps);
    } else {
        if (focused && selA != selB) {
            const float x0 = p.CaretX(display, selA, fs), x1 = p.CaretX(display, selB, fs);
            p.FillRect({inner.x - scroll + x0, r.y + 6, x1 - x0, r.h - 12}, theme::Accent.Alpha(0.45f));
        }
        ui::TextStyle ts;
        ts.size = fs;
        ts.ellipsis = !focused;
        ts.color = enabled ? theme::Text : theme::TextDim;
        const Rect tr = focused ? Rect{inner.x - scroll, r.y, std::max(inner.w + scroll, textW + 4), r.h} : inner;
        p.Text(tr, display, ts);
        if (compLen > 0) {
            const float x0 = p.CaretX(display, compStart, fs), x1 = p.CaretX(display, compStart + compLen, fs);
            p.FillRect({inner.x - scroll + x0, r.Bottom() - 8, x1 - x0, 1.5f}, theme::Text);
        }
    }
    if (focused) {
        const Rect cr{inner.x - scroll + caretX, r.y + 7, 1.5f, r.h - 14};
        if (c.CaretVisible()) p.FillRect(cr, theme::Text);
        c.SetTextInput(cr);
    }
    c.PopClip();
    return changed;
}

bool NumberField(ui::Context& c, std::string_view id, const Rect& r, float& value, float min, float max, std::string& text) {
    if (c.Focused() != c.MakeId(id)) text = std::format("{:g}", value);
    if (!TextField(c, id, r, text, {}, true, true)) return false;
    float v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(v)) return false;
    const float clamped = std::clamp(v, min, max);
    if (clamped == value) return false;
    value = clamped;
    return true;
}

void DrawSelectionBand(ui::Context& c, const Rect& r) {
    if (r.w <= 0 || r.h <= 0) return;
    c.P().FillRect(r, theme::Accent.Alpha(0.12f));
    c.P().StrokeRect(r, theme::Accent.Alpha(0.9f), 1.f);
}

bool Checkbox(ui::Context& c, std::string_view idStr, const Rect& r, bool& value, std::wstring_view label, bool mixed) {
    auto& p = c.P();
    const auto b = c.ButtonBehavior(c.MakeId(idStr), r);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    const Rect box{r.x, r.y + (r.h - 16) * 0.5f, 16, 16};
    const bool filled = value || mixed;
    DrawSurface(c, box, filled ? (b.hovered ? Surface::AccentHovered : Surface::Accent) : Surface::Sunken, 3.f);
    if (!filled && b.hovered) p.StrokeRoundRect(box, 3, theme::BorderHover);
    if (filled) DrawIcon(c, box, mixed ? Icon::Dash : Icon::Check, theme::OnAccent());
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.color = theme::Text;
    p.Text({box.Right() + 8, r.y, r.w - 24, r.h}, label, ts);
    if (b.clicked) {
        value = mixed ? true : !value;
        return true;
    }
    return false;
}

namespace {
constexpr float kCheckBoxW = 16.f, kCheckLabelGap = 8.f, kCountGap = 5.f;
}  // namespace

namespace {
constexpr float kCountMarkW = 14.f, kCountMarkGap = 2.f;  // 名前の前の印
}  // namespace

float CheckboxCountWidth(ui::Context& c, std::wstring_view label, std::wstring_view count, std::optional<Icon> mark) {
    float w = kCheckBoxW;
    if (count.empty()) return w + (label.empty() ? 0.f : kCheckLabelGap + c.P().TextWidth(label, theme::FontSize)) + 2.f;
    w += kCheckLabelGap;
    if (mark) w += kCountMarkW + kCountMarkGap;
    if (!label.empty()) w += c.P().TextWidth(label, theme::FontSize) + kCountGap;
    return w + c.P().TextWidth(count, theme::FontSize, true) + 2.f;
}

bool CheckboxCount(ui::Context& c, std::string_view id, const Rect& r, bool& value, std::wstring_view label,
                   std::wstring_view count, ui::Color countColor, std::optional<Icon> mark, float markAlpha) {
    if (count.empty()) return Checkbox(c, id, r, value, label);
    // 並び: [箱] 印 名前 数  (箱と押せる所は Checkbox の 1 本の道で。名前は印の後ろに自分で書く)
    const bool changed = Checkbox(c, id, r, value, {});
    float x = r.x + kCheckBoxW + kCheckLabelGap;
    if (mark) {
        DrawIcon(c, {x, r.y, kCountMarkW, r.h}, *mark, countColor.Alpha(markAlpha), 13);
        x += kCountMarkW + kCountMarkGap;
    }
    if (!label.empty()) {
        ui::TextStyle ls;
        ls.size = theme::FontSize;
        ls.color = theme::Text;
        const float lw = c.P().TextWidth(label, theme::FontSize);
        c.P().Text({x, r.y, lw + 1.f, r.h}, label, ls);
        x += lw + kCountGap;
    }
    ui::TextStyle ts;
    ts.size = theme::FontSize;
    ts.bold = true;
    ts.color = countColor;
    c.P().Text({x, r.y, std::max(0.f, r.Right() - x), r.h}, count, ts);
    return changed;
}

namespace {
constexpr float kTrackW = 40.f, kLabelGap = 8.f;
}  // namespace

float SwitchWidth(ui::Context& c, std::wstring_view offLabel, std::wstring_view onLabel) {
    float w = kTrackW;
    if (!offLabel.empty()) w += c.P().TextWidth(offLabel, theme::FontSize) + kLabelGap;
    if (!onLabel.empty()) w += c.P().TextWidth(onLabel, theme::FontSize) + kLabelGap;
    return w;
}

bool Switch(ui::Context& c, std::string_view idStr, float x, float y, bool& on, std::wstring_view offLabel, std::wstring_view onLabel) {
    auto& p = c.P();
    const ui::Id id = c.MakeId(idStr);
    const Rect all{x, y, SwitchWidth(c, offLabel, onLabel), kSwitchH};
    const auto b = c.ButtonBehavior(id, all);
    if (b.hovered) c.SetCursor(ui::Cursor::Hand);
    if (b.clicked) on = !on;

    // つまみの位置: 0 = 左 (オフ) .. 1 = 右 (オン)。数フレームかけてそこへ滑る。
    float& pos = c.FloatState(id ^ 0x5717C85717C85717ull, on ? 1.f : 0.f);
    const float target = on ? 1.f : 0.f;
    if (pos != target) {
        pos = target > pos ? std::min(target, pos + 0.2f) : std::max(target, pos - 0.2f);
        c.RequestAnimation();
    }

    ui::TextStyle ts;
    ts.size = theme::FontSize;
    float cx = x;
    if (!offLabel.empty()) {
        const float tw = p.TextWidth(offLabel, ts.size);
        ts.color = !on ? theme::Text : theme::TextFaint;
        ts.bold = !on;
        p.Text({cx, y, tw + 2, kSwitchH}, offLabel, ts);
        cx += tw + kLabelGap;
    }
    const Rect track{cx, y, kTrackW, kSwitchH};
    const float r = kSwitchH * 0.5f;
    // つまみが右へ行くにつれて、溝が灰色からアクセントの色に変わる。
    p.FillRoundRect(track, r, Mix(theme::SwitchOff(), b.hovered ? theme::AccentHover : theme::Accent, pos));
    if (pos < 1.f) p.StrokeRoundRect(track, r, theme::Border.Alpha(1.f - pos));
    const float knobR = r - 2.f;
    const float kx = track.x + r + (track.w - 2.f * r) * pos;
    p.FillCircle(kx, track.y + r + 0.8f, knobR, theme::Shadow.Alpha(0.35f));  // その下のやわらかい影
    p.FillCircle(kx, track.y + r, knobR, theme::SwitchKnob());
    cx += kTrackW + kLabelGap;
    if (!onLabel.empty()) {
        const float tw = p.TextWidth(onLabel, ts.size);
        ts.color = on ? theme::Text : theme::TextFaint;
        ts.bold = on;
        p.Text({cx, y, tw + 2, kSwitchH}, onLabel, ts);
    }
    return b.clicked;
}

void DrawThumbnail(ui::Context& c, const Rect& r, ID2D1Bitmap* bitmap, bool dimmed) {
    auto& p = c.P();
    p.FillRoundRect(r, 3, theme::ThumbnailBack());
    if (!bitmap) return;
    const D2D1_SIZE_F size = bitmap->GetSize();
    if (size.width <= 0 || size.height <= 0) return;
    const float k = std::min(r.w / size.width, r.h / size.height);
    const float w = size.width * k, h = size.height * k;
    p.Image(bitmap, {r.x + (r.w - w) * 0.5f, r.y + (r.h - h) * 0.5f, w, h}, dimmed ? 0.45f : 1.f);
}

bool Slider(ui::Context& c, std::string_view idStr, const Rect& r, float& value, float min, float max) {
    auto& p = c.P();
    const ui::Id id = c.MakeId(idStr);
    const auto b = c.ButtonBehavior(id, r);
    if (b.hovered || b.held) c.SetCursor(ui::Cursor::Hand);
    constexpr float knobR = 6.f;
    const float x0 = r.x + knobR, x1 = r.Right() - knobR;
    bool changed = false;
    if (b.held && x1 > x0 && max > min) {
        const float t = std::clamp((c.In().mouseX - x0) / (x1 - x0), 0.f, 1.f);
        const float v = min + t * (max - min);
        if (v != value) {
            value = v;
            changed = true;
        }
    }
    const float t = max > min ? std::clamp((value - min) / (max - min), 0.f, 1.f) : 0.f;
    const float cy = r.y + r.h * 0.5f;
    const float kx = x0 + (x1 - x0) * t;
    p.FillRoundRect({x0, cy - 2, x1 - x0, 4}, 2, theme::SwitchOff());
    p.FillRoundRect({x0, cy - 2, kx - x0, 4}, 2, b.hovered || b.held ? theme::AccentHover : theme::Accent);
    p.FillCircle(kx, cy + 0.8f, knobR, theme::Shadow.Alpha(0.35f));  // その下のやわらかい影
    p.FillCircle(kx, cy, knobR, theme::SwitchKnob());
    p.FillCircle(kx, cy, knobR - 3.5f, b.held ? theme::Accent : theme::SwitchOff());
    return changed;
}

Rect Panel(ui::Context& c, const Rect& r, std::wstring_view title) {
    auto& p = c.P();
    DrawSurface(c, r, Surface::Panel, 6.f);
    Rect body = r;
    const Rect header = ui::TakeTop(body, 30);
    const theme::Gradient hg = theme::Raised(theme::PanelHeader);
    p.FillRoundRectGradient(header.Inset(1, 1, 1, 0), 5.f, hg.top, hg.bottom);
    ui::TextStyle ts;
    ts.size = 12;
    ts.bold = true;
    ts.color = theme::TextDim;
    p.Text(header.Inset(12, 0, 12, 0), title, ts);
    p.FillRect({r.x, header.Bottom() - 1, r.w, 1}, theme::Border);
    return body;
}

float BeginScroll(ui::Context& c, std::string_view idStr, const Rect& view, float contentHeight) {
    float& off = c.FloatState(c.MakeId(idStr));
    const float maxOff = std::max(0.f, contentHeight - view.h);
    if (c.IsHovered(view) && c.In().wheel != 0.f) off -= c.In().wheel * 60.f;
    off = std::clamp(off, 0.f, maxOff);
    c.PushClip(view);
    return off;
}

void EndScroll(ui::Context& c, std::string_view idStr, const Rect& view, float contentHeight) {
    c.PopClip();
    if (contentHeight <= view.h) return;
    auto& p = c.P();
    const ui::Id id = c.MakeId(idStr);
    float& off = c.FloatState(id);
    const float maxOff = contentHeight - view.h;
    const Rect track{view.Right() - 9, view.y + 3, 6, view.h - 6};
    const float thumbH = std::max(28.f, track.h * view.h / contentHeight);
    const float travel = std::max(1.f, track.h - thumbH);
    const Rect thumb{track.x, track.y + travel * (off / maxOff), track.w, thumbH};

    const ui::Id tid = id ^ 0x7475786275756D74ull;
    float& grab = c.FloatState(tid);
    const auto b = c.ButtonBehavior(tid, thumb.Inset(-3));
    if (b.hovered && c.MousePressed()) grab = c.In().mouseY - thumb.y;
    if (c.Active() == tid && c.In().down[0]) {
        const float nv = std::clamp((c.In().mouseY - grab - track.y) / travel * maxOff, 0.f, maxOff);
        if (nv != off) {
            off = nv;
            c.RequestRedraw();
        }
    }
    const bool hot = b.hovered || c.Active() == tid;
    const theme::Gradient tg = theme::Raised(hot ? theme::ScrollThumbHot : theme::ScrollThumb);
    p.FillRoundRectGradient(thumb, 3, tg.top, tg.bottom);
}

void ScrollToEnd(ui::Context& c, std::string_view idStr) { c.FloatState(c.MakeId(idStr)) = 1e9f; }

bool ScrollAtEnd(ui::Context& c, std::string_view idStr, const Rect& view, float contentHeight) {
    return c.FloatState(c.MakeId(idStr)) >= std::max(0.f, contentHeight - view.h) - 1.f;
}

namespace {
constexpr float kItemH = 28.f;
constexpr float kSepH = 9.f;
constexpr float kPad = 5.f;
}  // namespace

Rect MenuRect(const ui::Context& c, const Popup& popup, const std::vector<MenuItem>& items, float width) {
    float h = kPad * 2;
    for (const auto& it : items) h += it.separator ? kSepH : kItemH;
    const Rect& vp = c.Viewport();
    h = std::min(h, std::max(kPad * 2 + kItemH, vp.h - 8));  // (ウィンドウより高い: スクロールする - MenuPopup)
    const float x = std::clamp(popup.x, vp.x + 4, std::max(vp.x + 4, vp.Right() - width - 4));
    const float y = std::clamp(popup.y, vp.y + 4, std::max(vp.y + 4, vp.Bottom() - h - 4));
    return {x, y, width, h};
}

namespace {

// `box` の中の 1 つのメニューの行 (描く。ウィンドウより高ければスクロールする)。クリックされた行を
// 返す (開いたフレームのものは除く: `fresh`)。なければ -1。`hovered`: マウスの下の行 (-1)。
// `openRow`: マウスの下にあるかのように見せる行 (そのメニューが開いている)。
int MenuBox(ui::Context& c, const Rect& box, const std::vector<MenuItem>& items, bool fresh, int openRow = -1, int* hovered = nullptr) {
    auto& p = c.P();
    DrawSurface(c, box, Surface::Popup, 6.f);
    if (hovered) *hovered = -1;

    int clicked = -1;
    // ウィンドウより高いリスト (MenuRect が箱をその中に収める): 行はスクロールする (ホイール、バー)。
    float content = 0;
    for (const auto& it : items) content += it.separator ? kSepH : kItemH;
    const Rect view{box.x, box.y + kPad, box.w, box.h - kPad * 2};
    const bool scrolls = content > view.h + 0.5f;
    const float off = scrolls ? BeginScroll(c, "menu_scroll", view, content) : 0.f;
    float y = view.y - off;
    const bool checks = std::any_of(items.begin(), items.end(), [](const MenuItem& it) { return it.checked; });
    c.PushId("menu");
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (it.separator) {
            Separator(c, box.x + 8, y + kSepH * 0.5f, box.w - 16);
            y += kSepH;
            continue;
        }
        const Rect row{box.x + kPad, y, box.w - kPad * 2 - (scrolls ? 10.f : 0.f), kItemH};
        const bool seen = row.Bottom() > view.y && row.y < view.Bottom();  // (スクロールで外に出た: 押されない)
        c.PushId(static_cast<uint64_t>(i));
        const auto b = it.enabled && seen ? c.ButtonBehavior(c.MakeId("item"), row) : ui::Context::ButtonResult{};
        c.PopId();
        if (b.hovered && hovered) *hovered = static_cast<int>(i);
        DrawRow(c, row, {false, b.hovered || static_cast<int>(i) == openRow, true});
        ui::TextStyle ts;
        ts.size = theme::FontSize;
        ts.color = it.enabled ? theme::Text : theme::TextFaint;
        if (it.checked) DrawIcon(c, {row.x + 2, row.y, 24, row.h}, Icon::Check, ts.color);
        p.Text(row.Inset(checks ? 28.f : 10.f, 0, it.submenu ? 28.f : 10.f, 0), it.label, ts);
        if (it.submenu) {
            DrawIcon(c, {row.Right() - 26, row.y, 22, row.h}, Icon::ChevronRight, it.enabled ? theme::TextDim : theme::TextFaint);
        } else if (!it.shortcut.empty()) {
            ts.h = ui::HAlign::Right;
            ts.color = theme::TextFaint;
            p.Text(row.Inset(10, 0, 10, 0), it.shortcut, ts);
        }
        // (開いたフレームのものは除く: 素早いクリックやタッチパッドのタップ - 1 フレームで押して離す -
        // だと、開いた同じ離しで、マウスの下に来た項目を選んでしまう)
        if (b.clicked && !fresh) clicked = static_cast<int>(i);
        y += kItemH;
    }
    c.PopId();
    if (scrolls) EndScroll(c, "menu_scroll", view, content);
    return clicked;
}

}  // namespace

int MenuPopup(ui::Context& c, Popup& popup, const std::vector<MenuItem>& items, float width) {
    if (!popup.open) return -1;
    const auto& in = c.In();
    const Rect box = MenuRect(c, popup, items, width);

    const bool fresh = popup.fresh;
    popup.fresh = false;
    if (!fresh && (in.pressed[0] || in.pressed[1]) && !box.Contains(in.mouseX, in.mouseY)) {
        popup.open = false;
        c.RequestRedraw();
        return -1;
    }
    if (c.KeyPressed(ui::Key::Escape)) {
        popup.open = false;
        c.RequestRedraw();
        return -1;
    }

    const int clicked = MenuBox(c, box, items, fresh);
    if (clicked >= 0) {
        popup.open = false;
        c.RequestRedraw();
    }
    return clicked;
}

namespace {
const wchar_t* KeyName(ui::Key k) {
    switch (k) {
        case ui::Key::Delete: return L"Delete";
        case ui::Key::Backspace: return L"Backspace";
        case ui::Key::Enter: return L"Enter";
        case ui::Key::F2: return L"F2";
        case ui::Key::F5: return L"F5";
        case ui::Key::A: return L"A";
        case ui::Key::C: return L"C";
        case ui::Key::D: return L"D";
        case ui::Key::E: return L"E";
        case ui::Key::F: return L"F";
        case ui::Key::Q: return L"Q";
        case ui::Key::W: return L"W";
        case ui::Key::N: return L"N";
        case ui::Key::O: return L"O";
        case ui::Key::P: return L"P";
        case ui::Key::S: return L"S";
        case ui::Key::V: return L"V";
        case ui::Key::X: return L"X";
        case ui::Key::Y: return L"Y";
        case ui::Key::Z: return L"Z";
        case ui::Key::Space: return L"Space";
        default: return L"";
    }
}
}  // namespace

std::wstring ShortcutText(const Action& a) {
    if (a.key == ui::Key::None) return {};
    std::wstring s;
    if (a.ctrl) s += L"Ctrl+";
    if (a.shift) s += L"Shift+";
    return s + KeyName(a.key);
}

void ActionMenu(ui::Context& c, Popup& popup, const std::vector<Action>& actions) {
    if (!popup.open) return;
    Popup* pp = &popup;
    const std::vector<Action>* list = &actions;
    c.AddOverlay([&c, pp, list] {
        Popup& pop = *pp;
        const auto& in = c.In();
        const Rect& vp = c.Viewport();
        std::vector<PickItem> items;
        items.reserve(list->size());
        for (const Action& a : *list) items.push_back({a.label, a.folder, a.separator});

        // 今開いているメニュー: 最初のものと、`sub` の各フォルダーの分 (追加, 追加/3D オブジェクト ...)。
        struct Level {
            std::wstring folder;
            std::vector<MenuLevelRow> rows;
            std::vector<MenuItem> shown;
            Rect box;
        };
        std::vector<Level> levels;
        auto build = [&](std::wstring folder) {
            Level l;
            l.folder = std::move(folder);
            l.rows = MenuLevel(items, l.folder);
            float width = 220;
            for (const MenuLevelRow& r : l.rows) {
                MenuItem mi;
                mi.label = r.label;
                mi.separator = r.separator;
                mi.submenu = r.isFolder;
                if (!r.isFolder && !r.separator) {
                    const Action& a = (*list)[static_cast<size_t>(r.item)];
                    mi.shortcut = ShortcutText(a);
                    mi.enabled = a.enabled && static_cast<bool>(a.run);
                }
                width = std::max(width, c.P().TextWidth(mi.label, theme::FontSize) + c.P().TextWidth(mi.shortcut, theme::FontSize) + 72);
                l.shown.push_back(std::move(mi));
            }
            l.box = {0, 0, width, 0};
            return l;
        };
        levels.push_back(build({}));
        for (size_t start = 0; start < pop.sub.size();) {
            const size_t slash = pop.sub.find(L'/', start);
            const size_t end = slash == std::wstring::npos ? pop.sub.size() : slash;
            Level l = build(pop.sub.substr(0, end));
            if (l.rows.empty()) break;  // (もうないフォルダー)
            levels.push_back(std::move(l));
            start = end + 1;
        }
        // それぞれの位置: 最初のものは開いた所。次のものは、前のメニューのフォルダーの行の横
        // (右に場所がなければ左)。ウィンドウの中に収める (MenuRect)。
        Popup at;
        at.x = pop.x;
        at.y = pop.y;
        for (size_t k = 0; k < levels.size(); ++k) {
            Level& l = levels[k];
            if (k > 0) {
                const Level& before = levels[k - 1];
                float y = before.box.y + kPad;  // (MenuBox が描くままの行。スクロールしていない)
                at.y = before.box.y;
                for (size_t i = 0; i < before.rows.size(); ++i) {
                    if (before.rows[i].isFolder && before.rows[i].path == l.folder) at.y = y - kPad;
                    y += before.shown[i].separator ? kSepH : kItemH;
                }
                at.x = before.box.Right() - 3;
                if (at.x + l.box.w > vp.Right() - 4) at.x = std::max(vp.x + 4, before.box.x - l.box.w + 3);
            }
            l.box = MenuRect(c, at, l.shown, l.box.w);
        }

        const bool fresh = pop.fresh;
        pop.fresh = false;
        const bool inside = std::any_of(levels.begin(), levels.end(), [&](const Level& l) { return l.box.Contains(in.mouseX, in.mouseY); });
        if (!fresh && (in.pressed[0] || in.pressed[1]) && !inside) {
            pop.open = false;
            c.RequestRedraw();
            return;
        }
        if (c.KeyPressed(ui::Key::Escape)) {
            // 最後に開いたメニューが閉じる (最初のメニューなら、全部)。
            if (levels.size() <= 1) {
                pop.open = false;
            } else {
                pop.sub = levels[levels.size() - 2].folder;
            }
            c.RequestRedraw();
            return;
        }

        const bool was = c.InputEnabled();
        c.SetInputEnabled(true);
        int picked = -1;
        std::wstring sub = levels.back().folder;
        for (size_t k = 0; k < levels.size(); ++k) {
            const Level& l = levels[k];
            int openRow = -1;
            if (k + 1 < levels.size()) {
                for (size_t i = 0; i < l.rows.size(); ++i) {
                    if (l.rows[i].isFolder && l.rows[i].path == levels[k + 1].folder) openRow = static_cast<int>(i);
                }
            }
            c.PushId(static_cast<uint64_t>(k));
            int hovered = -1;
            const int clicked = MenuBox(c, l.box, l.shown, fresh, openRow, &hovered);
            c.PopId();
            // 行の上のマウス: フォルダーのメニューはその横に開く。別の行なら、これより後のメニューを閉じる。
            if (hovered >= 0) {
                const MenuLevelRow& r = l.rows[static_cast<size_t>(hovered)];
                sub = r.isFolder ? r.path : l.folder;
            }
            if (clicked >= 0) {
                const MenuLevelRow& r = l.rows[static_cast<size_t>(clicked)];
                if (r.isFolder) {
                    sub = r.path;
                } else if (!r.separator) {
                    picked = r.item;
                }
            }
        }
        c.SetInputEnabled(was);
        if (sub != pop.sub) {
            pop.sub = std::move(sub);
            c.RequestRedraw();
        }
        if (picked >= 0 && static_cast<size_t>(picked) < list->size()) {
            pop.open = false;
            c.RequestRedraw();
            const auto run = (*list)[static_cast<size_t>(picked)].run;  // コピー: 実行するとリストが作り直されることがある
            if (run) run();
        }
    });
}

int PickerPopup(ui::Context& c, Popup& popup, std::string_view idStr, std::string& search, std::span<const PickRow> rows, float width,
                float rowHeight, const std::function<ID2D1Bitmap*(size_t row, float size)>& picture) {
    if (!popup.open) return -1;
    auto& p = c.P();
    const auto& in = c.In();
    constexpr float kSearchH = 38.f;
    const float kRowH = std::max(26.f, rowHeight);
    const float listH = kRowH * static_cast<float>(std::clamp<size_t>(rows.size(), 1, kRowH > 30.f ? 7 : 10));
    const float h = kPad * 2 + kSearchH + listH;
    const Rect& vp = c.Viewport();
    const Rect box{std::clamp(popup.x, vp.x + 4, std::max(vp.x + 4, vp.Right() - width - 4)),
                   std::clamp(popup.y, vp.y + 4, std::max(vp.y + 4, vp.Bottom() - h - 4)), width, h};
    c.PushId(idStr);
    const ui::Id searchId = c.MakeId("search");
    auto close = [&] {
        popup.open = false;
        if (c.Focused() == searchId) c.ClearFocus();
        c.RequestRedraw();
    };
    const bool fresh = popup.fresh;
    popup.fresh = false;
    if (fresh) c.FocusText(searchId, Utf8ToWide(search), true);
    if ((!fresh && (in.pressed[0] || in.pressed[1]) && !box.Contains(in.mouseX, in.mouseY)) || c.KeyPressed(ui::Key::Escape)) {
        close();
        c.PopId();
        return -1;
    }
    const bool enter = c.KeyPressed(ui::Key::Enter);

    DrawSurface(c, box, Surface::Popup, 6.f);
    TextField(c, "search", {box.x + kPad + 2, box.y + kPad + 4, box.w - kPad * 2 - 4, kSearchH - 10}, search, Tr(L"検索"));
    const Rect list{box.x + kPad, box.y + kPad + kSearchH, box.w - kPad * 2, listH};
    int clicked = -1;
    if (rows.empty()) {
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextFaint;
        p.Text(list.Inset(10, 0, 10, 0), Tr(L"見つかりません。"), ts);
    }
    const float content = kRowH * static_cast<float>(rows.size());
    const float off = BeginScroll(c, "list", list, content);
    for (size_t i = 0; i < rows.size(); ++i) {
        const Rect row{list.x, list.y + kRowH * static_cast<float>(i) - off, list.w - (content > list.h ? 12.f : 0.f), kRowH};
        if (row.Bottom() < list.y || row.y > list.Bottom()) continue;
        c.PushId(static_cast<uint64_t>(i));
        const auto b = c.ButtonBehavior(c.MakeId("row"), row);
        c.PopId();
        DrawRow(c, row, {false, b.hovered, true});
        const PickRow& it = rows[i];
        float x = row.x + 8;
        const float picSize = kRowH - 6.f;
        ID2D1Bitmap* bitmap = picture ? picture(i, picSize) : nullptr;
        if (bitmap) {
            DrawThumbnail(c, {x, row.y + 3.f, picSize, picSize}, bitmap);
            x += picSize + 6.f;
        } else if (it.hasIcon) {
            const float iconW = kRowH > 30.f ? picSize : 18.f;
            DrawIcon(c, {x, row.y, iconW, row.h}, it.icon, it.icon == Icon::Folder ? theme::Folder : theme::TextDim);
            x += iconW + 4.f;
        }
        ui::TextStyle ts;
        ts.size = theme::FontSize;
        ts.color = theme::Text;
        const float detailW = it.detail.empty() ? 0.f : std::min(row.w * 0.45f, p.TextWidth(it.detail, 11) + 8);
        p.Text({x, row.y, row.Right() - x - detailW - 6, row.h}, it.name, ts);
        if (detailW > 0) {
            ts.size = 11;
            ts.color = theme::TextFaint;
            ts.h = ui::HAlign::Right;
            p.Text({row.Right() - detailW - 6, row.y, detailW, row.h}, it.detail, ts);
        }
        // (開いたフレームのものは除く: 素早いクリックやタッチパッドのタップ - 1 フレームで押して離す -
        // だと、開いた同じ離しで、マウスの下に来た項目を選んでしまう)
        if (b.clicked && !fresh) clicked = static_cast<int>(i);
    }
    EndScroll(c, "list", list, content);
    // Enter は、入力した文字で見つかった最初の行を選ぶ。何も入力していなければ閉じるだけ: その
    // ときの最初の行は単にリストの先頭だから (参照の "(なし)" - Enter で閉じると消えてしまう)。
    if (clicked < 0 && enter && !rows.empty() && !search.empty()) clicked = 0;
    if (clicked < 0 && enter && search.empty()) close();
    if (clicked >= 0) close();
    c.PopId();
    return clicked;
}

void ActionPicker(ui::Context& c, Popup& popup, PickerState& state, const std::vector<Action>& actions, std::wstring title, float width) {
    if (!popup.open) return;
    Popup* pp = &popup;
    PickerState* sp = &state;
    const std::vector<Action>* list = &actions;
    c.AddOverlay([&c, pp, sp, list, title = std::move(title), width] {
        Popup& pop = *pp;
        PickerState& st = *sp;
        auto& p = c.P();
        const auto& in = c.In();
        const bool inputWas = c.InputEnabled();
        c.SetInputEnabled(true);
        constexpr float kHeadH = 34.f, kSearchH = 38.f, kRowH = 26.f, kIndent = 16.f;
        // 何を表示していても高さは同じ (フォルダーを開閉したり、入力したりしても、パネルが動かない)。
        const Rect& vp = c.Viewport();
        const float listH = std::clamp(vp.h - 8 - kPad * 2 - kHeadH - kSearchH, kRowH * 3, kRowH * 14);
        const float h = kPad * 2 + kHeadH + kSearchH + listH;
        const Rect box{std::clamp(pop.x, vp.x + 4, std::max(vp.x + 4, vp.Right() - width - 4)),
                       std::clamp(pop.y, vp.y + 4, std::max(vp.y + 4, vp.Bottom() - h - 4)), width, h};
        c.PushId("action_picker");
        const ui::Id searchId = c.MakeId("search");
        bool closing = false;
        auto close = [&] {
            pop.open = false;
            if (c.Focused() == searchId) c.ClearFocus();
            c.RequestRedraw();
            closing = true;
        };
        const bool fresh = pop.fresh;
        pop.fresh = false;
        if (fresh) {
            // 開いたとき: 検索は空にする (前回入力した文字だとリストの大半が隠れる)。フォルダーは
            // 閉じたときのままで、動かした位置もそのまま。
            st.search.clear();
            st.dragging = false;
            c.FocusText(searchId, L"", true);
            if (st.placed) {
                pop.x = st.x;
                pop.y = st.y;
            }
        }
        if ((!fresh && (in.pressed[0] || in.pressed[1]) && !box.Contains(in.mouseX, in.mouseY)) || c.KeyPressed(ui::Key::Escape)) close();
        const bool enter = c.KeyPressed(ui::Key::Enter);
        int run = -1;
        if (!closing) {
            DrawSurface(c, box, Surface::Popup, 6.f);
            // 上の部分: つかむ所と、何のためのものか、閉じる ×。つかんでドラッグすると、パネルが動く
            // (ウィンドウの中に収める: 上の箱は毎フレーム押し込まれる)。
            const Rect top{box.x, box.y, box.w - kPad - 34, kPad + kHeadH};
            const auto grab = c.ButtonBehavior(c.MakeId("top"), top);
            if (grab.held) {
                if (!st.dragging) {
                    st.dragging = true;
                    st.grabX = in.mouseX - box.x;
                    st.grabY = in.mouseY - box.y;
                }
                pop.x = in.mouseX - st.grabX;
                pop.y = in.mouseY - st.grabY;
                st.x = pop.x;
                st.y = pop.y;
                st.placed = true;
                c.RequestRedraw();
            } else {
                st.dragging = false;
            }
            DrawIcon(c, {box.x + kPad + 4, box.y + kPad, 16, kHeadH}, Icon::Grip, grab.hovered || grab.held ? theme::TextDim : theme::TextFaint);
            ui::TextStyle head;
            head.size = 12;
            head.color = theme::TextDim;
            p.Text({box.x + kPad + 24, box.y + kPad, box.w - kPad * 2 - 64, kHeadH}, title, head);
            if (IconButton(c, "close", {box.Right() - kPad - 30, box.y + kPad + 3, 28, 28}, Icon::Close)) close();
        }
        if (!closing) {
            TextField(c, "search", {box.x + kPad + 2, box.y + kPad + kHeadH, box.w - kPad * 2 - 4, kSearchH - 10}, st.search, Tr(L"検索"));
            // 行: フォルダー (開 / 閉) と、その中のアクション (BuildPickTree)。
            std::vector<PickItem> items;
            std::vector<size_t> index;  // items[i] は actions[index[i]]
            for (size_t i = 0; i < list->size(); ++i) {
                const Action& a = (*list)[i];
                if (a.separator) continue;
                items.push_back({a.label, a.folder});
                index.push_back(i);
            }
            const std::wstring search = Utf8ToWide(st.search);
            const std::vector<PickTreeRow> rows = BuildPickTree(items, search, st.open);
            const Rect listR{box.x + kPad, box.y + kPad + kHeadH + kSearchH, box.w - kPad * 2, listH};
            if (rows.empty()) {
                ui::TextStyle ts;
                ts.size = 12;
                ts.color = theme::TextFaint;
                p.Text({listR.x + 10, listR.y, listR.w - 20, kRowH}, Tr(L"見つかりません。"), ts);
            }
            const float content = kRowH * static_cast<float>(rows.size());
            const float off = BeginScroll(c, "list", listR, content);
            std::wstring toggle;
            for (size_t i = 0; i < rows.size(); ++i) {
                const PickTreeRow& r = rows[i];
                const Rect row{listR.x, listR.y + kRowH * static_cast<float>(i) - off, listR.w - (content > listR.h ? 12.f : 0.f), kRowH};
                if (row.Bottom() < listR.y || row.y > listR.Bottom()) continue;
                const Action* a = r.isFolder ? nullptr : &(*list)[index[static_cast<size_t>(r.item)]];
                const bool usable = r.isFolder || (a->enabled && static_cast<bool>(a->run));
                c.PushId(static_cast<uint64_t>(i));
                const auto b = usable ? c.ButtonBehavior(c.MakeId("row"), row) : ui::Context::ButtonResult{};
                c.PopId();
                DrawRow(c, row, {false, b.hovered, true});
                float x = row.x + 6 + kIndent * static_cast<float>(r.depth);
                ui::TextStyle ts;
                ts.size = theme::FontSize;
                ts.color = usable ? theme::Text : theme::TextFaint;
                if (r.isFolder) {
                    DrawIcon(c, {x, row.y, 16, row.h}, r.open ? Icon::ChevronDown : Icon::ChevronRight, theme::TextDim);
                    x += 18;
                    DrawIcon(c, {x, row.y, 18, row.h}, Icon::Folder, theme::Folder);
                    x += 22;
                    const std::wstring count = std::to_wstring(r.count);
                    const float countW = p.TextWidth(count, 11) + 10;
                    p.Text({x, row.y, row.Right() - x - countW - 6, row.h}, r.label, ts);
                    ui::TextStyle cs;
                    cs.size = 11;
                    cs.color = theme::TextFaint;
                    cs.h = ui::HAlign::Right;
                    p.Text({row.Right() - countW - 6, row.y, countW, row.h}, count, cs);
                    if (b.clicked && !fresh) toggle = r.path;
                } else {
                    // フォルダーの中: フォルダーの名前の下 (▸ と絵の先)。一番上: 端。
                    if (r.depth > 0) x += 40 - kIndent;
                    p.Text({x + 4, row.y, row.Right() - x - 10, row.h}, r.label, ts);
                    // (開いたフレームのものは除く: 素早いクリックだと、下に来た行を選んでしまう)
                    if (b.clicked && !fresh) run = static_cast<int>(index[static_cast<size_t>(r.item)]);
                }
            }
            EndScroll(c, "list", listR, content);
            // フォルダーをクリックした: 開閉する (検索中は、見つかったものを含むフォルダーが全部開く)。
            if (!toggle.empty() && search.empty()) {
                if (!st.open.erase(toggle)) st.open.insert(toggle);
                c.RequestRedraw();
            }
            // Enter: 入力した文字で見つかった、使える最初のもの。何も入力していなければ閉じるだけ。
            if (run < 0 && enter && !search.empty()) {
                for (const PickTreeRow& r : rows) {
                    if (r.isFolder) continue;
                    const Action& a = (*list)[index[static_cast<size_t>(r.item)]];
                    if (a.enabled && a.run) {
                        run = static_cast<int>(index[static_cast<size_t>(r.item)]);
                        break;
                    }
                }
            }
            if (run < 0 && enter && search.empty()) close();
            if (run >= 0) close();
        }
        c.PopId();
        c.SetInputEnabled(inputWas);
        if (run >= 0) {
            const auto fn = (*list)[static_cast<size_t>(run)].run;  // コピー: 実行するとリストが作り直されることがある
            if (fn) fn();
        }
    });
}

namespace {
struct DragState {
    bool active = false;
    DragThing thing;
    bool accepted = false;
    // 別のウィンドウの上 (SetDragElsewhere を見る): そのものをコピーして、そのウィンドウのフレームの間持っておく。
    const ui::Context* elsewhere = nullptr;
    float x = 0, y = 0;
    bool released = false;
    DragThing elsewhereThing;
};
DragState& Drag() {
    static DragState d;
    return d;
}
}  // namespace

void SetDragThing(DragThing thing) {
    Drag().active = true;
    Drag().thing = std::move(thing);
}

void EndDragThing(ui::Context& c) {
    c.AddOverlay([] {
        Drag().active = false;
        Drag().thing = {};
    });
}

const DragThing* DraggedThing() { return Drag().active ? &Drag().thing : nullptr; }

const DragThing* DropTarget(ui::Context& c, const Rect& r, const std::function<bool(const DragThing&)>& accepts) {
    DragState& d = Drag();
    if (d.elsewhere == &c) {
        // 別のウィンドウでドラッグして、これの上に来た。
        const bool over = r.Contains(d.x, d.y) && c.CurrentClip().Contains(d.x, d.y);
        if (!over || (accepts && !accepts(d.elsewhereThing))) return nullptr;
        c.P().StrokeRoundRect(r.Inset(-1), theme::Radius, theme::Accent, 2.f);
        if (!d.released) return nullptr;
        d.released = false;
        d.elsewhere = nullptr;
        return &d.elsewhereThing;  // 次のドラッグまで有効
    }
    const DragThing* t = DraggedThing();
    const bool released = c.MouseReleased();
    // (ボタンを押している間か、離した直後だけ: パネルがなくなったドラッグは何も残さない)
    if (!t || (!c.In().down[0] && !released) || !c.IsHovered(r) || (accepts && !accepts(*t))) return nullptr;
    Drag().accepted = true;
    c.P().StrokeRoundRect(r.Inset(-1), theme::Radius, theme::Accent, 2.f);
    c.RequestRedraw();
    return released ? t : nullptr;  // その上で離した
}

bool TakeDragAccepted() {
    const bool a = Drag().accepted;
    Drag().accepted = false;
    return a;
}

void SetDragElsewhere(const ui::Context* target, float x, float y, bool released) {
    DragState& d = Drag();
    d.elsewhere = target;
    d.x = x;
    d.y = y;
    d.released = target && released;
    if (target && d.active) d.elsewhereThing = d.thing;
}

const ui::Context* DragElsewhereTarget() { return Drag().elsewhere; }

void EndDragFrame(ui::Context& c) {
    DragState& d = Drag();
    if (d.elsewhere == &c && d.released) {
        d.elsewhere = nullptr;
        d.released = false;
    }
}

Rect BeginModal(ui::Context& c, float width, float height, std::wstring_view title) {
    auto& p = c.P();
    const Rect& vp = c.Viewport();
    p.FillRect(vp, theme::Dim);
    width = std::min(width, vp.w - 32);
    height = std::min(height, vp.h - 32);
    const Rect box{vp.x + (vp.w - width) * 0.5f, vp.y + (vp.h - height) * 0.5f, width, height};
    p.Shadow(box, 8, theme::Shadow, 28);
    DrawSurface(c, box, Surface::Panel, 8.f);
    // 盛り上がった帯の上のタイトル。ウィンドウ自身のタイトルバーのよう。
    const theme::Gradient tg = theme::Raised(theme::PanelHeader);
    p.FillRoundRectGradient({box.x + 1, box.y + 1, box.w - 2, 48}, 7.f, tg.top, tg.bottom);
    p.FillRect({box.x + 1, box.y + 30, box.w - 2, 19}, tg.bottom);  // 帯の下の角は四角
    TopLight(p, box, 8.f, theme::Highlight());
    Separator(c, box.x + 1, box.y + 49, box.w - 2);
    ui::TextStyle ts;
    ts.size = 17;
    ts.bold = true;
    ts.color = theme::Text;
    p.Text({box.x + 24, box.y + 9, box.w - 48, 32}, title, ts);
    return box.Inset(24, 58, 24, 20);
}

}  // namespace rm::editor::w
