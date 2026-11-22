#include "editor/help/HelpLibrary.h"

#include "base/Json.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "platform/Shell.h"

namespace fs = std::filesystem;

namespace rm::editor::help {

namespace {

// フォルダーからの場所 ("scene_view/move")。区切りは /、.md は付けない。
std::string RelOf(const fs::path& root, const fs::path& file) {
    fs::path rel = file.lexically_relative(root);
    rel.replace_extension();
    std::string s = PathToUtf8(rel);
    for (char& ch : s) {
        if (ch == '\\') ch = '/';
    }
    return s;
}

std::string FolderRel(const std::string& rel) {
    const size_t slash = rel.rfind('/');
    return slash == std::string::npos ? std::string() : rel.substr(0, slash);
}

void ReadBook(const BookSource& src, bool english, Library& lib, Node& out) {
    std::vector<PageRef> pages;
    std::map<std::string, std::vector<std::string>> orders;
    std::error_code ec;
    const fs::path en = src.folder / L"en";
    for (fs::recursive_directory_iterator it(src.folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& path = it->path();
        if (it->is_directory(ec) && path == en) {
            it.disable_recursion_pending();  // (英語は日本語のページから探す)
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const std::string rel = RelOf(src.folder, path);
        if (path.filename() == L"_order.json") {
            if (auto bytes = platform::ReadFileBytes(path, nullptr)) {
                if (auto doc = json::Parse(*bytes); doc && doc->IsArray()) {
                    std::vector<std::string>& list = orders[FolderRel(rel)];
                    for (const json::Value& v : doc->Items()) list.push_back(v.AsString());
                }
            }
            continue;
        }
        if (path.extension() != L".md") continue;
        PageFiles files;
        files.ja = path;
        const fs::path enFile = en / path.lexically_relative(src.folder);
        if (fs::is_regular_file(enFile, ec)) files.en = enFile;
        files.book = src.id;
        // 「index」はフォルダーのページ: ID もリンクの元もそのフォルダー。
        const bool index = path.stem() == L"index";
        const std::string folderRel = FolderRel(rel);  // ("scene_view/index" -> "scene_view"、"index" -> "")
        const std::string pageRel = index ? folderRel : rel;
        const std::string id = pageRel.empty() ? src.id : src.id + "/" + pageRel;
        files.folder = index ? id : (folderRel.empty() ? src.id : src.id + "/" + folderRel);
        auto title = platform::ReadFileBytes(english && !files.en.empty() ? files.en : files.ja, nullptr);
        pages.push_back({rel, title ? TitleOf(*title) : std::wstring()});
        lib.files[id] = std::move(files);
    }
    out = BuildBook(src.id, src.title, pages, orders);
}

}  // namespace

Library ReadLibrary(const BookSource& core, const std::vector<BookSource>& plugins, bool english) {
    Library lib;
    Node coreRoot;
    ReadBook(core, english, lib, coreRoot);
    lib.core = std::move(coreRoot.children);
    if (coreRoot.page) lib.corePage = coreRoot.id;
    std::vector<Book> books;
    for (const BookSource& src : plugins) {
        std::error_code ec;
        if (!fs::is_directory(src.folder, ec)) continue;  // (ヘルプの無いプラグイン)
        Book b;
        ReadBook(src, english, lib, b.root);
        b.under = src.under;
        books.push_back(std::move(b));
    }
    lib.plugins = ArrangeBooks(std::move(books));
    return lib;
}

std::optional<Doc> LoadPage(const Library& lib, const std::string& id, bool english, std::string* error) {
    const auto it = lib.files.find(id);
    if (it == lib.files.end()) {
        if (error) *error = "page not found: " + id;
        return std::nullopt;
    }
    const fs::path& file = english && !it->second.en.empty() ? it->second.en : it->second.ja;
    auto bytes = platform::ReadFileBytes(file, error);
    if (!bytes) return std::nullopt;
    return ParseDoc(*bytes);
}

fs::path PageFolder(const Library& lib, const std::string& id, bool english) {
    const auto it = lib.files.find(id);
    if (it == lib.files.end()) return {};
    return (english && !it->second.en.empty() ? it->second.en : it->second.ja).parent_path();
}

}  // namespace rm::editor::help
