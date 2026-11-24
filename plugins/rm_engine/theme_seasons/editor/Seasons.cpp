#include "rm_engine/theme_seasons/editor/Seasons.h"

#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/theme_seasons/editor/Ornament.h"

#include <algorithm>
#include <array>
#include <vector>

namespace rm::theme_seasons {

namespace {

namespace theme = editor::theme;
using ui::Color;
using ui::Rect;

// 季節の絵の色 (内容の後ろにうっすら敷く: アルファも色のうち)。絵そのものの色は、どのパレットでも同じ。
// 影はテーマのアクセント色 (theme::OrnamentColor) を使う。
namespace season {
Color Petal() { return Color::Hex(0xEE8FAE, 0.42f); }       // 春: 吹雪 (パネルのウィンドウ)
Color PetalTone(int i) {                                    // 春: 上に沿って散る花びら、4 色 (少しずつ違う桜色)
    constexpr uint32_t k[4] = {0xEE8FAE, 0xF5ABC3, 0xE57399, 0xF8C6D6};
    return Color::Hex(k[i & 3], 0.42f);
}
// 夏: 右上の雲 (2026-11-25: 本物らしく)。半透明の白い雲に、上の縁の明るい帯を重ね、下ほど濃くなる
// 青灰色の影を重ねる (帯が重なるほど明るい / 暗い)。空 (背景の青) の上でも砂 (パネル) の上でも雲に見える。
Color CloudBody() { return Color::Hex(0xFFFFFF, 0.34f); }
Color CloudLight() { return Color::Hex(0xFFFFFF, 0.16f); }   // 3 枚重なる
Color CloudShade() { return Color::Hex(0x7F94AC, 0.075f); }  // 4 枚重なる
// 夏: 左下の太陽は、ほかの季節のシルエット (theme::OrnamentColor) より濃く (2026-11-25: ユーザーの希望)。
Color SunShade() { return theme::Accent.Alpha(0.24f); }
Color Leaf(int i) {                                         // 秋: 樹冠、4 色
    constexpr uint32_t k[4] = {0xD8432A, 0xE8832A, 0xE7B43A, 0xB0432A};
    return Color::Hex(k[i & 3], 0.34f);
}
Color Snow() { return Color::Hex(0xFFFFFF); }             // 冬: ウィンドウの一番下に積もる
Color SnowEdge() { return Color::Hex(0xA9BBCF, 0.55f); }  // 積もった雪の輪郭 (薄く: 2026-11-24 ユーザーの希望)
Color SnowLow() { return Color::Hex(0xE6EEF7); }          // 吹きだまりの影になった足元
Color Flake() { return Color::Hex(0xFFFFFF, 0.9f); }      // 降る雪
}  // namespace season

enum class Kind { Spring, Summer, Fall, Winter };

// 絵 = 色の付いた図形を、順に塗ったもの。
struct Layer {
    ui::Path path;
    Color color;
};
using Picture = std::vector<Layer>;

// 左下の角の影: 10 度反時計回りに回し、どの季節も同じ正方形に収める (もみじの葉が桜の花と同じ高さになる)。
// 大きさはウィンドウで決まる。
float CornerRadius(const Rect& w) { return std::clamp(std::min(w.w, w.h) * 0.17f, 56.f, 150.f); }
Rect Corner(const Rect& w, float r) { return {w.x + 28.f, w.Bottom() - 28.f - 2.f * r, 2.f * r, 2.f * r}; }
void Silhouette(Picture& pic, const Rect& w, ui::Path shape, Color color = theme::OrnamentColor()) {
    pic.push_back({ornament::FitInto(shape, Corner(w, CornerRadius(w))), color});
}

void SpringFlurry(Picture& pic, const Rect& w, float scale) {
    const Rect area{w.x, w.y, std::min(w.w * 0.42f, 560.f * scale), std::min(w.h * 0.38f, 320.f * scale)};
    const int count = std::max(10, static_cast<int>(area.w * area.h / 4200.f));
    pic.push_back({ornament::Petals(area, count, 9.f * std::max(scale, 0.7f), 0x5A6E7Au), season::Petal()});
}

void SummerCloud(Picture& pic, Vec2 center, float width, uint32_t seed) {
    ornament::CloudShapes cloud = ornament::Cumulus(center, width, seed);
    pic.push_back({std::move(cloud.body), season::CloudBody()});
    for (ui::Path& band : cloud.light) pic.push_back({std::move(band), season::CloudLight()});
    for (ui::Path& band : cloud.shade) pic.push_back({std::move(band), season::CloudShade()});
}

void SummerClouds(Picture& pic, const Rect& w, float scale) {
    const float cw = std::clamp(w.w * 0.16f, 110.f, 260.f) * scale;
    SummerCloud(pic, {w.Right() - w.w * 0.17f, w.y + std::max(70.f, w.h * 0.12f) * scale}, cw, 0x5C10u);
    SummerCloud(pic, {w.Right() - w.w * 0.40f, w.y + std::max(42.f, w.h * 0.07f) * scale}, cw * 0.62f, 0x5C21u);
    SummerCloud(pic, {w.Right() - w.w * 0.06f, w.y + std::max(150.f, w.h * 0.25f) * scale}, cw * 0.5f, 0x5C32u);
}

// 冬: ウィンドウに降る雪 (止めたもの)。(2026-11-24: 上の端のつららはユーザーの希望で外した。
// 形を作る ornament::Icicles は残してある)
void WinterSky(Picture& pic, const Rect& w, float scale) {
    const int count = std::max(20, static_cast<int>(w.w * w.h / 3600.f));
    pic.push_back({ornament::Snowfall(w, count, 5.5f * scale, 0x5A0Fu), season::Flake()});
}

// 冬: ウィンドウ (エディター本体・Hub) の一番下に積もった雪: 上に輪郭、白い雪、その影になった足元。
// パネルのウィンドウには積もらない。パネルごとにも積もらせない (2026-11-24: ユーザーの希望。前は
// どのパネルの下にも積もった)。ウィンドウの絵なので、一番下のパネルやバーの塗りの上にも内容の後ろに出る。
// 左右の端まで積もり、ウィンドウの端で切れる。高さは決まった値で、形は左端からの位置だけで決まる
// (2026-11-25: ユーザーの希望。大きさを変えても形が変わらない)。
void WinterGround(Picture& pic, const Rect& w) {
    constexpr float kDepth = 24.f;
    pic.push_back({ornament::SnowDrift({w.x, w.y - 1.5f, w.w, w.h}, kDepth), season::SnowEdge()});
    pic.push_back({ornament::SnowDrift(w, kDepth), season::Snow()});
    pic.push_back({ornament::SnowDrift(w, kDepth * 0.45f), season::SnowLow()});
}

Picture Make(Kind kind, const Rect& w, bool floating) {
    Picture pic;
    switch (kind) {
        case Kind::Spring:
            if (floating) {
                SpringFlurry(pic, w, 0.6f);  // 花びらだけ (木と花はメインのウィンドウのもの)
                break;
            }
            {
                // 秋の葉と同じように、少しずつ違う桜色の花びらが上に沿って濃く、下へ散らばる。左下に桜の花。
                // (2026-11-24: 右の桜の木はユーザーの希望で外した)
                auto petals = ornament::PetalCanopy(w, std::clamp(w.h * 0.32f, 120.f, 300.f), 1.f / 4.f, 0x5A4Bu);
                for (int i = 0; i < 4; ++i) pic.push_back({std::move(petals[static_cast<size_t>(i)]), season::PetalTone(i)});
                Silhouette(pic, w, ornament::Sakura({0, 0}, 1.f, 10.f));
            }
            break;
        case Kind::Summer:
            if (floating) {
                SummerClouds(pic, {w.x, w.y, w.w, std::min(w.h, 260.f)}, 0.6f);  // 雲だけ
                break;
            }
            {
                // 左下に太陽、右下に影 (春の木のような、パラソルと砂のお城のある砂の小山)、右上に雲。
                const float h = std::clamp(w.h * 0.40f, 150.f, 380.f);
                pic.push_back({ornament::BeachScene({w.Right() - 0.62f * h, w.Bottom()}, h), theme::OrnamentColor()});
                Silhouette(pic, w, ornament::Sun({0, 0}, 1.f, 10.f), season::SunShade());
                SummerClouds(pic, w, 1.f);
            }
            break;
        case Kind::Fall:
            if (floating) break;
            {
                // 赤 / オレンジ / 黄色の葉が上に沿って濃く、下へ散らばる。左下にもみじの葉。
                auto canopy = ornament::Canopy(w, std::clamp(w.h * 0.32f, 120.f, 300.f), 1.f / 4.f, 0xF411u);
                for (int i = 0; i < 4; ++i) pic.push_back({std::move(canopy[static_cast<size_t>(i)]), season::Leaf(i)});
                Silhouette(pic, w, ornament::MapleLeaf({0, 0}, 1.f, 10.f));
            }
            break;
        case Kind::Winter:
            WinterSky(pic, w, floating ? 0.8f : 1.f);
            if (!floating) {
                Silhouette(pic, w, ornament::Snowflake({0, 0}, 1.f, 10.f));
                WinterGround(pic, w);
            }
            break;
    }
    return pic;
}

// 絵は 1 フレームに何度も描かれる (背景、そして各パネルとバー、切り抜きつき) ので、
// ウィンドウの大きさごとに 1 回だけ作る: 最近のいくつかのウィンドウ (メインとパネルのウィンドウ) の分を覚えておく。
const Picture& Cached(Kind kind, const Rect& w, bool floating) {
    struct Entry {
        Kind kind;
        Rect w;
        bool floating;
        Color accent;  // 影の色はテーマから来る
        Picture picture;
    };
    static std::vector<Entry> cache;
    for (const Entry& e : cache) {
        if (e.kind == kind && e.floating == floating && e.accent == theme::Accent && e.w.x == w.x && e.w.y == w.y && e.w.w == w.w &&
            e.w.h == w.h) {
            return e.picture;
        }
    }
    if (cache.size() >= 4) cache.erase(cache.begin());
    cache.push_back({kind, w, floating, theme::Accent, Make(kind, w, floating)});
    return cache.back().picture;
}

template <Kind K>
void DrawWindow(ui::Context& c, const Rect& window, bool floating) {
    for (const Layer& l : Cached(K, window, floating)) c.P().FillPath(l.path, l.color);
}

constexpr editor::w::OrnamentInfo kSpring{"spring", &DrawWindow<Kind::Spring>, nullptr};
constexpr editor::w::OrnamentInfo kSummer{"summer", &DrawWindow<Kind::Summer>, nullptr};
constexpr editor::w::OrnamentInfo kFall{"fall", &DrawWindow<Kind::Fall>, nullptr};
constexpr editor::w::OrnamentInfo kWinter{"winter", &DrawWindow<Kind::Winter>, nullptr};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    for (const editor::w::OrnamentInfo* o : {&kSpring, &kSummer, &kFall, &kWinter}) registry.Contribute(editor::w::kOrnamentPoint, *o);
}

}  // namespace rm::theme_seasons
