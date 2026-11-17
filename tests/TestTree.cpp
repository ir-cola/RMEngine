// Tests for the core tree (editor/tree/TreeModel): visible rows, search, selection, drop places.

#include "Test.h"

#include "editor/tree/TreeModel.h"

#include <chrono>

using namespace rm::editor::tree;

namespace {

// A          1
//   A1       2
//     A1a    3
//   A2       4
// B          5
// C          6  (cannot have children)
std::vector<Item> Sample() {
    std::vector<Item> v = {
        {1, 0, "A"}, {2, 1, "A1"}, {3, 2, "A1a"}, {4, 1, "A2"}, {5, 0, "B"}, {6, 0, "C"},
    };
    v[5].canHaveChildren = false;
    return v;
}

std::vector<ItemId> Ids(const std::vector<Item>& items, const std::vector<Row>& rows) {
    std::vector<ItemId> out;
    for (const Row& r : rows) out.push_back(items[r.item].id);
    return out;
}

}  // namespace

RM_TEST(Tree_VisibleRows_DepthAndCollapse) {
    const auto items = Sample();
    auto rows = VisibleRows(items, {}, "");
    CHECK((Ids(items, rows) == std::vector<ItemId>{1, 2, 3, 4, 5, 6}));
    CHECK(rows[0].depth == 0 && rows[1].depth == 1 && rows[2].depth == 2 && rows[3].depth == 1);
    CHECK(rows[0].hasChildren && rows[0].expanded);
    CHECK(!rows[2].hasChildren && !rows[2].expanded);

    rows = VisibleRows(items, {2}, "");
    CHECK((Ids(items, rows) == std::vector<ItemId>{1, 2, 4, 5, 6}));
    CHECK(rows[1].hasChildren && !rows[1].expanded);

    rows = VisibleRows(items, {1}, "");
    CHECK((Ids(items, rows) == std::vector<ItemId>{1, 5, 6}));
}

RM_TEST(Tree_Search_ShowsMatchesAndTheirParents) {
    const auto items = Sample();
    // A collapsed item opens while searching.
    auto rows = VisibleRows(items, {1, 2}, "a1A");
    CHECK((Ids(items, rows) == std::vector<ItemId>{1, 2, 3}));
    CHECK(rows[0].expanded && rows[1].expanded);
    rows = VisibleRows(items, {}, "b");
    CHECK((Ids(items, rows) == std::vector<ItemId>{5}));
    rows = VisibleRows(items, {}, "nothing");
    CHECK(rows.empty());
    CHECK(NameMatches("エンティティ 2", "ティ 2"));
    CHECK(!NameMatches("abc", "abcd"));
}

RM_TEST(Tree_BrokenParents_CountAsTopLevel) {
    // A parent listed after its child, or missing: the tree must not loop or hide the item.
    const std::vector<Item> items = {{1, 2, "child first"}, {2, 0, "parent"}, {3, 99, "orphan"}};
    const auto rows = VisibleRows(items, {}, "");
    CHECK(rows.size() == 3);
    CHECK(rows[0].depth == 0 && rows[2].depth == 0);
}

RM_TEST(Tree_Selection_ClickCtrlShift) {
    const auto items = Sample();
    const auto rows = VisibleRows(items, {}, "");
    Selection s;
    Click(s, items, rows, 2, false, false);
    CHECK((s.ids == std::vector<ItemId>{2}));
    Click(s, items, rows, 5, false, true);  // range 2..5
    CHECK((s.ids == std::vector<ItemId>{2, 3, 4, 5}));
    CHECK(s.anchor == 2 && s.focus == 5);
    Click(s, items, rows, 3, true, false);  // toggle off
    CHECK((s.ids == std::vector<ItemId>{2, 4, 5}));
    Click(s, items, rows, 1, true, false);  // toggle on, kept in display order
    CHECK((s.ids == std::vector<ItemId>{1, 2, 4, 5}));
    Click(s, items, rows, 6, true, true);  // Ctrl+Shift adds the range from the anchor (1) to 6
    CHECK((s.ids == std::vector<ItemId>{1, 2, 3, 4, 5, 6}));
    SelectAll(s, items, VisibleRows(items, {1}, ""));
    CHECK((s.ids == std::vector<ItemId>{1, 5, 6}));
}

RM_TEST(Tree_Selection_PruneForgetsGoneItems) {
    auto items = Sample();
    Selection s;
    s.ids = {4, 2};
    s.anchor = s.focus = 4;
    CHECK(Prune(s, items));  // order only
    CHECK((s.ids == std::vector<ItemId>{2, 4}));
    CHECK(!Prune(s, items));
    items.erase(items.begin() + 3);  // 4 is gone
    CHECK(Prune(s, items));
    CHECK((s.ids == std::vector<ItemId>{2}));
    CHECK(s.anchor == 0 && s.focus == 0);
}

RM_TEST(Tree_Roots_DropChildrenOfSelected) {
    const auto items = Sample();
    CHECK((Roots(items, std::vector<ItemId>{3, 1, 5}) == std::vector<ItemId>{1, 5}));
    CHECK((Roots(items, std::vector<ItemId>{3, 4}) == std::vector<ItemId>{3, 4}));
    CHECK(IsUnder(items, 3, 1) && IsUnder(items, 1, 1) && !IsUnder(items, 1, 3) && !IsUnder(items, 5, 1));
}

