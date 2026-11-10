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

// The colors of the seasons' pictures (laid faintly behind the content: their alpha is part of
// them). The pictures' own colors, the same whichever palette - the silhouettes take the theme's
// accent (theme::OrnamentColor).
namespace season {
Color Petal() { return Color::Hex(0xEE8FAE, 0.42f); }       // spring: the flurry
Color CloudColor() { return Color::Hex(0xFFFFFF, 0.40f); }  // summer: clouds at the top-right
Color Leaf(int i) {                                         // fall: the canopy, four colors
    constexpr uint32_t k[4] = {0xD8432A, 0xE8832A, 0xE7B43A, 0xB0432A};
    return Color::Hex(k[i & 3], 0.34f);
}
Color Snow() { return Color::Hex(0xFFFFFF); }             // winter: piled up at the bottom of panels
Color SnowShade() { return Color::Hex(0x8CA3BD); }        // its outline
Color SnowLow() { return Color::Hex(0xE6EEF7); }          // the shaded foot of the drift
Color Flake() { return Color::Hex(0xFFFFFF, 0.9f); }      // falling snow
Color Ice() { return Color::Hex(0xD6E7F6, 0.88f); }       // icicles: pale, a little see-through
Color IceShade() { return Color::Hex(0x6F8AA8, 0.75f); }  // their edge
Color IceShine() { return Color::Hex(0xFFFFFF, 0.95f); }
}  // namespace season

enum class Kind { Spring, Summer, Fall, Winter };

// A picture = shapes in colors, filled in order.
struct Layer {
    ui::Path path;
    Color color;
};
using Picture = std::vector<Layer>;

// The silhouette in the bottom-left corner: turned 10 degrees counter-clockwise and fitted into the
// same square for every season (so a maple leaf is as tall as a cherry blossom), sized by the window.
float CornerRadius(const Rect& w) { return std::clamp(std::min(w.w, w.h) * 0.17f, 56.f, 150.f); }
Rect Corner(const Rect& w, float r) { return {w.x + 28.f, w.Bottom() - 28.f - 2.f * r, 2.f * r, 2.f * r}; }
void Silhouette(Picture& pic, const Rect& w, ui::Path shape) {
    pic.push_back({ornament::FitInto(shape, Corner(w, CornerRadius(w))), theme::OrnamentColor()});
}

void SpringFlurry(Picture& pic, const Rect& w, float scale) {
    const Rect area{w.x, w.y, std::min(w.w * 0.42f, 560.f * scale), std::min(w.h * 0.38f, 320.f * scale)};
    const int count = std::max(10, static_cast<int>(area.w * area.h / 4200.f));
    pic.push_back({ornament::Petals(area, count, 9.f * std::max(scale, 0.7f), 0x5A6E7Au), season::Petal()});
}

void SummerClouds(Picture& pic, const Rect& w, float scale) {
    const float cw = std::clamp(w.w * 0.16f, 110.f, 260.f) * scale;
    pic.push_back({ornament::Cloud({w.Right() - w.w * 0.17f, w.y + std::max(70.f, w.h * 0.12f) * scale}, cw), season::CloudColor()});
    pic.push_back({ornament::Cloud({w.Right() - w.w * 0.40f, w.y + std::max(42.f, w.h * 0.07f) * scale}, cw * 0.62f), season::CloudColor()});
    pic.push_back({ornament::Cloud({w.Right() - w.w * 0.06f, w.y + std::max(150.f, w.h * 0.25f) * scale}, cw * 0.5f), season::CloudColor()});
}

// Winter: snow falling over the window (held still) and icicles from its top edge: their edge (the
// ice 1px lower and to the right), the pale ice, the shine down their left side, the snow they hang from.
void WinterSky(Picture& pic, const Rect& w, float scale) {
    const int count = std::max(20, static_cast<int>(w.w * w.h / 3600.f));
    pic.push_back({ornament::Snowfall(w, count, 5.5f * scale, 0x5A0Fu), season::Flake()});
    const float icicle = 40.f * scale;
    ornament::IcicleShapes edge = ornament::Icicles({w.x + 0.8f, w.y + 1.f, w.w, w.h}, icicle, 0x1C1Eu);
    ornament::IcicleShapes ice = ornament::Icicles(w, icicle, 0x1C1Eu);
    pic.push_back({std::move(edge.ice), season::IceShade()});
    pic.push_back({std::move(ice.ice), season::Ice()});
    pic.push_back({std::move(ice.shine), season::IceShine()});
    pic.push_back({std::move(edge.snow), season::SnowShade()});
    pic.push_back({std::move(ice.snow), season::Snow()});
}

