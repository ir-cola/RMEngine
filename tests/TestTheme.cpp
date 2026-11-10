// Tests for the color themes (editor/widgets/Theme.h, ThemeFile.h): the built-in three, the themes of
// the theme packs (their files in plugins/<author>/<name>/themes/), and the shapes of the seasons
// pack's pictures (plugins/rm_engine/theme_seasons/editor/Ornament.h).

#include "Test.h"

#include "editor/widgets/Theme.h"
#include "editor/widgets/ThemeFile.h"
#include "rm_engine/theme_seasons/editor/Ornament.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

using namespace rm::editor;
namespace ornament = rm::theme_seasons::ornament;

RM_TEST(Theme_BuiltInThree) {
    const auto list = theme::Themes();
    CHECK(list.size() >= 3);
    CHECK(list[0].id == "Dark" && list[1].id == "Light" && list[2].id == "System");
    for (size_t i = 0; i < 3; ++i) CHECK(list[i].group == 0 && list[i].pack.empty() && !list[i].ornament && !list[i].name.empty());
    // The old three-way choice was saved by these names: a hub.json from before keeps its theme.
    CHECK(theme::FindTheme("Dark").palette && theme::FindTheme("Dark").palette->bg == theme::kDark.bg);
    CHECK(!theme::FindTheme("System").palette);
    // Unknown (a pack not installed, a newer version's theme, a typo): the first theme.
    CHECK(&theme::FindTheme("NoSuchTheme") == &theme::Themes()[0]);
    CHECK(&theme::FindTheme("") == &theme::Themes()[0]);
}

RM_TEST(Theme_SystemFollowsWindows) {
    const theme::ThemeInfo& sys = theme::FindTheme("System");
    CHECK(&theme::PaletteOf(sys, true) == &theme::kLight);
    CHECK(&theme::PaletteOf(sys, false) == &theme::kDark);
    CHECK(theme::PaletteOf(theme::FindTheme("Dark"), true).bg == theme::kDark.bg);
    CHECK(theme::kLight.light && !theme::kDark.light);
}

RM_TEST(Theme_PackThemesComeAndGo) {
    theme::ThemeInfo spring;
    spring.id = "Spring";
    spring.name = L"スプリング";
    spring.palette = theme::kLight;
    spring.group = 1;
    spring.aliases = {"Sakura"};
    theme::SetPackThemes({spring});
    CHECK(theme::Themes().size() == 4);
    CHECK(theme::FindTheme("Spring").id == "Spring" && theme::FindTheme("Spring").group == 1);
    CHECK(theme::FindTheme("Sakura").id == "Spring");  // its name before (桜)
    // The pack removed: the first theme (hub.json keeps the id, so it comes back with the pack).
    theme::SetPackThemes({});
    CHECK(theme::Themes().size() == 3);
    CHECK(&theme::FindTheme("Spring") == &theme::Themes()[0]);
}

