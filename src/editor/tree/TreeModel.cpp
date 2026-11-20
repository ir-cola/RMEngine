#include "editor/tree/TreeModel.h"

#include <algorithm>
#include <unordered_map>

namespace rm::editor::tree {

Icon IconNamed(std::string_view name) {
    if (name == "object") return Icon::Object;
    if (name == "cube") return Icon::Cube;
    if (name == "camera") return Icon::Camera;
    if (name == "light") return Icon::Light;
    if (name == "square") return Icon::Square;
    if (name == "effect") return Icon::Effect;
    if (name == "image") return Icon::Image;
    if (name == "audio") return Icon::Audio;
    return Icon::None;
}


namespace {

constexpr size_t npos = static_cast<size_t>(-1);

char Lower(char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; }

std::unordered_map<ItemId, size_t> IndexOf(std::span<const Item> items) {
    std::unordered_map<ItemId, size_t> m;
    m.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) m.emplace(items[i].id, i);
    return m;
}

// 親の添字。最上位なら npos。親が無い、または子より後ろに並んでいる (ソフトの間違い) ときは
// 最上位として扱い、ツリーがループしないようにする。
std::vector<size_t> ParentIndices(std::span<const Item> items) {
    const auto index = IndexOf(items);
    std::vector<size_t> parent(items.size(), npos);
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].parent == 0) continue;
        const auto it = index.find(items[i].parent);
        if (it != index.end() && it->second < i) parent[i] = it->second;
    }
    return parent;
}

using IdSet = std::unordered_set<ItemId>;
IdSet SetOf(std::span<const ItemId> ids) { return {ids.begin(), ids.end()}; }

// 動いていない `parent` の子のうち、一覧の位置 `stop` より前にあるものの数。
size_t SiblingsBefore(std::span<const Item> items, ItemId parent, size_t stop, const IdSet& moving) {
    size_t n = 0;
    for (size_t i = 0; i < stop && i < items.size(); ++i) {
        if (items[i].parent == parent && !moving.contains(items[i].id)) ++n;
    }
    return n;
}

// 動く項目の中や下には入れない: `parent` から 1 回だけ上へたどる (動く項目ごとにではなく)。
bool ParentAllowed(std::span<const Item> items, ItemId parent, const IdSet& moving) {
    if (parent == 0) return true;
    const ItemIndex index(items);
    ItemId cur = parent;
    for (size_t steps = 0; cur != 0 && steps <= items.size(); ++steps) {
        if (moving.contains(cur)) return false;
        const size_t i = index.Find(cur);
        if (i == npos) break;
        cur = items[i].parent;
    }
    return true;
}

}  // namespace

bool NameMatches(std::string_view name, std::string_view filter) {
    if (filter.empty()) return true;
    if (filter.size() > name.size()) return false;
    for (size_t start = 0; start + filter.size() <= name.size(); ++start) {
        size_t k = 0;
        while (k < filter.size() && Lower(name[start + k]) == Lower(filter[k])) ++k;
        if (k == filter.size()) return true;
    }
    return false;
}

std::vector<Row> VisibleRows(std::span<const Item> items, const std::unordered_set<ItemId>& collapsed, std::string_view filter) {
    const std::vector<size_t> parent = ParentIndices(items);
    std::vector<int> depth(items.size(), 0);
    std::vector<bool> hasChildren(items.size(), false);
    for (size_t i = 0; i < items.size(); ++i) {
        if (parent[i] == npos) continue;
        depth[i] = depth[parent[i]] + 1;
        hasChildren[parent[i]] = true;
    }

    // フィルターがあるとき: 項目が一致するか、その下に一致するものがあれば表示する。
    std::vector<bool> shown(items.size(), filter.empty());
    if (!filter.empty()) {
        for (size_t i = items.size(); i-- > 0;) {
            if (NameMatches(items[i].name, filter)) shown[i] = true;
            if (shown[i] && parent[i] != npos) shown[parent[i]] = true;
        }
    }

    std::vector<Row> rows;
    std::vector<bool> open(items.size(), false);  // その項目は見えていて、子が表示される
    for (size_t i = 0; i < items.size(); ++i) {
        if (!shown[i]) continue;
        if (parent[i] != npos && !open[parent[i]]) continue;
        Row r;
        r.item = i;
        r.depth = depth[i];
        r.hasChildren = hasChildren[i];
        r.expanded = hasChildren[i] && (!filter.empty() || !collapsed.contains(items[i].id));
        open[i] = r.expanded;
        rows.push_back(r);
    }
    return rows;
}

ItemIndex::ItemIndex(std::span<const Item> items) {
    map_.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) map_.emplace(items[i].id, i);
}

size_t ItemIndex::Find(ItemId id) const {
    const auto it = map_.find(id);
    return it == map_.end() ? npos : it->second;
}

size_t Find(std::span<const Item> items, ItemId id) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].id == id) return i;
    }
    return npos;
}

