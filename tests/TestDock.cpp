// Tests for the docking tree (editor/dock/DockLayout): edits, pruning, save format, geometry, the tab
// strip that does not fit; and the named layouts (editor/dock/NamedLayouts).

#include "Test.h"

#include "editor/dock/DockLayout.h"
#include "editor/dock/NamedLayouts.h"

#include <cmath>

using namespace rm;
using namespace rm::editor::dock;

namespace {

// [ left | [ center | right ] ] over [ bottom1 bottom2 ]
Layout Sample() {
    Layout l;
    auto top = l.MakeSplit(Axis::Horizontal, 0.2f, l.MakeLeaf({"left"}),
                           l.MakeSplit(Axis::Horizontal, 0.75f, l.MakeLeaf({"center"}), l.MakeLeaf({"right"})));
    l.main = l.MakeSplit(Axis::Vertical, 0.7f, std::move(top), l.MakeLeaf({"bottom1", "bottom2"}));
    return l;
}

uint32_t LeafId(const Layout& l, const char* panel) {
    const Node* n = l.LeafOf(panel);
    return n ? n->id : 0;
}

int CountNodes(const Node& n) { return n.IsLeaf() ? 1 : 1 + CountNodes(*n.a) + CountNodes(*n.b); }

}  // namespace

RM_TEST(Dock_CloseCollapsesSplit) {
    Layout l = Sample();
    CHECK(CountNodes(*l.main) == 7);
    CHECK(l.Close("right"));
    CHECK(!l.Contains("right"));
    // the [center | right] split is replaced by the center leaf
    CHECK(CountNodes(*l.main) == 5);
    CHECK(l.Contains("center"));
    CHECK(!l.Close("right"));  // already closed
}

RM_TEST(Dock_CloseEverythingLeavesEmptyRoot) {
    Layout l = Sample();
    for (const auto& p : l.Panels()) l.Close(p);
    CHECK(l.main != nullptr);
    CHECK(l.main->IsEmptyLeaf());
    CHECK(l.Panels().empty());
    // and a panel can be opened again into it
    CHECK(l.Open("left", 12345));
    CHECK(l.main->IsLeaf() && l.main->tabs.size() == 1);
}

RM_TEST(Dock_CloseActiveTabShowsNeighbour) {
    Layout l = Sample();
    CHECK(l.Activate("bottom2"));
    CHECK(!l.Activate("bottom2"));
    CHECK(l.Close("bottom2"));
    const Node* leaf = l.LeafOf("bottom1");
    CHECK(leaf->active == 0);
}

RM_TEST(Dock_EdgeSplitsTarget) {
    Layout l = Sample();
    const uint32_t center = LeafId(l, "center");
    CHECK(l.Dock("bottom2", center, Zone::Top));
    const Node* moved = l.LeafOf("bottom2");
    CHECK(moved && moved->tabs.size() == 1);
    CHECK(l.LeafOf("bottom1")->tabs.size() == 1);
    // the center leaf is now the lower half of a vertical split whose upper half is bottom2
    const Node* c = l.FindNode(center);
    CHECK(c && c->tabs[0] == "center");
    bool found = false;
    std::vector<Placed> placed;
    Arrange(*l.main, {0, 0, 1000, 1000}, 0, placed);
    for (const auto& p : placed) {
        if (!p.node->IsLeaf() && p.node->a.get() == moved && p.node->b.get() == c) {
            found = p.node->axis == Axis::Vertical && std::fabs(p.node->ratio - 0.5f) < 1e-6f;
        }
    }
    CHECK(found);
}

RM_TEST(Dock_CenterAddsTabAtIndex) {
    Layout l = Sample();
    const uint32_t bottom = LeafId(l, "bottom1");
    CHECK(l.Dock("left", bottom, Zone::Center, 1));
    const Node* leaf = l.LeafOf("left");
    CHECK(leaf->id == bottom);
    CHECK(leaf->tabs.size() == 3);
    CHECK(leaf->tabs[1] == "left");
    CHECK(leaf->active == 1);
    CHECK(!l.Contains("missing"));
}

