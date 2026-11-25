// コンソールのパネルの見せる行と数 (src/editor/console/ConsoleModel) のテスト: レベルの絞り込み、
// 数は絞り込みと検索に関係なくクリアより後の全部、検索は英字の大文字と小文字を区別しない、
// ツールバーの幅の割り振り。

#include "Test.h"

#include "editor/console/ConsoleModel.h"

#include <vector>

using namespace rm;
using namespace rm::editor;

namespace {

std::vector<log::Entry> Sample() {
    std::vector<log::Entry> v;
    auto add = [&](log::Level level, const char* text) {
        log::Entry e;
        e.level = level;
        e.seq = v.size() + 1;
        e.text = text;
        v.push_back(e);
    };
    add(log::Level::Info, "プロジェクトを開きました");    // 0
    add(log::Level::Warn, "Texture が見つかりません");    // 1
    add(log::Level::Info, "保存しました (Main.rmscene)");  // 2
    add(log::Level::Error, "スクリプトのビルドに失敗");     // 3
    add(log::Level::Info, "TEXTURE を読み直しました");      // 4
    return v;
}

}  // namespace

RM_TEST(Console_AllShownByDefault) {
    const auto entries = Sample();
    const console::Result r = console::Apply(entries, {});
    CHECK((r.visible == std::vector<size_t>{0, 1, 2, 3, 4}));
    CHECK(r.counts.info == 3);
    CHECK(r.counts.warn == 1);
    CHECK(r.counts.error == 1);
    CHECK(r.counts.Of(log::Level::Info) == 3);
    CHECK(r.counts.Of(log::Level::Error) == 1);
}

RM_TEST(Console_LevelFilterKeepsCounts) {
    const auto entries = Sample();
    console::Filter f;
    f.info = false;
    console::Result r = console::Apply(entries, f);
    CHECK((r.visible == std::vector<size_t>{1, 3}));
    CHECK(r.counts.info == 3);  // 隠したレベルも数える
    f.info = true;
    f.warn = false;
    f.error = false;
    r = console::Apply(entries, f);
    CHECK((r.visible == std::vector<size_t>{0, 2, 4}));
    CHECK(r.counts.warn == 1);
    CHECK(r.counts.error == 1);
    f.info = false;
    r = console::Apply(entries, f);
    CHECK(r.visible.empty());
    CHECK(r.counts.info + r.counts.warn + r.counts.error == 5);
}

RM_TEST(Console_SearchIgnoresCase) {
    const auto entries = Sample();
    console::Filter f;
    f.search = "texture";
    console::Result r = console::Apply(entries, f);
    CHECK((r.visible == std::vector<size_t>{1, 4}));
    CHECK(r.counts.info == 3);  // 数は検索に関係ない
    f.search = "TeXtUrE";
    CHECK((console::Apply(entries, f).visible == std::vector<size_t>{1, 4}));
    f.search = "保存";
    CHECK((console::Apply(entries, f).visible == std::vector<size_t>{2}));
    f.search = "main.RMSCENE";
    CHECK((console::Apply(entries, f).visible == std::vector<size_t>{2}));
    f.search = "見つからない文字";
    CHECK(console::Apply(entries, f).visible.empty());
    // 検索とレベルの両方
    f.search = "texture";
    f.warn = false;
    CHECK((console::Apply(entries, f).visible == std::vector<size_t>{4}));
}

RM_TEST(Console_ContainsNoCase) {
    CHECK(console::ContainsNoCase("abc", ""));
    CHECK(console::ContainsNoCase("", ""));
    CHECK(!console::ContainsNoCase("", "a"));
    CHECK(console::ContainsNoCase("Hello World", "o w"));
    CHECK(console::ContainsNoCase("Hello", "HELLO"));
    CHECK(!console::ContainsNoCase("Hell", "hello"));
    CHECK(console::ContainsNoCase("警告: Abc", "警告: aBC"));
    CHECK(!console::ContainsNoCase("ａｂｃ", "ABC"));  // 全角は英字として扱わない
}

RM_TEST(Console_ClearHidesAndUncounts) {
    const auto entries = Sample();
    console::Filter f;
    f.clearedThrough = 3;  // seq 1..3 をクリアした
    console::Result r = console::Apply(entries, f);
    CHECK((r.visible == std::vector<size_t>{3, 4}));
    CHECK(r.counts.info == 1);
    CHECK(r.counts.warn == 0);
    CHECK(r.counts.error == 1);
    f.clearedThrough = 5;  // 全部
    r = console::Apply(entries, f);
    CHECK(r.visible.empty());
    CHECK(r.counts.info + r.counts.warn + r.counts.error == 0);
}

