// ドッキングの木 (editor/dock/DockLayout) のテスト: 編集、刈り込み、保存の形、幾何、収まらない
// タブの帯。そして名前つきの配置 (editor/dock/NamedLayouts)。

#include "Test.h"

#include "editor/dock/DockLayout.h"
#include "editor/dock/NamedLayouts.h"

#include <cmath>

using namespace rm;
using namespace rm::editor::dock;

namespace {

// [ left | [ center | right ] ] の上に [ bottom1 bottom2 ]
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
    // [center | right] の分割が center の葉に置き換わる
    CHECK(CountNodes(*l.main) == 5);
    CHECK(l.Contains("center"));
    CHECK(!l.Close("right"));  // もう閉じている
}

RM_TEST(Dock_CloseEverythingLeavesEmptyRoot) {
    Layout l = Sample();
    for (const auto& p : l.Panels()) l.Close(p);
    CHECK(l.main != nullptr);
    CHECK(l.main->IsEmptyLeaf());
    CHECK(l.Panels().empty());
    // そしてパネルは、またそこに開ける
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
    // center の葉は、上半分が bottom2 の縦分割の下半分になった
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
    // "a" を "c" の後ろのすき間にドラッグ (帯にまだ "a" がある状態で数えて 3 番目)
    CHECK(l.Dock("a", id, Zone::Center, 3));
    CHECK(l.main->tabs == std::vector<std::string>({"b", "c", "a"}));
    // "a" を先頭にドラッグ
    CHECK(l.Dock("a", id, Zone::Center, 0));
    CHECK(l.main->tabs == std::vector<std::string>({"a", "b", "c"}));
    // 自分の場所に落としても何も変わらない
    CHECK(!l.Dock("a", id, Zone::Center, 0));
    CHECK(!l.Dock("a", id, Zone::Center, 1));
}

RM_TEST(Dock_LoneTabOnItselfIsNoChange) {
    Layout l = Sample();
    const uint32_t center = LeafId(l, "center");
    CHECK(!l.Dock("center", center, Zone::Left));
    CHECK(!l.Dock("center", center, Zone::Center));
    CHECK(LeafId(l, "center") == center);  // ID は保つので、開いているウィンドウは今もノードを見つけられる
    CHECK(CountNodes(*l.main) == 7);
}

