#include "editor/help/HelpTree.h"

#include "base/Utf.h"

#include <algorithm>

namespace rm::editor::help {

namespace {

// 作っている途中のフォルダー: 名前 -> 子。
struct Draft {
    std::wstring title;
    bool page = false;
    std::map<std::string, Draft> children;
};

void Place(Draft& root, const PageRef& p) {
    Draft* at = &root;
    std::string_view rest = p.rel;
    for (;;) {
        const size_t slash = rest.find('/');
        const std::string part(rest.substr(0, slash));
        if (slash == std::string_view::npos) {
            if (part == "index") {  // フォルダー自身のページ
                at->page = true;
                if (!p.title.empty()) at->title = p.title;
            } else {
                Draft& leaf = at->children[part];
                leaf.page = true;
                leaf.title = p.title.empty() ? Utf8ToWide(part) : p.title;
            }
            return;
        }
        if (part.empty()) return;  // ("a//b": 置かない)
        at = &at->children[part];
        rest.remove_prefix(slash + 1);
    }
}

Node Finish(const std::string& id, const std::string& rel, const std::string& name, Draft& d,
            const std::map<std::string, std::vector<std::string>>& orders) {
    Node n;
    n.id = id;
    n.title = d.title.empty() ? Utf8ToWide(name) : d.title;
    n.page = d.page;
    std::vector<std::string> names;
    for (const auto& [childName, child] : d.children) names.push_back(childName);  // (名前の順)
    if (const auto it = orders.find(rel); it != orders.end()) {
        const std::vector<std::string>& order = it->second;
        std::stable_sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
            const auto ia = std::find(order.begin(), order.end(), a), ib = std::find(order.begin(), order.end(), b);
            return ia < ib;  // (並びに無いものは end: 後ろに、名前の順のまま)
        });
    }
    for (const std::string& childName : names) {
        const std::string childRel = rel.empty() ? childName : rel + "/" + childName;
        n.children.push_back(Finish(id + "/" + childName, childRel, childName, d.children[childName], orders));
    }
    return n;
}

const Node* FindIn(const Node& n, const std::string& id) {
    if (n.id == id) return &n;
    for (const Node& c : n.children) {
        if (const Node* f = FindIn(c, id)) return f;
    }
    return nullptr;
}

}  // namespace

Node BuildBook(const std::string& bookId, const std::wstring& bookTitle, const std::vector<PageRef>& pages,
               const std::map<std::string, std::vector<std::string>>& orders) {
    Draft root;
    for (const PageRef& p : pages) Place(root, p);
    Node n = Finish(bookId, "", bookId, root, orders);
    n.title = bookTitle;  // (本の名前はプラグインの表示名: 一番上のページの見出しではない)
    return n;
}

std::vector<Node> ArrangeBooks(std::vector<Book> books) {
    std::vector<Node> out;
    std::vector<Book> later;
    for (Book& b : books) {
        if (b.under) {
            later.push_back(std::move(b));
        } else {
            out.push_back(std::move(b.root));
        }
    }
    for (Book& b : later) {
        auto it = std::find_if(out.begin(), out.end(), [&](const Node& n) { return n.id == *b.under; });
        if (it != out.end()) {
            it->children.push_back(std::move(b.root));
        } else {
            out.push_back(std::move(b.root));  // (元の本が無い: 一番上に)
        }
    }
    return out;
}

uint64_t ItemIdOf(const std::string& pageId) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (const unsigned char ch : pageId) {
        h ^= ch;
        h *= 1099511628211ull;
    }
    return h == 0 ? 1 : h;
}

const Node* FindNode(const std::vector<Node>& roots, const std::string& id) {
    for (const Node& r : roots) {
        if (const Node* f = FindIn(r, id)) return f;
    }
    return nullptr;
}

}  // namespace rm::editor::help
