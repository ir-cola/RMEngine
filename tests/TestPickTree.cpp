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
