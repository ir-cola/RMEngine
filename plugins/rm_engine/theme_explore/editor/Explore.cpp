#include "rm_engine/theme_explore/editor/Explore.h"

#include "editor/widgets/OrnamentPicture.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/theme_explore/editor/Shapes.h"
#include "ui/Shape.h"

#include <algorithm>

namespace rm::theme_explore {

namespace {

namespace theme = editor::theme;
using editor::w::OrnamentPicture;
using ui::Color;
using ui::Rect;

// 絵の色 (内容の後ろにうっすら敷く: アルファも色のうち)。どのパレットでも同じ。左下の影はテーマのアクセント
// (theme::OrnamentColor)。
namespace look {
// 宇宙
Color StarFaint() { return Color::Hex(0xFFFFFF, 0.28f); }
Color StarBright() { return Color::Hex(0xFFFFFF, 0.55f); }
Color Sparkle() { return Color::Hex(0xE8E4FF, 0.55f); }
Color Nebula(int i) {  // 外 → 内
    constexpr uint32_t k[3] = {0x6A4DFF, 0xB04DFF, 0x4DA6FF};
    return Color::Hex(k[i % 3], 0.055f);
}
Color RingBack() { return Color::Hex(0xE8D2A0, 0.30f); }
Color Planet() { return Color::Hex(0xD08A5A, 0.42f); }
Color RingFront() { return Color::Hex(0xF2DDB0, 0.38f); }
Color Meteor() { return Color::Hex(0xFFFFFF, 0.50f); }
// 深海
Color Ray() { return Color::Hex(0x9FE8FF, 0.035f); }
Color MarineSnow() { return Color::Hex(0xCFEFF5, 0.18f); }
Color Bubble() { return Color::Hex(0xBFF3FF, 0.10f); }
Color BubbleShine() { return Color::Hex(0xFFFFFF, 0.35f); }
Color Glow() { return Color::Hex(0x5CFFE1, 0.35f); }  // 光る粒 (生き物の光)
Color Fish() { return theme::Accent.Alpha(0.16f); }
Color Floor() { return Color::Hex(0x1B3A40, 0.55f); }
Color KelpNear() { return Color::Hex(0x14705A, 0.45f); }
Color KelpFar() { return Color::Hex(0x0E4F42, 0.40f); }
// 火山
Color LavaGlowFar() { return Color::Hex(0xFF4A1A, 0.07f); }
Color LavaGlowNear() { return Color::Hex(0xFF7A2A, 0.08f); }
Color CraterGlow() { return Color::Hex(0xFF6A2A, 0.05f); }
Color SmokeColor() { return Color::Hex(0x8A7A75, 0.10f); }
Color Mountain() { return Color::Hex(0x000000, 0.40f); }
Color Lava() { return Color::Hex(0xFF6A1F, 0.55f); }
Color Ember() { return Color::Hex(0xFFA040, 0.45f); }
Color Spark() { return Color::Hex(0xFFE08A, 0.50f); }
}  // namespace look

enum class Kind { Space, DeepSea, Volcano };

void Silhouette(OrnamentPicture& pic, const Rect& w, ui::Path shape) {
    pic.push_back({ui::shape::FitInto(shape, editor::w::OrnamentCorner(w)), theme::OrnamentColor()});
}

// 面積に比べた数 (少なくとも `least`)。
int CountFor(const Rect& r, float perArea, int least) { return std::max(least, static_cast<int>(r.w * r.h / perArea)); }

void StarSky(OrnamentPicture& pic, const Rect& w, float scale) {
    pic.push_back({ui::shape::Dots(w, CountFor(w, 2600.f, 20), 2.2f * scale, 0x5701u), look::StarFaint()});
    pic.push_back({ui::shape::Dots(w, CountFor(w, 11000.f, 6), 3.4f * scale, 0x5702u), look::StarBright()});
    pic.push_back({shapes::Sparkles(w, CountFor(w, 90000.f, 3), 16.f * scale, 0x5703u), look::Sparkle()});
}

void Space(OrnamentPicture& pic, const Rect& w, bool floating) {
    if (floating) {
        StarSky(pic, w, 0.8f);
        return;
    }
    const float m = std::min(w.w, w.h);
    auto nebula = shapes::Nebula({w.x + w.w * 0.30f, w.y + w.h * 0.32f}, std::clamp(m * 0.35f, 140.f, 420.f), 0x5704u);
    for (int i = 0; i < 3; ++i) pic.push_back({std::move(nebula[static_cast<size_t>(i)]), look::Nebula(i)});
    StarSky(pic, w, 1.f);
    const float r = std::clamp(m * 0.07f, 30.f, 80.f);
    shapes::PlanetShapes planet =
        shapes::RingedPlanet({w.x + w.w - std::max(w.w * 0.15f, 2.6f * r), w.y + std::max(w.h * 0.22f, 1.6f * r)}, r, 14.f);
    pic.push_back({std::move(planet.ringBack), look::RingBack()});
    pic.push_back({std::move(planet.body), look::Planet()});
    pic.push_back({std::move(planet.ringFront), look::RingFront()});
    pic.push_back({shapes::ShootingStar({w.x + w.w * 0.62f, w.y + w.h * 0.20f}, std::clamp(w.w * 0.12f, 80.f, 220.f), -55.f), look::Meteor()});
    Silhouette(pic, w, shapes::Rocket({0, 0}, 1.f, -25.f));
}

void DeepSea(OrnamentPicture& pic, const Rect& w, bool floating) {
    if (floating) {
        pic.push_back({ui::shape::Dots(w, CountFor(w, 5000.f, 10), 2.f, 0x5E01u), look::MarineSnow()});
        shapes::BubbleShapes b = shapes::Bubbles(w, CountFor(w, 30000.f, 6), 12.f, 0x5E02u);
        pic.push_back({std::move(b.body), look::Bubble()});
        pic.push_back({std::move(b.shine), look::BubbleShine()});
        return;
    }
    for (ui::Path& ray : shapes::LightRays(w, 4, 0x5E03u)) pic.push_back({std::move(ray), look::Ray()});
    pic.push_back({ui::shape::Dots(w, CountFor(w, 5000.f, 20), 2.f, 0x5E04u), look::MarineSnow()});
    const Rect column{w.x + w.w * 0.74f, w.y + w.h * 0.08f, w.w * 0.18f, w.h * 0.84f};
    shapes::BubbleShapes b = shapes::Bubbles(column, 26, 18.f, 0x5E05u);
    pic.push_back({std::move(b.body), look::Bubble()});
    pic.push_back({std::move(b.shine), look::BubbleShine()});
    pic.push_back({shapes::FishSchool({w.x + w.w * 0.58f, w.y + w.h * 0.42f}, std::clamp(w.w * 0.18f, 140.f, 320.f), 14, 22.f, 0x5E06u),
                   look::Fish()});
    pic.push_back({ui::shape::Dots({w.x, w.y + w.h * 0.5f, w.w, w.h * 0.45f}, 18, 4.f, 0x5E07u), look::Glow()});
    const float h = std::clamp(w.h * 0.38f, 140.f, 340.f);
    pic.push_back({shapes::Kelp({w.x + w.w - w.w * 0.28f, w.y + w.h}, h * 0.7f, 0x5E08u), look::KelpFar()});
    pic.push_back({ui::shape::WavyBottom(w, 22.f, 0.7f), look::Floor()});
    pic.push_back({shapes::Kelp({w.x + w.w - w.w * 0.12f, w.y + w.h}, h, 0x5E09u), look::KelpNear()});
    Silhouette(pic, w, shapes::Jellyfish({0, 0}, 1.f, 8.f));
}

void Volcano(OrnamentPicture& pic, const Rect& w, bool floating) {
    if (!floating) {
        pic.push_back({ui::shape::WavyBottom(w, 60.f, 0.3f), look::LavaGlowFar()});
        pic.push_back({ui::shape::WavyBottom(w, 30.f, 1.9f), look::LavaGlowNear()});
        const float h = std::clamp(w.h * 0.42f, 160.f, 380.f);
        shapes::VolcanoShapes v = shapes::Volcano({w.x + w.w - 0.9f * h, w.y + w.h}, h, 0x7A01u);
        for (ui::Path& g : v.glow) pic.push_back({std::move(g), look::CraterGlow()});
        pic.push_back({shapes::Smoke(v.crater, h * 1.1f, 0x7A02u), look::SmokeColor()});
        pic.push_back({std::move(v.mountain), look::Mountain()});
        pic.push_back({std::move(v.lava), look::Lava()});
        Silhouette(pic, w, shapes::Flame({0, 0}, 1.f, 6.f));
    }
    const float scale = floating ? 0.7f : 1.f;
    pic.push_back({shapes::Embers(w, CountFor(w, 7000.f, 12), 7.f * scale, 0x7A03u), look::Ember()});
    pic.push_back({shapes::Embers(w, CountFor(w, 16000.f, 6), 4.f * scale, 0x7A04u), look::Spark()});
}

OrnamentPicture Make(int kind, const Rect& w, bool floating, const Rect&) {
    OrnamentPicture pic;
    switch (static_cast<Kind>(kind)) {
        case Kind::Space: Space(pic, w, floating); break;
        case Kind::DeepSea: DeepSea(pic, w, floating); break;
        case Kind::Volcano: Volcano(pic, w, floating); break;
    }
    return pic;
}

template <Kind K>
void DrawWindow(ui::Context& c, const Rect& window, bool floating) {
    editor::w::DrawOrnamentPicture(c, &Make, static_cast<int>(K), window, floating);
}

constexpr editor::w::OrnamentInfo kSpace{"space", &DrawWindow<Kind::Space>, nullptr};
constexpr editor::w::OrnamentInfo kDeepSea{"deep_sea", &DrawWindow<Kind::DeepSea>, nullptr};
constexpr editor::w::OrnamentInfo kVolcano{"volcano", &DrawWindow<Kind::Volcano>, nullptr};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    for (const editor::w::OrnamentInfo* o : {&kSpace, &kDeepSea, &kVolcano}) registry.Contribute(editor::w::kOrnamentPoint, *o);
}

}  // namespace rm::theme_explore