RM_TEST(Console_FitBar) {
    // 広い: ラベルあり、欄は上限
    console::BarFit fit = console::FitBar(800, 400, 250, 90, 220);
    CHECK(!fit.compact);
    CHECK(fit.searchW == 220);
    // 少し狭い: 欄が先に縮む
    fit = console::FitBar(560, 400, 250, 90, 220);
    CHECK(!fit.compact);
    CHECK(fit.searchW == 160);
    fit = console::FitBar(490, 400, 250, 90, 220);
    CHECK(!fit.compact);
    CHECK(fit.searchW == 90);
    // もっと狭い: ラベルを外す (欄はその分また広がる)
    fit = console::FitBar(480, 400, 250, 90, 220);
    CHECK(fit.compact);
    CHECK(fit.searchW == 220);
    fit = console::FitBar(400, 400, 250, 90, 220);
    CHECK(fit.compact);
    CHECK(fit.searchW == 150);
    // それでも入らない: 欄は下限のまま (はみ出した分は切れる)
    fit = console::FitBar(200, 400, 250, 90, 220);
    CHECK(fit.compact);
    CHECK(fit.searchW == 90);
}

// --- 時刻・選択・コピー --------------------------------------------------------------------

RM_TEST(Console_ClockText) {
    CHECK(console::ClockText(0) == "00:00:00");
    // 秒より細かい所は出さない (切り捨て)
    CHECK(console::ClockText(((13ll * 60 + 4) * 60 + 5) * 1000 + 67) == "13:04:05");
    CHECK(console::ClockText(24ll * 3600 * 1000 - 1) == "23:59:59");
    // 日付は出さない (何日目でも、その日の時刻)
    CHECK(console::ClockText(20000ll * 24 * 3600 * 1000 + 9 * 3600 * 1000 + 500) == "09:00:00");
    // 1970 年より前 (負): 前の日の終わり
    CHECK(console::ClockText(-1) == "23:59:59");
    CHECK(console::ClockText(-1000) == "23:59:59");
}

RM_TEST(Console_CopyLine) {
    log::Entry e;
    e.level = log::Level::Warn;
    e.clockMs = ((8ll * 60 + 30) * 60 + 1) * 1000 + 2;
    e.text = "Texture が見つかりません";
    CHECK(console::CopyLine(e) == "08:30:01 [Warn] Texture が見つかりません");
    e.level = log::Level::Error;
    CHECK(console::CopyLine(e) == "08:30:01 [Error] Texture が見つかりません");
    e.level = log::Level::Info;
    CHECK(console::CopyLine(e) == "08:30:01 [Info] Texture が見つかりません");
}

RM_TEST(Console_ClickSelects) {
    const std::vector<uint64_t> order{1, 2, 3, 4, 5};
    console::Selection sel;
    console::Click(sel, order, 2, false, false);
    CHECK((sel.seqs == std::vector<uint64_t>{2}));
    CHECK(sel.anchor == 2);
    // Ctrl: 足す・外す (小さい順のまま)
    console::Click(sel, order, 5, true, false);
    console::Click(sel, order, 1, true, false);
    CHECK((sel.seqs == std::vector<uint64_t>{1, 2, 5}));
    console::Click(sel, order, 2, true, false);
    CHECK((sel.seqs == std::vector<uint64_t>{1, 5}));
    CHECK(sel.anchor == 2);
    CHECK(sel.Contains(5) && !sel.Contains(2));
    // ふつうのクリック: それだけ
    console::Click(sel, order, 3, false, false);
    CHECK((sel.seqs == std::vector<uint64_t>{3}));
}

RM_TEST(Console_ShiftRangeInShownOrder) {
    // 見せている行だけの並び (seq 3 と 5 は絞り込みで隠れている)
    const std::vector<uint64_t> order{1, 2, 4, 6, 7};
    console::Selection sel;
    console::Click(sel, order, 2, false, false);
    console::Click(sel, order, 6, false, true);
    CHECK((sel.seqs == std::vector<uint64_t>{2, 4, 6}));  // 隠れた 3・5 は入らない
    CHECK(sel.anchor == 2);                                // 起点は変わらない
    // 起点より上へ: 置き換え
    console::Click(sel, order, 1, false, true);
    CHECK((sel.seqs == std::vector<uint64_t>{1, 2}));
    // Ctrl+Shift: 範囲を足す
    console::Click(sel, order, 7, true, false);  // 起点 = 7
    CHECK((sel.seqs == std::vector<uint64_t>{1, 2, 7}));
    console::Click(sel, order, 4, true, true);
    CHECK((sel.seqs == std::vector<uint64_t>{1, 2, 4, 6, 7}));
    // 起点が無いときの Shift: ふつうのクリック
    console::Selection fresh;
    console::Click(fresh, order, 4, false, true);
    CHECK((fresh.seqs == std::vector<uint64_t>{4}));
    CHECK(fresh.anchor == 4);
    // 起点が隠れたとき: ふつうのクリック
    console::Selection hidden;
    hidden.anchor = 5;
    console::Click(hidden, order, 6, false, true);
    CHECK((hidden.seqs == std::vector<uint64_t>{6}));
}