RM_TEST(Dock_EdgeOfGroupThatHoldsTheSource) {
    // "right" を、それが属する [center | right] のグループの左端にドッキングする。
    Layout l = Sample();
    const Node* group = l.main->a->b.get();
    const uint32_t groupId = group->id;
    CHECK(l.Dock("right", groupId, Zone::Left));
    // グループは center の葉に畳まれ、"right" がその前に来る
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
    // 1 つだけのパネルをまた浮かせても、ウィンドウが動くだけ
    CHECK(l.Float("left", {300, 300, 50, 50}) == fid);
    CHECK(l.floating.size() == 1);
    CHECK(l.floating[0].rect.x == 300 && l.floating[0].rect.w >= 120);
    // 別のパネルが浮かせたウィンドウにドッキングした
    CHECK(l.Dock("right", l.floating[0].root->id, Zone::Bottom));
    CHECK(l.SiteOf(LeafId(l, "right")) == fid);
    // ウィンドウごとメインの木に戻る
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
    // メインの分割は "a" を左、"b" を右に保った。比率は収められた
    CHECK(!l->main->IsLeaf());
    CHECK(l->main->ratio <= 0.95f);
    CHECK(l->main->a->active == 0);
    // パネルが 1 つもなくなったウィンドウは捨てられた。もう一方の重複した "b" も
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
    // 左へ大きくドラッグすると、最小サイズで止まる
    const float r = RatioAt(*l.main, {0, 0, 404, 100}, 4, -50, 80);
    CHECK(std::fabs(r - 0.2f) < 1e-6f);
    const float mid = RatioAt(*l.main, {0, 0, 404, 100}, 4, 202, 80);
    CHECK(std::fabs(mid - 0.5f) < 1e-6f);
    // 小さすぎて両方の最小を保てない: 真ん中で分ける
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

// --- タブの帯: 帯より長いタブの列 -----------------------------------------------

namespace {
const float kFour[] = {100, 100, 100, 100};  // 中身 = 4 * 100 + 3 * 2 = 406
}

RM_TEST(Dock_TabStripFits) {
    const TabStripFit roomy = FitTabStrip(kFour, 2, 500, 70, 50);
    CHECK(!roomy.overflow && roomy.view == 500 && roomy.scroll == 0 && roomy.content == 406);
    // 長すぎる: ボタンが場所を取り、スクロールは列の中に収める
    const TabStripFit tight = FitTabStrip(kFour, 2, 250, 70, 1000);
    CHECK(tight.overflow && tight.view == 180 && tight.scroll == 226);
    CHECK(FitTabStrip(kFour, 2, 250, 70, -5).scroll == 0);
    CHECK(FitTabStrip({}, 2, 250, 70, 10).content == 0);
    CHECK(TabLeft(kFour, 2, 2) == 204);
    CHECK(ClampTabScroll(406, 180, std::nanf("")) == 0);
}

RM_TEST(Dock_TabStripShowsTab) {
    CHECK(ScrollToShowTab(kFour, 2, 180, 0, 2) == 124);   // 右端をビューの右端に
    CHECK(ScrollToShowTab(kFour, 2, 180, 0, 1) == 22);
    CHECK(ScrollToShowTab(kFour, 2, 180, 226, 0) == 0);   // 左端をビューの左端に
    CHECK(ScrollToShowTab(kFour, 2, 180, 150, 2) == 150); // もう全部見えている: 動かない
    const float wide[] = {300};
    CHECK(ScrollToShowTab(wide, 2, 100, 0, 0) == 0);       // ビューより広い: その左側
    CHECK(ScrollToShowTab(kFour, 2, 180, 40, 9) == 40);    // そんなタブはない
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
    CHECK(TabInsertIndex(kFour, 2, 260) == 3);  // 隠れたタブも、見えているタブと同じに数える
    CHECK(TabInsertIndex(kFour, 2, 999) == 4);
}

RM_TEST(Dock_TabMoveWithinStrip) {
    // タブ 1 を少しだけ動かしても場所は変わらない。隣の真ん中を越えたら入れ替わる。
    CHECK(TabMoveIndex(kFour, 2, 1, 102) == 1);
    CHECK(TabMoveIndex(kFour, 2, 1, 40) == 1);
    CHECK(TabMoveIndex(kFour, 2, 1, -10) == 0);
    CHECK(TabMoveIndex(kFour, 2, 1, 160) == 2);
    CHECK(TabMoveIndex(kFour, 2, 1, 999) == 3);
    CHECK(TabMoveIndex(kFour, 2, 9, 50) == 0);  // ない番号
    // 動かした並び: 1 を最後へ -> 0, 2, 3, 1。
    const std::vector<float> lefts = TabLeftsMoved(kFour, 2, 1, 3);
    CHECK(lefts.size() == 4 && lefts[0] == 0 && lefts[2] == 102 && lefts[3] == 204 && lefts[1] == 306);
    const std::vector<float> same = TabLeftsMoved(kFour, 2, 2, 2);
    CHECK(same[0] == 0 && same[1] == 102 && same[2] == 204 && same[3] == 306);
    // Dock の番号: 前へは同じ、後ろへは 1 つ足す (Dock が取り除いた分を引くので)。
    CHECK(TabMoveToDockIndex(2, 0) == 0);
    CHECK(TabMoveToDockIndex(2, 2) == 3);
    CHECK(TabMoveToDockIndex(1, 3) == 4);
    Layout l;
    l.main = l.MakeLeaf({"a", "b", "c", "d"});
    CHECK(l.Dock("b", l.main->id, Zone::Center, TabMoveToDockIndex(1, 3)));
    CHECK(l.main->tabs == std::vector<std::string>({"a", "c", "d", "b"}));
    // 滑らかに近づく: 少しずつ近づき、最後は目標そのもの。
    float x = 0;
    x = SlideToward(x, 100, 1.f / 60);
    CHECK(x > 0 && x < 100);
    for (int i = 0; i < 120; ++i) x = SlideToward(x, 100, 1.f / 60);
    CHECK(x == 100);
    CHECK(SlideToward(NAN, 5, 0.016f) == 5);
}

// --- 名前つきの配置 (editor/dock/NamedLayouts): 名前、保存 / 名前変更 / 削除、保存の形 ---
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
    CHECK(NamedLayouts::CleanName("\xE3\x80\x80作業 中\xE3\x80\x80") == "作業 中");  // 全角スペースも
    CHECK(NamedLayouts::CleanName(" \xE3\x80\x80 ").empty());
    NamedLayouts n;
    CHECK(n.Check("") == NamedLayouts::NameCheck::Empty);
    CHECK(n.Check("   ") == NamedLayouts::NameCheck::Empty);
    CHECK(n.FreeName("配置") == "配置");
    CHECK(n.Put("配置", Two("a", "b")));
    CHECK(n.Check(" 配置 ") == NamedLayouts::NameCheck::Taken);
    CHECK(n.Check("配置", "配置") == NamedLayouts::NameCheck::Ok);  // 自分の名前
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
    CHECK(n.Put(" first", Two("x", "y")));  // 同じ名前: 追加せず置き換える
    CHECK(n.Entries().size() == 2);
    CHECK(n.Entries()[0].info.name == "first");
    const auto got = n.Get("first", {"x", "y"});
    CHECK(got && got->Contains("x") && got->Contains("y"));
}

