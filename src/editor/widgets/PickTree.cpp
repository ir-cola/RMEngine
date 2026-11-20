#include "editor/widgets/PickTree.h"

#include <algorithm>
#include <map>

namespace rm::editor::w {

namespace {

// 比べるための 1 文字: 全角のラテン文字と数字を半角に、そのあと小文字に。
wchar_t Fold(wchar_t ch) {
    if (ch >= 0xFF01 && ch <= 0xFF5E) ch = static_cast<wchar_t>(ch - 0xFF01 + 0x21);
    if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    return ch;
}

// フォルダーと、その中身 (パスから作る。出てきた順)。
struct Node {
    std::wstring name, path;
    std::vector<size_t> folders;  // ノード一覧への添字
    std::vector<int> items;
    size_t count = 0;  // 中の項目の数 (深くまで)
};

std::vector<std::wstring> Parts(std::wstring_view path) {
    std::vector<std::wstring> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find(L'/', start);
        const std::wstring_view part = path.substr(start, slash == std::wstring_view::npos ? std::wstring_view::npos : slash - start);
        if (!part.empty()) parts.emplace_back(part);
        if (slash == std::wstring_view::npos) break;
        start = slash + 1;
    }
    return parts;
}

}  // namespace

bool PickMatches(std::wstring_view text, std::wstring_view search) {
    if (search.empty()) return true;
    if (search.size() > text.size()) return false;
    for (size_t i = 0; i + search.size() <= text.size(); ++i) {
        size_t k = 0;
        while (k < search.size() && Fold(text[i + k]) == Fold(search[k])) ++k;
        if (k == search.size()) return true;
    }
    return false;
}

std::vector<PickTreeRow> BuildPickTree(std::span<const PickItem> items, std::wstring_view search, const std::set<std::wstring>& open) {
    // ノード 0: 最上位 (フォルダーなし)。
    std::vector<Node> nodes(1);
    std::map<std::wstring, size_t> byPath;
    for (size_t i = 0; i < items.size(); ++i) {
        const PickItem& it = items[i];
        if (!search.empty() && !PickMatches(it.label, search) && !PickMatches(it.folder, search)) continue;
        size_t at = 0;
        std::wstring path;
        for (const std::wstring& part : Parts(it.folder)) {
            path += (path.empty() ? L"" : L"/") + part;
            auto found = byPath.find(path);
            if (found == byPath.end()) {
                Node n;
                n.name = part;
                n.path = path;
                nodes.push_back(std::move(n));
                found = byPath.emplace(path, nodes.size() - 1).first;
                nodes[at].folders.push_back(found->second);
            }
            at = found->second;
            ++nodes[at].count;
        }
        nodes[at].items.push_back(static_cast<int>(i));
    }

    std::vector<PickTreeRow> rows;
    // 最上位の項目が先、次にそのフォルダー。フォルダーの中では、そのフォルダーが先、次に項目。
    auto add = [&](auto&& self, size_t n, int depth) -> void {
        const Node& node = nodes[n];
        auto addItems = [&] {
            for (const int i : node.items) {
                PickTreeRow r;
                r.label = items[static_cast<size_t>(i)].label;
                r.path = node.path;
                r.depth = depth;
                r.item = i;
                rows.push_back(std::move(r));
            }
        };
        if (n == 0) addItems();
        for (const size_t f : node.folders) {
            const Node& sub = nodes[f];
            PickTreeRow r;
            r.isFolder = true;
            r.label = sub.name;
            r.path = sub.path;
            r.depth = depth;
            r.count = sub.count;
            r.open = !search.empty() || open.contains(sub.path);
            const bool show = r.open;
            rows.push_back(std::move(r));
            if (show) self(self, f, depth + 1);
        }
        if (n != 0) addItems();
    };
    add(add, 0, 0);
    return rows;
}

std::vector<MenuLevelRow> MenuLevel(std::span<const PickItem> items, std::wstring_view folder) {
    const std::vector<std::wstring> at = Parts(folder);
    std::vector<MenuLevelRow> rows;
    std::set<std::wstring> seen;
    for (size_t i = 0; i < items.size(); ++i) {
        const std::vector<std::wstring> parts = Parts(items[i].folder);
        if (parts.size() < at.size() || !std::equal(at.begin(), at.end(), parts.begin())) continue;  // このメニューの中ではない
        MenuLevelRow r;
        if (parts.size() == at.size()) {
            r.separator = items[i].separator;
            r.label = items[i].label;
            r.item = static_cast<int>(i);
        } else {
            std::wstring path;
            for (size_t k = 0; k <= at.size(); ++k) path += (k ? L"/" : L"") + parts[k];
            if (!seen.insert(path).second) continue;
            r.isFolder = true;
            r.label = parts[at.size()];
            r.path = std::move(path);
        }
        if (r.separator && (rows.empty() || rows.back().separator)) continue;
        rows.push_back(std::move(r));
    }
    if (!rows.empty() && rows.back().separator) rows.pop_back();
    return rows;
}

}  // namespace rm::editor::w
