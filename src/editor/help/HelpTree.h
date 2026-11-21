#pragma once
// ヘルプのページの木 (純粋。RMTests でテストする)。1 つの「本」= 核 (`core`) かプラグイン 1 つ
// (`<作者>/<名前>`)。本の中は、フォルダーの入れ子がそのまま「機能 → 細かい仕様」の入れ子:
//
//   help/index.md                 本の一番上のページ (無くてもよい)
//   help/scene_view/index.md      フォルダー「scene_view」のページ (題名はその見出し)
//   help/scene_view/move.md       その下のページ
//   help/_order.json              そのフォルダーの並び ["scene_view", "settings"]。書いていないものは名前の順で後ろに
//   help/en/...                   英語 (同じ形)
//
// ページの ID は「本 / フォルダー / 名前」(見出しの文字は使わない: 英語にしても、リンクが切れない)。
// つなぎのプラグイン (天候 × 物理) の本は、元のプラグイン (天候) の本の下に入れる (ArrangeBooks)。

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor::help {

struct Node {
    std::string id;      // "rm_engine/weather/snow"
    std::wstring title;  // 一覧に出す名前
    bool page = false;   // 開けるページがある (無いフォルダーは、開くと子の一覧を出す)
    std::vector<Node> children;
};

// 本の中の 1 ページ: フォルダーからの場所 (区切りは /、.md は付けない) と題名。
struct PageRef {
    std::string rel;  // "index"・"scene_view/index"・"scene_view/move"
    std::wstring title;
};

// 本 1 冊の木。`orders`: フォルダー (本の一番上は "") -> その並び。題名の無いページはファイルの名前。
Node BuildBook(const std::string& bookId, const std::wstring& bookTitle, const std::vector<PageRef>& pages,
               const std::map<std::string, std::vector<std::string>>& orders);

struct Book {
    Node root;
    std::optional<std::string> under;  // つなぎ: この本の下に入れる (その本が無ければ一番上に)
};
// 本を並べる (この順): つなぎの本は元の本の子の最後に。
std::vector<Node> ArrangeBooks(std::vector<Book> books);

// ツリーの項目の ID (ページの ID から。0 にはならない)。
uint64_t ItemIdOf(const std::string& pageId);
// 木の中で `id` を探す (無ければ null)。
const Node* FindNode(const std::vector<Node>& roots, const std::string& id);

}  // namespace rm::editor::help
