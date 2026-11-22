// ヘルプ (editor/help) の純粋な部分のテスト: ページの書き方 (Markdown の一部) を読む・リンク先・
// ページの木 (フォルダーの入れ子・並び・つなぎのプラグインは元の本の下)。

#include "Test.h"

#include "editor/help/HelpDoc.h"
#include "editor/help/HelpTree.h"

using namespace rm::editor::help;

RM_TEST(Help_ParseBlocks) {
    const Doc d = ParseDoc(
        "\xEF\xBB\xBF# シーンビュー\r\n"
        "ふつうの文は\n"
        "続く行とつながる。\n"
        "\n"
        "## 動かし方\n"
        "- 右ドラッグ: 見回す\n"
        "  - W A S D: 動く\n"
        "1. 選ぶ\n"
        "2. 動かす\n"
        "> 注意: Play 中は\n"
        "> 保存されない\n"
        "---\n"
        "| キー | すること |\n"
        "|---|---|\n"
        "| F | 選んだ物へ |\n"
        "```\n"
        "t.Translate({0, 0, 1});\n"
        "  // 前へ\n"
        "```\n"
        "![見本](images/view.png)\n"
        "English words\n"
        "join with a space\n");
    CHECK(d.title == L"シーンビュー");
    CHECK(d.blocks.size() == 13);
    CHECK(d.blocks[0].kind == Block::Kind::Heading && d.blocks[0].level == 1);
    CHECK(d.blocks[1].kind == Block::Kind::Paragraph && PlainText(d.blocks[1].spans) == L"ふつうの文は続く行とつながる。");  // (日本語どうしは空白なし)
    CHECK(d.blocks[2].kind == Block::Kind::Heading && d.blocks[2].level == 2);
    CHECK(d.blocks[3].kind == Block::Kind::Bullet && d.blocks[3].level == 0 && PlainText(d.blocks[3].spans) == L"右ドラッグ: 見回す");
    CHECK(d.blocks[4].kind == Block::Kind::Bullet && d.blocks[4].level == 1);
    CHECK(d.blocks[5].kind == Block::Kind::Number && d.blocks[5].number == 1 && d.blocks[6].number == 2);
    CHECK(d.blocks[7].kind == Block::Kind::Note && PlainText(d.blocks[7].spans) == L"注意: Play 中は保存されない");
    CHECK(d.blocks[8].kind == Block::Kind::Rule);
    CHECK(d.blocks[9].kind == Block::Kind::Table && d.blocks[9].rows.size() == 2 && d.blocks[9].rows[1].size() == 2);  // (区切りの行は入らない)
    CHECK(PlainText(d.blocks[9].rows[1][1]) == L"選んだ物へ");
    CHECK(d.blocks[10].kind == Block::Kind::Code && d.blocks[10].code == L"t.Translate({0, 0, 1});\n  // 前へ");  // (コードはそのまま)
    CHECK(d.blocks[11].kind == Block::Kind::Image && d.blocks[11].image == "images/view.png" && d.blocks[11].alt == L"見本");
    // 画像の後の段落: 英語の単語の間には空白。
    CHECK(d.blocks[12].kind == Block::Kind::Paragraph && PlainText(d.blocks[12].spans) == L"English words join with a space");
}

RM_TEST(Help_ParseInline) {
    const auto s = ParseInline(L"**太字**と`code`と[リンク](../move)と \\*星\\*");
    CHECK(s.size() == 6);
    CHECK(s[0].kind == Span::Kind::Bold && s[0].text == L"太字");
    CHECK(s[1].kind == Span::Kind::Text && s[1].text == L"と");
    CHECK(s[2].kind == Span::Kind::Code && s[2].text == L"code");
    CHECK(s[4].kind == Span::Kind::Link && s[4].text == L"リンク" && s[4].target == "../move");
    CHECK(s[5].text == L"と *星*");  // (\* は印ではなく文字)
    // 閉じていない印はそのまま文字。
    CHECK(PlainText(ParseInline(L"**開いたまま と `も")) == L"**開いたまま と `も");
    CHECK(TitleOf("前の文\n\n# 題名 **太い**\n# 次") == L"題名 太い");
    CHECK(TitleOf("見出しなし") == L"");
}