RM_TEST(NamedLayouts_GetCleansForKnownPanels) {
    NamedLayouts n;
    n.Put("p", Two("keep", "gone"));
    const auto got = n.Get("p", {"keep"});  // 今はインストールされていないプラグインのパネル
    CHECK(got && got->Contains("keep") && !got->Contains("gone"));
    CHECK(got->main->IsLeaf());  // 空になった側は畳まれた
    CHECK(!n.Get("missing", {"keep"}));
}

RM_TEST(NamedLayouts_RenameAndRemove) {
    NamedLayouts n;
    n.Put("a", Two("x", "y"));
    n.Put("b", Two("x", "y"));
    CHECK(!n.Rename("a", "b"));    // 使用中
    CHECK(!n.Rename("a", "  "));   // 空
    CHECK(!n.Rename("zzz", "c"));  // ない
    CHECK(n.Rename("a", "a"));     // 同じ名前なら問題ない
    CHECK(n.Rename("a", " c "));
    CHECK(n.Find("c") && !n.Find("a"));
    CHECK(n.Entries()[0].info.name == "c");  // 場所は保つ
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
    list.At(0).Set("color", "red");                 // 新しい版のキー
    json::Value odd = json::Value::MakeObject();
    odd.Set("future", 1);                          // 項目ですらない
    list.Push(odd);
    json::Value twin = json::Value::MakeObject();  // 同じ名前の 2 つ目の項目
    twin.Set("name", "work");
    list.Push(twin);

    const NamedLayouts back = NamedLayouts::FromJson(list);
    CHECK(back.Entries().size() == 1);
    CHECK(back.Entries()[0].layout == layout);
    CHECK(back.Get("work", {"x", "y"}).has_value());
    const json::Value again = back.ToJson();
    CHECK(again.Size() == 3);
    CHECK(again.At(0).Get("color").AsString() == "red");
    CHECK(CountKey(again.At(0), "layout") == 1);  // "layout" は私たちのもので、知らないキーが 2 回書かれたのではない
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
    CHECK(n.Find("broken"));  // 一覧に出る (削除はできる) ...
    CHECK(!n.Get("broken", {"x"}));  // ... が、適用はしない
    CHECK(n.ToJson().At(0).Get("layout").AsString() == "not a layout");
}

RM_TEST(NamedLayouts_MoveBetweenLists) {
    NamedLayouts project, shared;
    project.Put("a", Two("x", "y"));
    project.Put("b", Two("x", "y"));
    shared.Put("b", Two("y", "x"));
    json::Value list = project.ToJson();
    list.At(0).Set("memo", "kept");  // 知らないキーは項目と一緒に運ばれる
    project = NamedLayouts::FromJson(list);

    CHECK(NamedLayouts::Move(project, " a ", shared));
    CHECK(!project.Find("a") && shared.Find("a"));
    CHECK(shared.Entries().back().info.name == "a");  // 末尾に
    CHECK(shared.ToJson().At(1).Get("memo").AsString() == "kept");
    CHECK(shared.Get("a", {"x", "y"}).has_value());
    CHECK(!NamedLayouts::Move(project, "b", shared));  // そこでは名前が使われている: 何も動かない
    CHECK(project.Find("b") && shared.Entries().size() == 2);
    CHECK(!NamedLayouts::Move(project, "zzz", shared));
    CHECK(!NamedLayouts::Move(shared, "a", shared));
}