RM_TEST(Dock_ReorderInsideOneStrip) {
    Layout l;
    l.main = l.MakeLeaf({"a", "b", "c"});
    const uint32_t id = l.main->id;
    // drag "a" to the gap after "c" (index 3 counted with "a" still in the strip)
    CHECK(l.Dock("a", id, Zone::Center, 3));
    CHECK(l.main->tabs == std::vector<std::string>({"b", "c", "a"}));
    // drag "a" to the front
    CHECK(l.Dock("a", id, Zone::Center, 0));
    CHECK(l.main->tabs == std::vector<std::string>({"a", "b", "c"}));
    // dropping on its own place changes nothing
    CHECK(!l.Dock("a", id, Zone::Center, 0));
    CHECK(!l.Dock("a", id, Zone::Center, 1));
}

RM_TEST(Dock_LoneTabOnItselfIsNoChange) {
    Layout l = Sample();
    const uint32_t center = LeafId(l, "center");
    CHECK(!l.Dock("center", center, Zone::Left));
    CHECK(!l.Dock("center", center, Zone::Center));
    CHECK(LeafId(l, "center") == center);  // ids kept, so open windows still find their nodes
    CHECK(CountNodes(*l.main) == 7);
}

RM_TEST(Dock_EdgeOfGroupThatHoldsTheSource) {
    // Dock "right" to the left edge of the [center | right] group it is part of.
    Layout l = Sample();
    const Node* group = l.main->a->b.get();
    const uint32_t groupId = group->id;
    CHECK(l.Dock("right", groupId, Zone::Left));
    // the group collapsed to the center leaf, with "right" now in front of it
    const Node* top = l.main->a.get();
    CHECK(!top->IsLeaf());
    const Node* wrap = top->b.get();
    CHECK(!wrap->IsLeaf());
    CHECK(wrap->a->IsLeaf() && wrap->a->tabs[0] == "right");
    CHECK(wrap->b->IsLeaf() && wrap->b->tabs[0] == "center");
    CHECK(std::fabs(wrap->ratio - EdgeShare(false)) < 1e-6f);
}

RM_TEST(Dock_FloatAndBack) {
    Layout l = Sample();
    const uint32_t fid = l.Float("left", {100, 100, 400, 300});
    CHECK(fid != 0);
    CHECK(l.floating.size() == 1);
    uint32_t site = 99;
    CHECK(l.LeafOf("left", &site) != nullptr && site == fid);
    CHECK(CountNodes(*l.main) == 5);
    // a lone panel floated again only moves its window
    CHECK(l.Float("left", {300, 300, 50, 50}) == fid);
    CHECK(l.floating.size() == 1);
    CHECK(l.floating[0].rect.x == 300 && l.floating[0].rect.w >= 120);
    // another panel docked into the floating window
    CHECK(l.Dock("right", l.floating[0].root->id, Zone::Bottom));
    CHECK(l.SiteOf(LeafId(l, "right")) == fid);
    // the whole window goes back to the main tree
    CHECK(l.DockFloating(fid, LeafId(l, "center"), Zone::Right));
    CHECK(l.floating.empty());
    CHECK(l.LeafOf("left", &site) && site == 0);
    CHECK(l.LeafOf("right", &site) && site == 0);
    CHECK(l.Panels().size() == 5);
}

RM_TEST(Dock_FloatingWindowEmptiedIsRemoved) {
    Layout l = Sample();
    const uint32_t fid = l.Float("left", {0, 0, 300, 300});
    CHECK(l.Dock("left", LeafId(l, "center"), Zone::Center));
    CHECK(l.floating.empty());
    CHECK(l.FindFloating(fid) == nullptr);
    const uint32_t f2 = l.Float("right", {0, 0, 300, 300});
    CHECK(l.CloseFloating(f2));
    CHECK(!l.Contains("right"));
}

RM_TEST(Dock_CannotDockWindowIntoItself) {
    Layout l = Sample();
    const uint32_t fid = l.Float("left", {0, 0, 300, 300});
    CHECK(!l.DockFloating(fid, l.floating[0].root->id, Zone::Left));
    CHECK(l.floating.size() == 1);
}