RM_TEST(Help_ResolveLink) {
    CHECK(ResolveLink("rm_engine/weather", "rm_engine/weather/snow", "trails") == "rm_engine/weather/snow/trails");
    CHECK(ResolveLink("rm_engine/weather", "rm_engine/weather/snow", "../rain/index") == "rm_engine/weather/rain");  // (index はフォルダー)
    CHECK(ResolveLink("rm_engine/weather", "rm_engine/weather", "rain.md") == "rm_engine/weather/rain");
    CHECK(ResolveLink("rm_engine/weather", "rm_engine/weather", "../physics") == "");  // (本の外へは出ない)
    CHECK(ResolveLink("core", "core/editor", "/rm_engine/weather/snow") == "rm_engine/weather/snow");
    CHECK(ResolveLink("core", "core", "https://ir-cola.github.io/") == "https://ir-cola.github.io/");
    CHECK(ResolveLink("core", "core", "") == "");
    CHECK(IsWebLink("http://a") && !IsWebLink("file:///c:/x"));
}

RM_TEST(Help_BuildBook) {
    const std::vector<PageRef> pages = {
        {"index", L"天候"},
        {"snow/index", L"雪"},
        {"snow/trails", L"足跡"},
        {"snow/depth", L"積もる高さ"},
        {"rain", L"雨"},
        {"fog", L""},  // (題名なし: ファイルの名前)
    };
    const std::map<std::string, std::vector<std::string>> orders = {{"", {"rain", "snow"}}, {"snow", {"depth"}}};
    const Node n = BuildBook("rm_engine/weather", L"天候 (表示名)", pages, orders);
    CHECK(n.id == "rm_engine/weather" && n.title == L"天候 (表示名)" && n.page);  // (本の名前はプラグインの表示名)
    CHECK(n.children.size() == 3);
    CHECK(n.children[0].id == "rm_engine/weather/rain" && n.children[0].title == L"雨");  // 並びの順
    CHECK(n.children[1].id == "rm_engine/weather/snow" && n.children[1].title == L"雪" && n.children[1].page);
    CHECK(n.children[2].id == "rm_engine/weather/fog" && n.children[2].title == L"fog");  // 並びに無いものは後ろ
    CHECK(n.children[1].children.size() == 2 && n.children[1].children[0].title == L"積もる高さ" && n.children[1].children[1].title == L"足跡");
    // ページの無いフォルダー: 名前が題名、開くと子の一覧。
    const Node m = BuildBook("core", L"本体", {{"editor/docking", L"ドッキング"}}, {});
    CHECK(m.children.size() == 1 && m.children[0].id == "core/editor" && !m.children[0].page && m.children[0].title == L"editor");
    CHECK(FindNode({n, m}, "core/editor/docking") && FindNode({n, m}, "core/editor/docking")->title == L"ドッキング");
    CHECK(!FindNode({n, m}, "core/nothing"));
}

RM_TEST(Help_ConnectorsGoUnderTheirBook) {
    std::vector<Book> books;
    books.push_back({BuildBook("rm_engine/weather_x_physics", L"天候 × 物理", {{"index", L"x"}}, {}), std::string("rm_engine/weather")});
    books.push_back({BuildBook("rm_engine/weather", L"天候", {{"rain", L"雨"}}, {}), std::nullopt});
    books.push_back({BuildBook("rm_engine/physics", L"物理", {}, {}), std::nullopt});
    books.push_back({BuildBook("rm_engine/lonely_x", L"元が無いつなぎ", {}, {}), std::string("rm_engine/gone")});
    const std::vector<Node> out = ArrangeBooks(std::move(books));
    CHECK(out.size() == 3);
    CHECK(out[0].id == "rm_engine/weather" && out[0].children.size() == 2 && out[0].children[1].id == "rm_engine/weather_x_physics");  // 元の本の子の最後
    CHECK(out[1].id == "rm_engine/physics");
    CHECK(out[2].id == "rm_engine/lonely_x");  // (元の本が無い: 一番上に)
    CHECK(ItemIdOf("a") != ItemIdOf("b") && ItemIdOf("") != 0 && ItemIdOf("a") == ItemIdOf("a"));
}