RM_TEST(Console_SelectRangeAndAll) {
    const std::vector<uint64_t> order{10, 11, 12, 13};
    console::Selection sel;
    console::SelectRange(sel, order, 12, 10, false);  // 逆向きのドラッグ
    CHECK((sel.seqs == std::vector<uint64_t>{10, 11, 12}));
    CHECK(sel.anchor == 12);
    console::SelectRange(sel, order, 13, 13, true);
    CHECK((sel.seqs == std::vector<uint64_t>{10, 11, 12, 13}));
    console::SelectRange(sel, order, 13, 99, false);  // 並びに無い: 何もしない
    CHECK(sel.seqs.size() == 4);
    console::ClearSelection(sel);
    CHECK(sel.Empty() && sel.anchor == 0);
    console::SelectAll(sel, order);
    CHECK((sel.seqs == std::vector<uint64_t>(order)));
    CHECK(sel.anchor == 10);
    console::SelectAll(sel, {});
    CHECK(sel.Empty());
}

RM_TEST(Console_SelectionSurvivesNewLinesAndPrunes) {
    auto entries = Sample();  // seq 1..5
    const std::vector<uint64_t> order = console::SeqsOf(entries, console::Apply(entries, {}).visible);
    CHECK((order == std::vector<uint64_t>{1, 2, 3, 4, 5}));
    console::Selection sel;
    console::Click(sel, order, 4, false, false);
    console::Click(sel, order, 2, true, false);
    // 新しい行が来ても、選んだ行は同じ
    log::Entry more;
    more.seq = 6;
    more.text = "新しい行";
    entries.push_back(more);
    CHECK(!console::Prune(sel, entries, 0));
    CHECK((sel.seqs == std::vector<uint64_t>{2, 4}));
    // 古い行がログから落ちた (seq 1..2 が無い): その行は外れ、起点 (2) も消える
    entries.erase(entries.begin(), entries.begin() + 2);
    CHECK(console::Prune(sel, entries, 0));
    CHECK((sel.seqs == std::vector<uint64_t>{4}));
    CHECK(sel.anchor == 0);
    // クリアした行も外れる
    CHECK(console::Prune(sel, entries, 4));
    CHECK(sel.Empty());
    // ログが空
    sel.seqs = {7};
    CHECK(console::Prune(sel, {}, 0));
    CHECK(sel.Empty());
}

RM_TEST(Console_CopyTextUsesShownOrder) {
    auto entries = Sample();
    for (size_t i = 0; i < entries.size(); ++i) entries[i].clockMs = static_cast<int64_t>(i) * 1000;
    console::Selection sel;
    sel.seqs = {2, 3, 4};
    console::Filter f;
    std::string text = console::CopyText(entries, console::Apply(entries, f).visible, sel);
    CHECK(text ==
          "00:00:01 [Warn] Texture が見つかりません\r\n"
          "00:00:02 [Info] 保存しました (Main.rmscene)\r\n"
          "00:00:03 [Error] スクリプトのビルドに失敗");
    CHECK(console::CopyText(entries, console::Apply(entries, f).visible, sel, true) ==
          "Texture が見つかりません\r\n保存しました (Main.rmscene)\r\nスクリプトのビルドに失敗");
    // 隠した行はコピーしない
    f.info = false;
    text = console::CopyText(entries, console::Apply(entries, f).visible, sel);
    CHECK(text == "00:00:01 [Warn] Texture が見つかりません\r\n00:00:03 [Error] スクリプトのビルドに失敗");
    // 1 行: 改行を付けない
    sel.seqs = {1};
    CHECK(console::CopyText(entries, console::Apply(entries, {}).visible, sel) == "00:00:00 [Info] プロジェクトを開きました");
    sel.seqs.clear();
    CHECK(console::CopyText(entries, console::Apply(entries, {}).visible, sel).empty());
}

RM_TEST(Console_RowAt) {
    CHECK(console::RowAt(0, 0, 20, 0) == 0);
    CHECK(console::RowAt(-5, 0, 20, 3) == 0);  // 上へはみ出した
    CHECK(console::RowAt(0, 0, 20, 3) == 0);
    CHECK(console::RowAt(19.9f, 0, 20, 3) == 0);
    CHECK(console::RowAt(20, 0, 20, 3) == 1);
    CHECK(console::RowAt(130, 100, 20, 3) == 1);
    CHECK(console::RowAt(1000, 0, 20, 3) == 2);  // 下へはみ出した
}