RM_TEST(Dock_JsonRoundTrip) {
    Layout l = Sample();
    l.Float("right", {10, 20, 500, 400});
    l.Activate("bottom2");
    const json::Value v = l.ToJson();
    const auto back = Layout::FromJson(v);
    CHECK(back.has_value());
    CHECK(back->ToJson() == v);
    CHECK(json::Write(back->ToJson()) == json::Write(v));
    CHECK(back->floating.size() == 1 && back->floating[0].rect.y == 20);
    CHECK(back->LeafOf("bottom2")->active == 1);
}

RM_TEST(Dock_JsonRejectsGarbage) {
    CHECK(!Layout::FromJson(json::Value(3)).has_value());
    auto bad = json::Parse(R"({"main":{"split":"diagonal","a":{"tabs":[]},"b":{"tabs":[]}}})");
    CHECK(bad && !Layout::FromJson(*bad).has_value());
    auto missingChild = json::Parse(R"({"main":{"split":"vertical","a":{"tabs":["x"]}}})");
    CHECK(missingChild && !Layout::FromJson(*missingChild).has_value());
}

RM_TEST(Dock_SanitizeDropsUnknownAndDuplicates) {
    auto v = json::Parse(R"({
      "main": {"split":"horizontal","ratio":7.0,
               "a":{"tabs":["a","gone.plugin.panel"],"active":9},
               "b":{"tabs":["a","b"]}},
      "floating": [{"x":0,"y":0,"w":10,"h":10,"root":{"tabs":["gone.too"]}},
                   {"x":0,"y":0,"w":400,"h":300,"root":{"tabs":["c","b"]}}]
    })");
    CHECK(v.has_value());
    auto l = Layout::FromJson(*v);
    CHECK(l.has_value());
    l->Sanitize({"a", "b", "c"});
    CHECK(l->Panels() == std::vector<std::string>({"a", "b", "c"}));
    // the main split kept "a" left and "b" right; the ratio was clamped
    CHECK(!l->main->IsLeaf());
    CHECK(l->main->ratio <= 0.95f);
    CHECK(l->main->a->active == 0);
    // the window whose only panel is gone was dropped; the duplicate "b" in the other one too
    CHECK(l->floating.size() == 1);
    CHECK(l->floating[0].root->tabs == std::vector<std::string>({"c"}));
}

RM_TEST(Dock_ArrangeAndSplitter) {
    Layout l;
    l.main = l.MakeSplit(Axis::Horizontal, 0.25f, l.MakeLeaf({"a"}), l.MakeLeaf({"b"}));
    std::vector<Placed> placed;
    Arrange(*l.main, {0, 0, 404, 100}, 4, placed);
    CHECK(placed.size() == 3);
    CHECK(placed[1].rect.x == 0 && placed[1].rect.w == 100);
    CHECK(placed[2].rect.x == 104 && placed[2].rect.w == 300);
    const ui::Rect s = SplitterRect(*l.main, {0, 0, 404, 100}, 4);
    CHECK(s.x == 100 && s.w == 4 && s.h == 100);
    // dragging far left stops at the minimum size
    const float r = RatioAt(*l.main, {0, 0, 404, 100}, 4, -50, 80);
    CHECK(std::fabs(r - 0.2f) < 1e-6f);
    const float mid = RatioAt(*l.main, {0, 0, 404, 100}, 4, 202, 80);
    CHECK(std::fabs(mid - 0.5f) < 1e-6f);
    // too small to keep both minimums: split in the middle
    CHECK(RatioAt(*l.main, {0, 0, 100, 100}, 4, 10, 80) == 0.5f);
}

RM_TEST(Dock_ZonePreview) {
    const ui::Rect t{0, 0, 200, 100};
    const ui::Rect left = ZonePreview(t, Zone::Left, true);
    CHECK(left.w == 100 && left.h == 100);
    const ui::Rect bottom = ZonePreview(t, Zone::Bottom, false);
    CHECK(bottom.h == 25 && bottom.y == 75);
    const ui::Rect center = ZonePreview(t, Zone::Center, true);
    CHECK(center.w == 200);
}

