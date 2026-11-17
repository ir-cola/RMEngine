// Tests for the rows of a list in folders (editor/widgets/PickTree: the インスペクター's 部品を足す).

#include "Test.h"

#include "editor/widgets/PickTree.h"

using namespace rm::editor::w;

namespace {

std::vector<PickItem> Sample() {
    return {
        {L"Transform", L"3D"},       // 0
        {L"メッシュ", L"3D"},         // 1
        {L"キャンバス", L"UI"},       // 2
        {L"剛体", L"物理"},           // 3
        {L"コライダー (2D)", L"物理/2D"},  // 4
        {L"ボタン (UI)", L"UI"},      // 5
        {L"足せる部品は全部付いています", L""},  // 6 (outside every folder)
    };
}

std::wstring Line(const PickTreeRow& r) {
    return std::wstring(static_cast<size_t>(r.depth) * 2, L' ') + (r.isFolder ? (r.open ? L"v " : L"> ") : L"") + r.label;
}

std::vector<std::wstring> Lines(const std::vector<PickTreeRow>& rows) {
    std::vector<std::wstring> out;
    for (const PickTreeRow& r : rows) out.push_back(Line(r));
    return out;
}

}  // namespace

RM_TEST(PickTree_ClosedFolders) {
    const auto items = Sample();
    const auto rows = BuildPickTree(items, L"", {});
    // The top's own items first, then the folders in the order their first item comes - all closed.
    CHECK((Lines(rows) == std::vector<std::wstring>{L"足せる部品は全部付いています", L"> 3D", L"> UI", L"> 物理"}));
    CHECK(rows[0].item == 6 && !rows[0].isFolder);
    // A folder counts what is inside it, its folders too.
    CHECK(rows[1].count == 2 && rows[2].count == 2 && rows[3].count == 2);
    CHECK(rows[3].path == L"物理");
}

RM_TEST(PickTree_OpenFolders) {
    const auto items = Sample();
    const auto rows = BuildPickTree(items, L"", {L"UI", L"物理", L"物理/2D"});
    // Inside a folder: its folders first, then its items (in the list's order), one step in.
    CHECK((Lines(rows) == std::vector<std::wstring>{L"足せる部品は全部付いています", L"> 3D", L"v UI", L"  キャンバス", L"  ボタン (UI)", L"v 物理",
                                                    L"  v 2D", L"    コライダー (2D)", L"  剛体"}));
    CHECK(rows[3].item == 2 && rows[4].item == 5 && rows[7].item == 4 && rows[8].item == 3);
    CHECK(rows[6].path == L"物理/2D" && rows[6].count == 1);
    // A folder inside a closed one stays hidden even when "open".
    const auto closedOuter = BuildPickTree(items, L"", {L"物理/2D"});
    CHECK((Lines(closedOuter) == std::vector<std::wstring>{L"足せる部品は全部付いています", L"> 3D", L"> UI", L"> 物理"}));
}

RM_TEST(PickTree_Search) {
    const auto items = Sample();
    // Only what matches, its folders open (even closed ones), the counts of what was found.
    const auto rows = BuildPickTree(items, L"ui", {});
    CHECK((Lines(rows) == std::vector<std::wstring>{L"v UI", L"  キャンバス", L"  ボタン (UI)"}));  // the folder's name matches: all of it
    CHECK(rows[0].count == 2);
    const auto mesh = BuildPickTree(items, L"メッシュ", {});
    CHECK((Lines(mesh) == std::vector<std::wstring>{L"v 3D", L"  メッシュ"}));
    const auto deep = BuildPickTree(items, L"コライダー", {});
    CHECK((Lines(deep) == std::vector<std::wstring>{L"v 物理", L"  v 2D", L"    コライダー (2D)"}));
    CHECK(deep[0].count == 1);
    CHECK(BuildPickTree(items, L"何も無い", {}).empty());
}

RM_TEST(PickTree_Matches) {
    CHECK(PickMatches(L"Transform", L"trans") && PickMatches(L"Transform", L"FORM"));
    CHECK(PickMatches(L"ボタン (UI)", L"ｕｉ"));  // full-width letters as half-width
    CHECK(PickMatches(L"anything", L""));
    CHECK(!PickMatches(L"UI", L"UII") && !PickMatches(L"剛体", L"体剛"));
}

RM_TEST(PickTree_MenuLevel) {
    // The 階層's right-click: 追加 ▸ (its own items, a line, its folders), then the rest of the menu.
    const std::vector<PickItem> items = {
        {L"空のエンティティ", L"追加"},             // 0
        {L"", L"追加", true},                       // 1 (a line)
        {L"箱", L"追加/3D オブジェクト"},           // 2
        {L"球", L"追加/3D オブジェクト"},           // 3
        {L"スプライト", L"追加/2D オブジェクト"},   // 4
        {L"キャンバス", L"追加/UI"},                // 5
        {L"", L"", true},                           // 6
        {L"コピー", L""},                           // 7
        {L"円柱", L"追加/3D オブジェクト"},         // 8 (later in the list: still in its folder)
        {L"", L"", true},                           // 9 (the end: left out)
    };
    auto lines = [](const std::vector<MenuLevelRow>& rows) {
        std::vector<std::wstring> out;
        for (const MenuLevelRow& r : rows) out.push_back(r.separator ? L"---" : (r.isFolder ? r.label + L" >" : r.label));
        return out;
    };
    const auto top = MenuLevel(items, L"");
    CHECK((lines(top) == std::vector<std::wstring>{L"追加 >", L"---", L"コピー"}));
    CHECK(top[0].path == L"追加" && top[2].item == 7);
    const auto add = MenuLevel(items, L"追加");
    CHECK((lines(add) == std::vector<std::wstring>{L"空のエンティティ", L"---", L"3D オブジェクト >", L"2D オブジェクト >", L"UI >"}));
    CHECK(add[2].path == L"追加/3D オブジェクト" && add[0].item == 0);
    const auto shapes = MenuLevel(items, L"追加/3D オブジェクト");
    CHECK((lines(shapes) == std::vector<std::wstring>{L"箱", L"球", L"円柱"}));
    CHECK(shapes[2].item == 8);
    // Lines: none at the start, none twice (the folder between them took its rows away).
    const std::vector<PickItem> lined = {{L"", L"", true}, {L"A", L""}, {L"", L"", true}, {L"B", L"F"}, {L"", L"", true}, {L"C", L""}};
    CHECK((lines(MenuLevel(lined, L"")) == std::vector<std::wstring>{L"A", L"---", L"F >", L"---", L"C"}));
    const std::vector<PickItem> doubled = {{L"A", L""}, {L"", L"", true}, {L"B", L"F"}, {L"", L"", true}};
    CHECK((lines(MenuLevel(doubled, L"")) == std::vector<std::wstring>{L"A", L"---", L"F >"}));
    CHECK(MenuLevel(items, L"無い").empty());
}
