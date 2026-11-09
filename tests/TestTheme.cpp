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
    const Bounds sun = BoundsOf(Sun(c, 100));
    CHECK(std::fabs(sun.y0 - 400) < 0.5f && sun.x1 < 601);

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

    // The beach covers the bottom of the window, no higher than asked.
    const rm::ui::Rect win{0, 0, 800, 600};
    const Bounds sand = BoundsOf(Sand(win, 100));
    CHECK(sand.y1 == 600 && sand.y0 >= 500 && sand.x0 == 0 && sand.x1 == 800);
    // The canopy keeps to the top.
    for (const auto& part : Canopy(win, 80, 9)) CHECK(part.steps.empty() || BoundsOf(part).y1 < 80 + 30);

    // Snow lies on the top edge: never more than `depth` above it, barely below it, off the corners.
    const rm::ui::Rect panel{100, 200, 300, 150};
    const Bounds snow = BoundsOf(SnowCap(panel, 6, 8));
    CHECK(snow.y0 >= 200 - 6.01f && snow.y1 <= 200 + 6.f);
    CHECK(snow.x0 >= 100 + 4.f && snow.x1 <= 400 - 4.f);
    CHECK(AllClockwise(SnowCap(panel, 6, 8)));
    CHECK(SnowCap({0, 0, 5, 10}, 6, 8).steps.empty());  // too narrow for any
}
