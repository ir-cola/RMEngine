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

RM_TEST(Theme_SakuraHasTheOrnament) {
    CHECK(theme::FindTheme("Sakura").palette->ornament == theme::Ornament::Sakura);
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
    CHECK(theme::InkOn(theme::kSakura.sidebar) == black);
    CHECK(theme::InkOn(black) == white && theme::InkOn(white) == black);
    // Every theme: the logo's ink is the one a theme of that kind (light / dark) expects.
    for (const theme::ThemeInfo& t : theme::Themes()) {
        if (t.palette) CHECK(theme::InkOn(t.palette->sidebar) == (t.palette->light ? black : white));
    }
}
