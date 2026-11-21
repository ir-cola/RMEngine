#pragma once
// ヘルプの本を、ディスクのフォルダーから読む (ヘルプのパネルと Hub のヘルプの窓が使う)。
//   核        <exe>/Help/                        (リポジトリの resources/help/)
//   プラグイン <入れたプラグイン>/<作者>/<名前>/help/ (入っていて使えるものだけ: 入れると出る・消すと消える)
// 英語は各フォルダーの en/ に同じ形で (無いページは日本語で出す。check_lang.ps1 が書き忘れを止める)。
// 書き出したゲームには入らない (エディターだけ)。

#include "editor/help/HelpDoc.h"
#include "editor/help/HelpTree.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor::help {

// 読む本 1 冊: ID ("core" か "<作者>/<名前>")・一覧に出す名前・フォルダー・つなぎなら元の本の ID。
struct BookSource {
    std::string id;
    std::wstring title;
    std::filesystem::path folder;
    std::optional<std::string> under;
};

struct PageFiles {
    std::filesystem::path ja, en;  // en: 無ければ空
    std::string book;              // 本の ID
    std::string folder;            // リンクの元になるフォルダーの ID
};

struct Library {
    std::vector<Node> core;     // タブ「Hub とエディター本体」: 核の本の中身 (本そのものは出さない)
    std::optional<std::string> corePage;  // 核の本の一番上のページ (何も選んでいないときに出す)
    std::vector<Node> plugins;  // タブ「プラグイン」: プラグインの本 (つなぎは元の本の下)
    std::map<std::string, PageFiles> files;  // ページの ID -> ファイル
};

// `english`: 一覧の題名を英語のページから取る (無ければ日本語)。
Library ReadLibrary(const BookSource& core, const std::vector<BookSource>& plugins, bool english);
// ページを読む。`english` で英語のページがあればそれを。
std::optional<Doc> LoadPage(const Library& lib, const std::string& id, bool english, std::string* error);
// ページのファイルのある所 (画像はここから)。
std::filesystem::path PageFolder(const Library& lib, const std::string& id, bool english);

}  // namespace rm::editor::help