Picture Make(Kind kind, const Rect& w, bool floating) {
    Picture pic;
    switch (kind) {
        case Kind::Spring:
            if (floating) {
                SpringFlurry(pic, w, 0.6f);  // the petals only (the tree and the blossom are the main window's)
                break;
            }
            {
                // A cherry tree growing from the bottom edge on the right, in bloom up top, its petals
                // blowing off it to the left; a blossom in the bottom-left, petals at the top-left.
                const float d = std::clamp(std::max(w.w * 0.18f, w.h * 0.25f), 150.f, 340.f);
                pic.push_back({ornament::CherryTree(w, d), theme::OrnamentColor()});
                const int stream = std::max(24, static_cast<int>(w.w * w.h / 16000.f));
                pic.push_back({ornament::PetalStream(w, w.Right() - d * 1.3f, stream, 9.f, 0xB10Fu), season::Petal()});
                Silhouette(pic, w, ornament::Sakura({0, 0}, 1.f, 10.f));
                SpringFlurry(pic, w, 1.f);
            }
            break;
        case Kind::Summer:
            if (floating) {
                SummerClouds(pic, {w.x, w.y, w.w, std::min(w.h, 260.f)}, 0.6f);  // the clouds only
                break;
            }
            {
                // The sun in the bottom-left, a silhouette in the bottom-right (a mound of sand with a
                // parasol and a sand castle, as spring has its tree), clouds at the top-right.
                const float h = std::clamp(w.h * 0.40f, 150.f, 380.f);
                pic.push_back({ornament::BeachScene({w.Right() - 0.62f * h, w.Bottom()}, h), theme::OrnamentColor()});
                Silhouette(pic, w, ornament::Sun({0, 0}, 1.f, 10.f));
                SummerClouds(pic, w, 1.f);
            }
            break;
        case Kind::Fall:
            if (floating) break;
            {
                // Red / orange / yellow leaves thick along the top and scattered further down, a maple
                // leaf in the bottom-left.
                auto canopy = ornament::Canopy(w, std::clamp(w.h * 0.32f, 120.f, 300.f), 1.f / 4.f, 0xF411u);
                for (int i = 0; i < 4; ++i) pic.push_back({std::move(canopy[static_cast<size_t>(i)]), season::Leaf(i)});
                Silhouette(pic, w, ornament::MapleLeaf({0, 0}, 1.f, 10.f));
            }
            break;
        case Kind::Winter:
            WinterSky(pic, w, floating ? 0.8f : 1.f);
            if (!floating) Silhouette(pic, w, ornament::Snowflake({0, 0}, 1.f, 10.f));
            break;
    }
    return pic;
}

// The picture is drawn many times a frame (the background, then each panel and bar, clipped), so it
// is made once per window size: kept for the last few windows (the main one and panel windows).
const Picture& Cached(Kind kind, const Rect& w, bool floating) {
    struct Entry {
        Kind kind;
        Rect w;
        bool floating;
        Color accent;  // the silhouettes' color comes from the theme
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

// Winter: snow piled up at the bottom of a panel, behind its content: an outline above, the white
// snow, and its shaded foot.
void WinterPanel(ui::Context& c, const Rect& r, float corner) {
    constexpr float kDepth = 22.f;
    auto& p = c.P();
    p.FillPath(ornament::SnowDrift({r.x, r.y - 1.5f, r.w, r.h}, kDepth, corner), season::SnowShade());
    p.FillPath(ornament::SnowDrift(r, kDepth, corner), season::Snow());
    p.FillPath(ornament::SnowDrift(r, kDepth * 0.45f, corner), season::SnowLow());
}

constexpr editor::w::OrnamentInfo kSpring{"spring", &DrawWindow<Kind::Spring>, nullptr};
constexpr editor::w::OrnamentInfo kSummer{"summer", &DrawWindow<Kind::Summer>, nullptr};
constexpr editor::w::OrnamentInfo kFall{"fall", &DrawWindow<Kind::Fall>, nullptr};
constexpr editor::w::OrnamentInfo kWinter{"winter", &DrawWindow<Kind::Winter>, &WinterPanel};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    for (const editor::w::OrnamentInfo* o : {&kSpring, &kSummer, &kFall, &kWinter}) registry.Contribute(editor::w::kOrnamentPoint, *o);
}

}  // namespace rm::theme_seasons