bool IsUnder(std::span<const Item> items, ItemId id, ItemId ancestor) {
    if (id == 0 || ancestor == 0) return false;
    const auto index = IndexOf(items);
    ItemId cur = id;
    for (size_t steps = 0; cur != 0 && steps <= items.size(); ++steps) {
        if (cur == ancestor) return true;
        const auto it = index.find(cur);
        if (it == index.end()) return false;
        cur = items[it->second].parent;
    }
    return false;
}

bool Selection::Contains(ItemId id) const { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

void SelectOnly(Selection& sel, ItemId id) {
    sel.ids.clear();
    if (id != 0) sel.ids.push_back(id);
    sel.anchor = sel.focus = id;
}

void Click(Selection& sel, std::span<const Item> items, std::span<const Row> rows, ItemId id, bool ctrl, bool shift) {
    if (shift) {
        auto rowOf = [&](ItemId x) -> size_t {
            for (size_t r = 0; r < rows.size(); ++r) {
                if (items[rows[r].item].id == x) return r;
            }
            return npos;
        };
        const size_t a = rowOf(sel.anchor), b = rowOf(id);
        if (a != npos && b != npos) {
            if (!ctrl) sel.ids.clear();
            IdSet have = sel.Set();
            for (size_t r = std::min(a, b); r <= std::max(a, b); ++r) {
                const ItemId x = items[rows[r].item].id;
                if (have.insert(x).second) sel.ids.push_back(x);
            }
            sel.focus = id;
            Prune(sel, items);  // 表示順
            return;
        }
    }
    if (ctrl) {
        if (sel.Contains(id)) {
            std::erase(sel.ids, id);
        } else {
            sel.ids.push_back(id);
        }
        sel.anchor = sel.focus = id;
        Prune(sel, items);
        return;
    }
    SelectOnly(sel, id);
}

void SelectAll(Selection& sel, std::span<const Item> items, std::span<const Row> rows) {
    sel.ids.clear();
    for (const Row& r : rows) sel.ids.push_back(items[r.item].id);
    if (!sel.ids.empty() && !sel.Contains(sel.focus)) sel.focus = sel.anchor = sel.ids.front();
}

bool Prune(Selection& sel, std::span<const Item> items) {
    // 毎フレーム: items + selected であり、items x selected にはしない。
    const IdSet selected = sel.Set();
    std::vector<ItemId> kept;
    kept.reserve(sel.ids.size());
    bool anchorThere = sel.anchor == 0, focusThere = sel.focus == 0;
    for (const Item& it : items) {
        if (selected.contains(it.id)) kept.push_back(it.id);
        anchorThere |= it.id == sel.anchor;
        focusThere |= it.id == sel.focus;
    }
    const bool changed = kept != sel.ids;
    sel.ids = std::move(kept);
    if (!anchorThere) sel.anchor = 0;
    if (!focusThere) sel.focus = 0;
    return changed;
}

std::vector<ItemId> Roots(std::span<const Item> items, std::span<const ItemId> ids) {
    const std::vector<size_t> parent = ParentIndices(items);
    const IdSet selected = SetOf(ids);
    // covered[i]: 項目 i が選択中か、選択中のものの下にある (親は一覧の先に来る)。
    std::vector<bool> covered(items.size(), false);
    std::vector<ItemId> out;
    for (size_t i = 0; i < items.size(); ++i) {
        const bool underSelected = parent[i] != npos && covered[parent[i]];
        const bool isSelected = selected.contains(items[i].id);
        covered[i] = underSelected || isSelected;
        if (isSelected && !underSelected) out.push_back(items[i].id);
    }
    return out;
}

std::optional<Drop> DropOnRow(std::span<const Item> items, std::span<const Row> rows, size_t rowIndex, float fraction,
                              std::span<const ItemId> moving) {
    if (rowIndex >= rows.size()) return std::nullopt;
    const Row& row = rows[rowIndex];
    const Item& it = items[row.item];
    const IdSet movingSet = SetOf(moving);
    const bool movingItself = movingSet.contains(it.id);

    Drop d;
    d.target = it.id;
    float before = 0.25f, after = 0.75f;
    if (!it.canHaveChildren) before = after = 0.5f;
    if (fraction < before) {
        d.mark = Drop::Mark::Before;
        d.parent = it.parent;
        d.index = SiblingsBefore(items, it.parent, row.item, movingSet);
    } else if (fraction >= after) {
        d.mark = Drop::Mark::After;
        if (row.expanded) {
            // 開いている項目の下の線は、その最初の子の上にある: 最初の子にあたる。
            d.parent = it.id;
            d.index = 0;
        } else {
            d.parent = it.parent;
            d.index = SiblingsBefore(items, it.parent, row.item, movingSet) + (movingItself ? 0 : 1);
        }
    } else {
        d.mark = Drop::Mark::Inside;
        d.parent = it.id;
        d.index = SiblingsBefore(items, it.id, items.size(), movingSet);
    }
    if (!ParentAllowed(items, d.parent, movingSet)) return std::nullopt;
    return d;
}

Drop DropAtEnd(std::span<const Item> items, std::span<const ItemId> moving) {
    Drop d;
    d.mark = Drop::Mark::End;
    d.parent = 0;
    d.index = SiblingsBefore(items, 0, items.size(), SetOf(moving));
    return d;
}

}  // namespace rm::editor::tree