RM_TEST(ThemeFile_ReadsColorsAndFillsTheRest) {
    CHECK(theme::ParseHexColor("#FF8000") == rm::Color::Hex(0xFF8000));
    CHECK(theme::ParseHexColor("00000080") == rm::Color::Hex(0x000000, 128.f / 255.f));
    CHECK(!theme::ParseHexColor("#12345") && !theme::ParseHexColor("#GG0000") && !theme::ParseHexColor(""));
    const auto doc = rm::json::Parse(R"({"kind": "rm_engine.theme", "format": 1, "id": "Sea", "name": "海", "light": true,
        "ornament": "waves", "aliases": ["Ocean"], "order": 2, "caption": "#102030",
        "colors": {"bg": "#AABBCC", "shadow": "#00000040"}, "future": 1})");
    CHECK(doc.has_value());
    std::string err;
    const auto t = theme::ParseThemeFile(*doc, &err);
    CHECK(t.has_value());
    if (!t) return;
    CHECK(t->info.id == "Sea" && t->info.name == L"海" && t->order == 2 && t->ornament == "waves");
    CHECK(t->info.aliases == std::vector<std::string>{"Ocean"});
    CHECK(t->info.palette && t->info.palette->light);
    CHECK(t->info.palette->bg == rm::Color::Hex(0xAABBCC));
    CHECK(t->info.palette->shadow == rm::Color::Hex(0x000000, 64.f / 255.f));
    CHECK(t->info.palette->caption == 0x102030u);
    CHECK(t->info.palette->text == theme::kLight.text);  // left out: the light theme's
    // Not a theme, no id, a color that cannot be read: refused, with the reason.
    for (const char* bad : {R"({"kind": "other", "id": "A", "name": "A"})", R"({"kind": "rm_engine.theme", "name": "A"})",
                            R"({"kind": "rm_engine.theme", "id": "A", "name": "A", "colors": {"bg": "red"}})"}) {
        std::string why;
        CHECK(!theme::ParseThemeFile(*rm::json::Parse(bad), &why) && !why.empty());
    }
}

// Every theme file of the packs in this repository: readable, ids unique, light ones light.
RM_TEST(ThemeFile_ThePacks) {
    const std::filesystem::path plugins = std::filesystem::path(__FILE__).parent_path().parent_path() / "plugins";
    std::set<std::string> ids{"Dark", "Light", "System"};
    int count = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(plugins, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != ".json" || it->path().parent_path().filename() != "themes") continue;
        std::ifstream in(it->path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        const auto doc = rm::json::Parse(text);
        CHECK(doc.has_value());
        if (!doc) continue;
        std::string err;
        const auto t = theme::ParseThemeFile(*doc, &err);
        CHECK(t.has_value());
        if (!t) continue;
        CHECK(ids.insert(t->info.id).second);
        for (const std::string& a : t->info.aliases) CHECK(ids.insert(a).second);
        // The logo's ink (white or black, whichever stands out) is the one a theme of its kind expects.
        const rm::Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
        CHECK(theme::InkOn(t->info.palette->sidebar) == (t->info.palette->light ? black : white));
        ++count;
    }
    CHECK(count == 7);
    for (const char* id : {"Midnight", "Forest", "Paper", "Spring", "Summer", "Fall", "Winter", "Sakura"}) CHECK(ids.count(id) == 1);
}

RM_TEST(Ornament_SakuraShape) {
    using rm::ui::Path;
    const rm::Vec2 c{200, 300};
    const Path flat = ornament::Sakura(c, 100, 0);
    int figures = 0;
    float farthest = 0;
    auto reach = [&](rm::Vec2 v) { farthest = std::max(farthest, std::hypot(v.x - c.x, v.y - c.y)); };
    for (const Path::Step& s : flat.steps) {
        if (s.op == Path::Op::Begin) ++figures;
        reach(s.a);
        if (s.op == Path::Op::Cubic) {
            reach(s.b);
            reach(s.c);
        }
    }
    CHECK(figures == 5);                       // five petals
    CHECK(std::fabs(farthest - 100) < 0.01f);  // the tips touch the radius, nothing goes past it
    // Not turned: the first petal points straight up, its notch on the centre line.
    CHECK(flat.steps[3].op == Path::Op::Line);
    CHECK(std::fabs(flat.steps[3].a.x - 200) < 0.01f && std::fabs(flat.steps[3].a.y - 216) < 0.01f);
    // Turned 10 degrees counter-clockwise: that notch moves to the left (and stays 84 from the centre).
    const rm::Vec2 n = ornament::Sakura(c, 100, 10).steps[3].a;
    CHECK(n.x < 200 - 14 && n.x > 200 - 15);
    CHECK(std::fabs(std::hypot(n.x - c.x, n.y - c.y) - 84) < 0.01f);
}