RM_TEST(Tree_Drop_BeforeInsideAfter) {
    const auto items = Sample();
    const auto rows = VisibleRows(items, {}, "");
    const std::vector<ItemId> moving = {5};
    auto d = DropOnRow(items, rows, 0, 0.1f, moving);  // top of A
    CHECK(d && d->parent == 0 && d->index == 0 && d->mark == Drop::Mark::Before && d->target == 1);
    d = DropOnRow(items, rows, 0, 0.5f, moving);  // middle of A: at the end of its children
    CHECK(d && d->parent == 1 && d->index == 2 && d->mark == Drop::Mark::Inside);
    d = DropOnRow(items, rows, 0, 0.9f, moving);  // bottom of an open A: its first child
    CHECK(d && d->parent == 1 && d->index == 0 && d->mark == Drop::Mark::After);
    d = DropOnRow(items, rows, 3, 0.9f, moving);  // bottom of A2: after it
    CHECK(d && d->parent == 1 && d->index == 2);
    // C cannot have children: halves only. B (moving) is not counted.
    d = DropOnRow(items, rows, 5, 0.4f, moving);
    CHECK(d && d->parent == 0 && d->index == 1 && d->mark == Drop::Mark::Before);
    d = DropOnRow(items, rows, 5, 0.6f, moving);
    CHECK(d && d->parent == 0 && d->index == 2 && d->mark == Drop::Mark::After);
    const Drop end = DropAtEnd(items, moving);
    CHECK(end.parent == 0 && end.index == 2 && end.mark == Drop::Mark::End);
    // A closed item: the bottom edge is after it (not inside).
    const auto closed = VisibleRows(items, {1}, "");
    d = DropOnRow(items, closed, 0, 0.9f, moving);
    CHECK(d && d->parent == 0 && d->index == 1);
}

RM_TEST(Tree_Drop_NeverIntoItself) {
    const auto items = Sample();
    const auto rows = VisibleRows(items, {}, "");
    const std::vector<ItemId> moving = {1};
    CHECK(!DropOnRow(items, rows, 2, 0.5f, moving));  // inside A1a (below A)
    CHECK(!DropOnRow(items, rows, 1, 0.1f, moving));  // before A1 = under A
    CHECK(!DropOnRow(items, rows, 0, 0.5f, moving));  // inside A itself
    auto d = DropOnRow(items, rows, 0, 0.1f, moving);  // before itself: allowed, changes nothing
    CHECK(d && d->parent == 0 && d->index == 0);
    d = DropOnRow(items, rows, 4, 0.9f, moving);  // after B: index counts without A
    CHECK(d && d->parent == 0 && d->index == 1);
    CHECK(!DropOnRow(items, rows, 99, 0.5f, moving));
}

RM_TEST(Tree_IconNamed) {
    CHECK(IconNamed("cube") == Icon::Cube);
    CHECK(IconNamed("object") == Icon::Object);
    CHECK(IconNamed("camera") == Icon::Camera);
    CHECK(IconNamed("light") == Icon::Light);
    CHECK(IconNamed("effect") == Icon::Effect);
    CHECK(IconNamed("teapot") == Icon::None);
    CHECK(IconNamed("") == Icon::None);
}

RM_TEST(Tree_ItemIndex) {
    const auto items = Sample();
    const ItemIndex index(items);
    CHECK(index.Find(4) == 3 && index.Find(1) == 0);
    CHECK(index.Find(99) == static_cast<size_t>(-1) && !index.Has(99) && index.Has(6));
}

// Thousands of items, all selected: every step stays proportional to items + selected (it used to
// look through the selection for each item, and rebuild an index for each moving item).
RM_TEST(Tree_ThousandsAllSelected_StayLight) {
    constexpr ItemId kGroups = 200, kEach = 100;  // 200 parents with 100 children each = 20 200 items
    std::vector<Item> items;
    for (ItemId g = 1; g <= kGroups; ++g) {
        const ItemId parent = g * 1000;
        items.push_back({parent, 0, "group"});
        for (ItemId k = 1; k <= kEach; ++k) items.push_back({parent + k, parent, "child"});
    }
    const auto start = std::chrono::steady_clock::now();

    const std::vector<Row> rows = VisibleRows(items, {}, "");
    CHECK(rows.size() == items.size());
    Selection sel;
    SelectAll(sel, items, rows);
    CHECK(sel.ids.size() == items.size());
    for (int frame = 0; frame < 10; ++frame) CHECK(!Prune(sel, items));  // every frame
    // Shift+click over everything (the range adds what is not selected yet)
    Selection range;
    SelectOnly(range, items.front().id);
    Click(range, items, rows, items.back().id, false, true);
    CHECK(range.ids.size() == items.size());
    // Only the groups move as a whole (their children go with them).
    const auto roots = Roots(items, sel.ids);
    CHECK(roots.size() == kGroups && roots.front() == 1000);
    // Dropping everything somewhere: never into itself, counted without the moving ones.
    CHECK(!DropOnRow(items, rows, 5, 0.5f, sel.ids));
    const Drop end = DropAtEnd(items, sel.ids);
    CHECK(end.index == 0);
    const auto onlyLast = std::vector<ItemId>{items.back().id};
    const auto d = DropOnRow(items, rows, 0, 0.5f, onlyLast);
    CHECK(d && d->parent == 1000 && d->index == kEach);

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 3.0);  // generous: the quadratic version took far longer at this size
}