// --- Tab strip: a row of tabs longer than the strip -----------------------------------------

namespace {
const float kFour[] = {100, 100, 100, 100};  // content = 4 * 100 + 3 * 2 = 406
}

RM_TEST(Dock_TabStripFits) {
    const TabStripFit roomy = FitTabStrip(kFour, 2, 500, 70, 50);
    CHECK(!roomy.overflow && roomy.view == 500 && roomy.scroll == 0 && roomy.content == 406);
    // too long: the buttons take room, the scroll is kept inside the row
    const TabStripFit tight = FitTabStrip(kFour, 2, 250, 70, 1000);
    CHECK(tight.overflow && tight.view == 180 && tight.scroll == 226);
    CHECK(FitTabStrip(kFour, 2, 250, 70, -5).scroll == 0);
    CHECK(FitTabStrip({}, 2, 250, 70, 10).content == 0);
    CHECK(TabLeft(kFour, 2, 2) == 204);
    CHECK(ClampTabScroll(406, 180, std::nanf("")) == 0);
}

RM_TEST(Dock_TabStripShowsTab) {
    CHECK(ScrollToShowTab(kFour, 2, 180, 0, 2) == 124);   // its right edge to the view's right edge
    CHECK(ScrollToShowTab(kFour, 2, 180, 0, 1) == 22);
    CHECK(ScrollToShowTab(kFour, 2, 180, 226, 0) == 0);   // its left edge to the view's left edge
    CHECK(ScrollToShowTab(kFour, 2, 180, 150, 2) == 150); // already whole: does not move
    const float wide[] = {300};
    CHECK(ScrollToShowTab(wide, 2, 100, 0, 0) == 0);       // wider than the view: its left side
    CHECK(ScrollToShowTab(kFour, 2, 180, 40, 9) == 40);    // no such tab
}

RM_TEST(Dock_TabStripSteps) {
    float s = 0;
    const float right[] = {22, 124, 226, 226};
    for (float want : right) {
        s = StepTabScroll(kFour, 2, 180, s, 1);
        CHECK(s == want);
    }
    const float left[] = {204, 102, 0, 0};
    for (float want : left) {
        s = StepTabScroll(kFour, 2, 180, s, -1);
        CHECK(s == want);
    }
    CHECK(StepTabScroll({}, 2, 180, 30, 1) == 0);
}

RM_TEST(Dock_TabStripInsertIndex) {
    CHECK(TabInsertIndex(kFour, 2, 0) == 0);
    CHECK(TabInsertIndex(kFour, 2, 49) == 0);
    CHECK(TabInsertIndex(kFour, 2, 60) == 1);
    CHECK(TabInsertIndex(kFour, 2, 260) == 3);  // a hidden tab counts like a seen one
    CHECK(TabInsertIndex(kFour, 2, 999) == 4);
}

// --- Named layouts (editor/dock/NamedLayouts): names, save / rename / delete, the save format ---
namespace {

Layout Two(const char* a, const char* b) {
    Layout l;
    l.main = l.MakeSplit(Axis::Horizontal, 0.3f, l.MakeLeaf({a}), l.MakeLeaf({b}));
    return l;
}

size_t CountKey(const json::Value& o, std::string_view key) {
    size_t n = 0;
    for (const auto& m : o.Members()) n += m.first == key ? 1 : 0;
    return n;
}

}  // namespace

RM_TEST(NamedLayouts_Names) {
    CHECK(NamedLayouts::CleanName("  作業  ") == "作業");
    CHECK(NamedLayouts::CleanName("\xE3\x80\x80作業 中\xE3\x80\x80") == "作業 中");  // full-width spaces too
    CHECK(NamedLayouts::CleanName(" \xE3\x80\x80 ").empty());
    NamedLayouts n;
    CHECK(n.Check("") == NamedLayouts::NameCheck::Empty);
    CHECK(n.Check("   ") == NamedLayouts::NameCheck::Empty);
    CHECK(n.FreeName("配置") == "配置");
    CHECK(n.Put("配置", Two("a", "b")));
    CHECK(n.Check(" 配置 ") == NamedLayouts::NameCheck::Taken);
    CHECK(n.Check("配置", "配置") == NamedLayouts::NameCheck::Ok);  // its own name
    CHECK(n.FreeName("配置") == "配置 2");
    CHECK(n.Put("配置 2", Two("a", "b")));
    CHECK(n.FreeName("配置") == "配置 3");
}

