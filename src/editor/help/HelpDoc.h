#pragma once
// ヘルプの 1 ページ (純粋。RMTests でテストする)。ページは Markdown の小さな一部で書いたファイル
// (plugins/<作者>/<名前>/help/*.md・核は resources/help/*.md。英語は横の en/ に同じ形で):
//
//   # 見出し 1 (最初のものがページの題名)        ## 見出し 2        ### 見出し 3
//   ふつうの文 (続く行は 1 つの段落。空の行で区切る)
//   - 箇条書き (2 文字下げるごとに 1 段深く)     1. 番号付き
//   > 注意書き (枠で囲む)                       ---  (横線)
//   | 表 | の行 |  (2 行目の |---|---| は区切り)
//   ```  コード (そのまま、等幅)  ```
//   ![説明](画像.png)  (その行だけ。ページのフォルダーから)
//   行の中: **太字**・`コード`・[文字](リンク先)
//
// リンク先: 同じフォルダーからのページ (`move`・`../scene_view/index`)、`/` で始まるページの ID
// (`/core/editor/docking`)、`http(s)://` で始まる Web のページ。

#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::help {

struct Span {
    enum class Kind : uint8_t { Text, Bold, Code, Link };
    Kind kind = Kind::Text;
    std::wstring text;
    std::string target;  // Link: 書いてあるとおりのリンク先 (UTF-8)
};

struct Block {
    enum class Kind : uint8_t { Heading, Paragraph, Bullet, Number, Note, Code, Table, Image, Rule };
    Kind kind = Kind::Paragraph;
    int level = 0;   // Heading: 1..3、Bullet / Number: 下げた段 (0 から)
    int number = 0;  // Number: 書いてある番号
    std::vector<Span> spans;                           // Heading / Paragraph / Bullet / Number / Note
    std::wstring code;                                 // Code (行は \n で)
    std::vector<std::vector<std::vector<Span>>> rows;  // Table: 行 -> 列 -> 文字 (0 行目が見出し)
    std::string image;                                 // Image: ページのフォルダーからのパス (UTF-8)
    std::wstring alt;                                  // Image: 説明
};

struct Doc {
    std::wstring title;  // 最初の「# 」の見出し (無いときは空)
    std::vector<Block> blocks;
};

Doc ParseDoc(std::string_view utf8);
std::vector<Span> ParseInline(std::wstring_view text);
// 文字だけ (書式の印を外したもの)。
std::wstring PlainText(const std::vector<Span>& spans);
// ファイルの最初の「# 」の見出しだけ (一覧を作るとき、全部は読まない)。無いときは空。
std::wstring TitleOf(std::string_view utf8);

// リンク先をページの ID にする。`book`: そのページの本 ("core" か "<作者>/<名前>")、`folder`: リンクを
// 書いたページのフォルダーの ID ("rm_engine/weather/snow"。本の一番上なら book と同じ)。
// Web のページ (http(s)://) はそのまま返す。行き先が無い (".." で本の外へ出る・空) ときは空。
std::string ResolveLink(std::string_view book, std::string_view folder, std::string_view target);
inline bool IsWebLink(std::string_view target) { return target.starts_with("http://") || target.starts_with("https://"); }

}  // namespace rm::editor::help
