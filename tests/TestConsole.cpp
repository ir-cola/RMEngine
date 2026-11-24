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
