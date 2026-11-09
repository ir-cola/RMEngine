// Tests for the list of color themes (editor/widgets/Theme.h): ids, the fallback, "PC の設定に合わせる".

#include "Test.h"

#include "editor/widgets/Theme.h"

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
