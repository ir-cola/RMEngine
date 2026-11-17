#include "editor/widgets/PickTree.h"

#include <algorithm>
#include <map>

namespace rm::editor::w {

namespace {

// One letter for comparing: full-width Latin letters and digits as half-width, then lower case.
wchar_t Fold(wchar_t ch) {
    if (ch >= 0xFF01 && ch <= 0xFF5E) ch = static_cast<wchar_t>(ch - 0xFF01 + 0x21);
    if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    return ch;
}

// A folder and what is in it (built from the paths, in the order things first come).
struct Node {
    std::wstring name, path;
    std::vector<size_t> folders;  // indexes into the node list
    std::vector<int> items;
    size_t count = 0;  // items inside, deep
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
    // Node 0: the top (no folder).
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
    // The top's items, then its folders; inside a folder, its folders then its items.
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

}  // namespace rm::editor::w