RM_TEST(NamedLayouts_PutReplacesInPlace) {
    NamedLayouts n;
    CHECK(!n.Put("  ", Two("a", "b")));
    CHECK(n.Empty());
    CHECK(n.Put("first", Two("a", "b")));
    CHECK(n.Put("second", Two("b", "a")));
    CHECK(n.Put(" first", Two("x", "y")));  // the same name: replaced, not added
    CHECK(n.Entries().size() == 2);
    CHECK(n.Entries()[0].info.name == "first");
    const auto got = n.Get("first", {"x", "y"});
    CHECK(got && got->Contains("x") && got->Contains("y"));
}

RM_TEST(NamedLayouts_GetCleansForKnownPanels) {
    NamedLayouts n;
    n.Put("p", Two("keep", "gone"));
    const auto got = n.Get("p", {"keep"});  // a panel of a plugin that is not installed now
    CHECK(got && got->Contains("keep") && !got->Contains("gone"));
    CHECK(got->main->IsLeaf());  // the empty side folded away
    CHECK(!n.Get("missing", {"keep"}));
}

RM_TEST(NamedLayouts_RenameAndRemove) {
    NamedLayouts n;
    n.Put("a", Two("x", "y"));
    n.Put("b", Two("x", "y"));
    CHECK(!n.Rename("a", "b"));    // taken
    CHECK(!n.Rename("a", "  "));   // empty
    CHECK(!n.Rename("zzz", "c"));  // missing
    CHECK(n.Rename("a", "a"));     // the same name is fine
    CHECK(n.Rename("a", " c "));
    CHECK(n.Find("c") && !n.Find("a"));
    CHECK(n.Entries()[0].info.name == "c");  // keeps its place
    CHECK(n.Remove("c"));
    CHECK(!n.Remove("c"));
    CHECK(n.Entries().size() == 1 && n.Entries()[0].info.name == "b");
}

RM_TEST(NamedLayouts_JsonKeepsWhatItDoesNotKnow) {
    NamedLayouts n;
    n.Put("work", Two("x", "y"));
    const json::Value layout = n.Entries()[0].layout;
    json::Value list = n.ToJson();
    CHECK(list.IsArray() && list.Size() == 1);
    list.At(0).Set("color", "red");                 // a key of a newer version
    json::Value odd = json::Value::MakeObject();
    odd.Set("future", 1);                          // not an entry at all
    list.Push(odd);
    json::Value twin = json::Value::MakeObject();  // a second entry of the same name
    twin.Set("name", "work");
    list.Push(twin);

    const NamedLayouts back = NamedLayouts::FromJson(list);
    CHECK(back.Entries().size() == 1);
    CHECK(back.Entries()[0].layout == layout);
    CHECK(back.Get("work", {"x", "y"}).has_value());
    const json::Value again = back.ToJson();
    CHECK(again.Size() == 3);
    CHECK(again.At(0).Get("color").AsString() == "red");
    CHECK(CountKey(again.At(0), "layout") == 1);  // "layout" is ours, not an unknown key written twice
    CHECK(again.At(1) == odd && again.At(2) == twin);
    CHECK(NamedLayouts::FromJson(json::Value()).Empty());
}

RM_TEST(NamedLayouts_BrokenLayoutIsNotUsed) {
    json::Value list = json::Value::MakeArray();
    json::Value e = json::Value::MakeObject();
    e.Set("name", "broken");
    e.Set("layout", "not a layout");
    list.Push(e);
    const NamedLayouts n = NamedLayouts::FromJson(list);
    CHECK(n.Find("broken"));  // listed (it can be deleted) ...
    CHECK(!n.Get("broken", {"x"}));  // ... but never applied
    CHECK(n.ToJson().At(0).Get("layout").AsString() == "not a layout");
}