RM_TEST(Theme_InkOn_WhiteOnDarkBlackOnLight) {
    const rm::Color white{1, 1, 1, 1}, black{0, 0, 0, 1};
    CHECK(theme::InkOn(theme::kDark.sidebar) == white);
    CHECK(theme::InkOn(theme::kLight.sidebar) == black);
    CHECK(theme::InkOn(black) == white && theme::InkOn(white) == black);
}

namespace {

// Every point a path reaches (the control points of its curves too).
struct Bounds {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int figures = 0;
};
Bounds BoundsOf(const rm::ui::Path& p) {
    Bounds b;
    auto add = [&](rm::Vec2 v) {
        b.x0 = std::min(b.x0, v.x), b.y0 = std::min(b.y0, v.y), b.x1 = std::max(b.x1, v.x), b.y1 = std::max(b.y1, v.y);
    };
    for (const auto& st : p.steps) {
        if (st.op == rm::ui::Path::Op::Begin) ++b.figures;
        add(st.a);
        if (st.op == rm::ui::Path::Op::Cubic) {
            add(st.b);
            add(st.c);
        }
    }
    return b;
}

// Twice the signed area of each figure's straight corners: > 0 = clockwise on the screen.
bool AllClockwise(const rm::ui::Path& p) {
    std::vector<rm::Vec2> fig;
    bool ok = true;
    auto close = [&] {
        float a = 0;
        for (size_t i = 0; i < fig.size(); ++i) {
            const rm::Vec2 u = fig[i], v = fig[(i + 1) % fig.size()];
            a += u.x * v.y - v.x * u.y;
        }
        if (fig.size() >= 3 && a < 0) ok = false;
        fig.clear();
    };
    for (const auto& st : p.steps) {
        if (st.op == rm::ui::Path::Op::Begin) close();
        fig.push_back(st.op == rm::ui::Path::Op::Cubic ? st.c : st.a);
    }
    close();
    return ok;
}

}  // namespace

