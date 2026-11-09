// Tests for the list of color themes (editor/widgets/Theme.h): ids, the fallback, "PC の設定に合わせる".

#include "Test.h"

#include "editor/widgets/Ornament.h"
#include "editor/widgets/Theme.h"

#include <algorithm>
#include <cmath>

#include <set>
#include <string>

using namespace rm::editor;

RM_TEST(Theme_IdsAreUniqueAndNamed) {
    std::set<std::string> ids;
    for (const theme::ThemeInfo& t : theme::Themes()) {
        CHECK(ids.insert(t.id).second);
        CHECK(t.name && *t.name);
    }
    CHECK(!ids.empty());
}

RM_TEST(Theme_FindKnownAndUnknown) {
    CHECK(std::string(theme::FindTheme("Light").id) == "Light");
    // The old three-way choice was saved by these names: a hub.json from before keeps its theme.
    CHECK(theme::FindTheme("Dark").palette == &theme::kDark);
    CHECK(theme::FindTheme("System").palette == nullptr);
    // Unknown (a newer version's theme, a typo): the first theme.
    CHECK(&theme::FindTheme("NoSuchTheme") == &theme::Themes()[0]);
    CHECK(&theme::FindTheme("") == &theme::Themes()[0]);
}

RM_TEST(Theme_SystemFollowsWindows) {
    const theme::ThemeInfo& sys = theme::FindTheme("System");
    CHECK(&theme::PaletteOf(sys, true) == &theme::kLight);
    CHECK(&theme::PaletteOf(sys, false) == &theme::kDark);
    CHECK(&theme::PaletteOf(theme::FindTheme("Dark"), true) == &theme::kDark);
    CHECK(theme::kLight.light && !theme::kDark.light);
}

RM_TEST(Theme_TheFourSeasons) {
    CHECK(theme::FindTheme("Spring").palette->ornament == theme::Ornament::Spring);
    CHECK(theme::FindTheme("Summer").palette->ornament == theme::Ornament::Summer);
    CHECK(theme::FindTheme("Fall").palette->ornament == theme::Ornament::Fall);
    CHECK(theme::FindTheme("Winter").palette->ornament == theme::Ornament::Winter);
    // 桜 was renamed スプリング: a hub.json from before keeps its theme.
    CHECK(std::string_view(theme::FindTheme("Sakura").id) == "Spring");
    // The seasons are a group of their own (their own row on the theme screen).
    for (const char* id : {"Spring", "Summer", "Fall", "Winter"}) CHECK(theme::FindTheme(id).group == 2);
    CHECK(theme::kDark.ornament == theme::Ornament::None);
    CHECK(theme::kLight.ornament == theme::Ornament::None);
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
    CHECK(theme::InkOn(theme::kMidnight.sidebar) == white);
    CHECK(theme::InkOn(theme::kForest.sidebar) == white);
    CHECK(theme::InkOn(theme::kLight.sidebar) == black);
    CHECK(theme::InkOn(theme::kPaper.sidebar) == black);
    CHECK(theme::InkOn(theme::kSpring.sidebar) == black);
    CHECK(theme::InkOn(theme::kSummer.sidebar) == black);
    CHECK(theme::InkOn(theme::kFall.sidebar) == black);
    CHECK(theme::InkOn(theme::kWinter.sidebar) == black);
    CHECK(theme::InkOn(black) == white && theme::InkOn(white) == black);
    // Every theme: the logo's ink is the one a theme of that kind (light / dark) expects.
    for (const theme::ThemeInfo& t : theme::Themes()) {
        if (t.palette) CHECK(theme::InkOn(t.palette->sidebar) == (t.palette->light ? black : white));
    }
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
    const Bounds flower = BoundsOf(Sunflower(c, 100, 0));
    CHECK(flower.x0 > 390 && flower.x1 < 610 && flower.y0 > 390 && flower.y1 < 660);  // the stem hangs 1.45 below

    // The cherry tree stands on its base and keeps under its height.
    const Bounds tree = BoundsOf(CherryTree({500, 900}, 400));
    CHECK(tree.y1 <= 900.5f && tree.y0 > 900 - 400 * 1.05f);
    CHECK(tree.x0 > 500 - 400 * 0.8f && tree.x1 < 500 + 400 * 0.8f);
    CHECK(AllClockwise(CherryTree({500, 900}, 400)));
    // The same tree every time.
    CHECK(CherryTree({1, 2}, 50).steps.size() == CherryTree({1, 2}, 50).steps.size());

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
    const Beach beach = BeachCorner(win, 300, 200);
    const Bounds sand = BoundsOf(beach.sand);
    CHECK(sand.x0 >= 500 && sand.x1 == 800 && sand.y0 >= 400 - 0.01f && sand.y1 == 600);
    CHECK(std::fabs(ShoreY(win, 300, 200, 500) - 600) < 0.01f && std::fabs(ShoreY(win, 300, 200, 800) - 400) < 0.01f);
    CHECK(ShoreY(win, 300, 200, 650) < 600 - 100);  // bowed towards the sea in the middle
    CHECK(AllClockwise(beach.sand) && AllClockwise(beach.foam));
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

    // Icicles hang from the top edge, no longer than asked, in the same places when the window widens.
    const Bounds ice = BoundsOf(Icicles(win, 40, 5));
    CHECK(ice.y0 >= -4.01f && ice.y1 <= 2 + 40 + 0.01f && ice.x0 >= 0 && ice.x1 <= 800);
    // ... only a few, here and there (not along the whole edge).
    CHECK(ice.figures >= 2 && ice.figures <= 4 * (800 / 260 + 1));
    CHECK(AllClockwise(Icicles(win, 40, 5)));
    // (each icicle's tip is the end of its first curve)
    const auto tips = [](const rm::ui::Path& path) {
        std::vector<rm::Vec2> out;
        for (size_t i = 0; i + 1 < path.steps.size(); ++i) {
            if (path.steps[i].op == rm::ui::Path::Op::Line && path.steps[i + 1].op == rm::ui::Path::Op::Cubic && path.steps[i + 1].c.x < 700)
                out.push_back(path.steps[i + 1].c);
        }
        return out;
    };
    const std::vector<rm::Vec2> t1 = tips(Icicles({0, 0, 800, 600}, 40, 5)), t2 = tips(Icicles({0, 0, 1200, 600}, 40, 5));
    CHECK(!t1.empty() && t1 == t2);
}