RM_TEST(Ornament_ShapesStayInTheirPlace) {
    using namespace ornament;
    // Random: the same numbers for the same seed, all in 0 .. 1.
    Random a{7}, b{7};
    for (int i = 0; i < 100; ++i) {
        const float x = a.Next();
        CHECK(x == b.Next() && x >= 0 && x < 1);
    }
    // A polygon given counter-clockwise is turned round (so overlapping figures join, never cut holes).
    rm::ui::Path poly;
    const rm::Vec2 ccw[4] = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};
    AddPolygon(poly, ccw);
    CHECK(AllClockwise(poly));

    // The silhouettes: within their radius (a little more for a stalk / stem), all clockwise.
    const rm::Vec2 c{500, 500};
    const Bounds leaf = BoundsOf(MapleLeaf(c, 100, 0));
    CHECK(std::fabs(leaf.y0 - 400) < 1.f);         // the top lobe's tip at the radius
    CHECK(leaf.x0 > 390 && leaf.x1 < 610 && leaf.y1 < 570);
    CHECK(AllClockwise(MapleLeaf(c, 100, 33)));
    const Bounds flake = BoundsOf(Snowflake(c, 100, 0));
    CHECK(flake.x0 > 390 && flake.x1 < 610 && flake.y0 > 390 && flake.y1 < 610);
    CHECK(AllClockwise(Snowflake(c, 100, 10)));
    const Bounds sun = BoundsOf(Sun(c, 100, 0));
    CHECK(std::fabs(sun.y0 - 400) < 0.5f && sun.x0 > 390 && sun.x1 < 610 && sun.y1 < 610);  // the top ray's tip at the radius
    CHECK(AllClockwise(Sun(c, 100, 10)));

    // The cherry tree grows from the bottom edge near the right, its crown up top past the top and
    // right edges, reaching about 1.6 x size in; its petals blow to the left.
    const rm::ui::Rect screen{0, 0, 1000, 700};
    const rm::ui::Path cherry = CherryTree(screen, 200);
    const Bounds tree = BoundsOf(cherry);
    CHECK(tree.x0 > 1000 - 200 * 1.7f && tree.x1 > 1000 && tree.y0 < 0 && tree.y1 > 700);
    CHECK(AllClockwise(cherry));
    // Near the bottom only the trunk (and a petal or two): narrow, standing near the right edge.
    float lowLeft = 1e9f, lowRight = -1e9f;
    for (const auto& st : cherry.steps) {
        if (st.a.y < 680) continue;
        lowLeft = std::min(lowLeft, st.a.x);
        lowRight = std::max(lowRight, st.a.x);
    }
    CHECK(lowLeft > 1000 - 200 * 1.0f && lowRight < 1000);
    const rm::ui::Path stream = PetalStream(screen, 820, 80, 9, 4);
    const Bounds sb = BoundsOf(stream);
    CHECK(sb.figures > 60 && sb.x1 < 820 + 30 && sb.x0 > -10 && sb.y1 <= 700 + 10);
    int nearTree = 0, farAway = 0;  // thick near the tree, thinning out
    for (const auto& st : stream.steps) {
        if (st.op != rm::ui::Path::Op::Begin) continue;
        (st.a.x > 820 - 300 ? nearTree : farAway) += 1;
    }
    CHECK(nearTree > farAway);

    // Every corner silhouette fills the same box (a maple leaf as tall as a cherry blossom).
    const rm::ui::Rect box{10, 400, 200, 200};
    for (const rm::ui::Path& sil : {FitInto(Sakura({0, 0}, 1, 10), box), FitInto(MapleLeaf({0, 0}, 1, 10), box),
                                     FitInto(Snowflake({0, 0}, 1, 10), box), FitInto(Sun({0, 0}, 1, 10), box)}) {
        const Bounds sb2 = BoundsOf(sil);
        CHECK(sb2.x0 >= 10 - 0.01f && sb2.x1 <= 210 + 0.01f && sb2.y0 >= 400 - 0.01f && sb2.y1 <= 600 + 0.01f);
        CHECK(std::fabs((sb2.x1 - sb2.x0) - 200) < 0.01f || std::fabs((sb2.y1 - sb2.y0) - 200) < 0.01f);  // as large as it fits
        CHECK(std::fabs((sb2.x0 + sb2.x1) * 0.5f - 110) < 0.01f && std::fabs((sb2.y0 + sb2.y1) * 0.5f - 500) < 0.01f);  // centred
    }

    // The flurry stays in its area (give or take a petal's size).
    const rm::ui::Rect area{0, 0, 300, 200};
    const rm::ui::Path petals = Petals(area, 40, 8, 1234);
    const Bounds pb = BoundsOf(petals);
    CHECK(pb.figures == 40 && pb.x0 > -8 && pb.y0 > -8 && pb.x1 < 308 && pb.y1 < 208);

    const rm::ui::Rect win{0, 0, 800, 600};
    // The beach: only the bottom-right corner, its shore from (800 - 300, 600) up to (800, 600 - 200).
    // The sand castle: on its base (its heap a little below it, in the sand), as wide and tall as asked.
    const Bounds castle = BoundsOf(SandCastle({400, 500}, 100));
    CHECK(castle.x0 > 400 - 90 && castle.x1 < 400 + 90 && castle.y0 > 500 - 100 && castle.y1 < 500 + 12);
    CHECK(AllClockwise(SandCastle({400, 500}, 100)));
    // The summer silhouette: a mound on the bottom edge, the parasol the tallest thing on it.
    const Bounds scene = BoundsOf(BeachScene({500, 600}, 300));
    CHECK(scene.y1 > 600 && scene.y0 > 600 - 300 * 1.1f && scene.y0 < 600 - 300 * 0.85f);
    CHECK(scene.x0 > 500 - 300 && scene.x1 < 500 + 300 * 0.9f);
    CHECK(AllClockwise(BeachScene({500, 600}, 300)));
    // The canopy: thick at the top, a few further down, never below `depth` (give or take a leaf).
    int above = 0, below = 0;
    for (const auto& part : Canopy(win, 200, 0.25f, 9)) {
        if (part.steps.empty()) continue;
        CHECK(BoundsOf(part).y1 < 200 + 30);
        for (const auto& st : part.steps) (st.a.y < 60 ? above : below) += st.op == rm::ui::Path::Op::Begin ? 1 : 0;
    }
    CHECK(above + below >= 200 && above > below);  // 800 x 0.25 leaves (and a stalk each), most near the top

    // Falling snow stays in its area; the same flakes for the same seed.
    const Bounds flakes = BoundsOf(Snowfall({0, 0, 400, 300}, 50, 6, 3));
    CHECK(flakes.figures == 50 && flakes.x0 > -4 && flakes.y0 > -4 && flakes.x1 < 404 && flakes.y1 < 304);
    // Snow piles up at the bottom edge, inside it: never more than `depth` above it, off the corners.
    const rm::ui::Rect panel{100, 200, 300, 150};
    const Bounds drift = BoundsOf(SnowDrift(panel, 9, 8));
    CHECK(drift.y1 == 350 && drift.y0 >= 350 - 9.01f);
    CHECK(drift.x0 >= 100 + 4.f && drift.x1 <= 400 - 4.f);
    CHECK(AllClockwise(SnowDrift(panel, 9, 8)));
    CHECK(SnowDrift({0, 0, 5, 10}, 6, 8).steps.empty());  // too narrow for any
    // The same drift wherever the panel is and however wide it grows (only its right end follows).
    const auto heights = [](const rm::ui::Rect& r) {
        std::vector<float> out;
        for (const auto& st : SnowDrift(r, 9, 8).steps) out.push_back(st.a.y);
        return out;
    };
    const std::vector<float> narrow = heights({100, 200, 300, 150}), wide = heights({100, 200, 600, 150});
    const std::vector<float> moved = heights({250, 200, 300, 150});
    CHECK(narrow.size() > 40 && wide.size() > narrow.size());
    for (size_t i = 0; i + 10 < narrow.size(); ++i) CHECK(narrow[i] == wide[i]);  // until the right end's slope
    CHECK(moved == narrow);

    // Icicles hang from the top edge, no longer than asked, only a few here and there.
    const IcicleShapes ice = Icicles(win, 40, 5);
    const Bounds ib = BoundsOf(ice.ice);
    CHECK(ib.y0 >= 2 - 0.01f && ib.y1 <= 2 + 40 + 0.01f && ib.x0 >= 0);
    CHECK(ib.figures >= 1 && ib.figures <= 3 * (800 / 260 + 1));
    CHECK(BoundsOf(ice.snow).y0 >= -5.01f && BoundsOf(ice.shine).y1 <= ib.y1);
    CHECK(AllClockwise(ice.ice) && AllClockwise(ice.shine) && AllClockwise(ice.snow));
    // Each icicle's tip (its lowest point): the same icicles in the same places whatever the window's
    // width - one running past the right edge is kept (cut off by the edge), never dropped.
    const auto tips = [](const rm::ui::Path& path, float before) {
        std::vector<rm::Vec2> out;
        rm::Vec2 low{0, -1e9f};
        auto flush = [&] {
            if (low.y > -1e9f && low.x < before) out.push_back(low);
            low = {0, -1e9f};
        };
        for (const auto& st : path.steps) {
            if (st.op == rm::ui::Path::Op::Begin) flush();
            if (st.a.y > low.y) low = st.a;
        }
        flush();
        return out;
    };
    const auto tipsNarrow = tips(Icicles({0, 0, 800, 600}, 40, 5).ice, 800), tipsWide = tips(Icicles({0, 0, 2000, 600}, 40, 5).ice, 800);
    CHECK(!tipsNarrow.empty() && tipsNarrow == tipsWide);
}
