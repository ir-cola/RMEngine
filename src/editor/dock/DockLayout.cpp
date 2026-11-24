#include "editor/dock/DockLayout.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace rm::editor::dock {

namespace {

constexpr float kMinRatio = 0.05f;
constexpr int kMinFloatingSize = 120;

template <class F>
void ForEachNode(const Node& n, F&& f) {
    f(n);
    if (!n.IsLeaf()) {
        ForEachNode(*n.a, f);
        ForEachNode(*n.b, f);
    }
}

template <class F>
void ForEachNodeMut(Node& n, F&& f) {
    f(n);
    if (!n.IsLeaf()) {
        ForEachNodeMut(*n.a, f);
        ForEachNodeMut(*n.b, f);
    }
}

const Node* FindIn(const Node& n, uint32_t id) {
    if (n.id == id) return &n;
    if (n.IsLeaf()) return nullptr;
    if (const Node* r = FindIn(*n.a, id)) return r;
    return FindIn(*n.b, id);
}

std::unique_ptr<Node>* FindSlotIn(std::unique_ptr<Node>& slot, uint32_t id) {
    if (!slot) return nullptr;
    if (slot->id == id) return &slot;
    if (slot->IsLeaf()) return nullptr;
    if (auto* r = FindSlotIn(slot->a, id)) return r;
    return FindSlotIn(slot->b, id);
}

const Node* LeafWith(const Node& n, std::string_view panel) {
    if (n.IsLeaf()) {
        for (const auto& t : n.tabs) {
            if (t == panel) return &n;
        }
        return nullptr;
    }
    if (const Node* r = LeafWith(*n.a, panel)) return r;
    return LeafWith(*n.b, panel);
}

const Node* FirstLeaf(const Node& n) { return n.IsLeaf() ? &n : FirstLeaf(*n.a); }

// 空の葉の子を持つ分割を、もう一方の子に置き換える。ルートは空の葉のままでもよい。
void PruneSlot(std::unique_ptr<Node>& slot) {
    if (!slot || slot->IsLeaf()) return;
    PruneSlot(slot->a);
    PruneSlot(slot->b);
    if (slot->a->IsEmptyLeaf()) {
        std::unique_ptr<Node> keep = std::move(slot->b);
        slot = std::move(keep);
    } else if (slot->b->IsEmptyLeaf()) {
        std::unique_ptr<Node> keep = std::move(slot->a);
        slot = std::move(keep);
    }
}

void ClampActive(Node& leaf) {
    const int n = static_cast<int>(leaf.tabs.size());
    leaf.active = n == 0 ? 0 : std::clamp(leaf.active, 0, n - 1);
}

std::unique_ptr<Node> CloneNode(const Node& n) {
    auto c = std::make_unique<Node>();
    c->id = n.id;
    c->axis = n.axis;
    c->ratio = n.ratio;
    c->tabs = n.tabs;
    c->active = n.active;
    if (!n.IsLeaf()) {
        c->a = CloneNode(*n.a);
        c->b = CloneNode(*n.b);
    }
    return c;
}

json::Value NodeToJson(const Node& n) {
    json::Value v = json::Value::MakeObject();
    if (n.IsLeaf()) {
        json::Value tabs = json::Value::MakeArray();
        for (const auto& t : n.tabs) tabs.Push(t);
        v.Set("tabs", std::move(tabs));
        v.Set("active", n.active);
    } else {
        v.Set("split", n.axis == Axis::Horizontal ? "horizontal" : "vertical");
        v.Set("ratio", std::round(static_cast<double>(n.ratio) * 10000.0) / 10000.0);
        v.Set("a", NodeToJson(*n.a));
        v.Set("b", NodeToJson(*n.b));
    }
    return v;
}

std::unique_ptr<Node> NodeFromJson(const json::Value& v) {
    if (!v.IsObject()) return nullptr;
    auto n = std::make_unique<Node>();
    if (const json::Value* split = v.Find("split")) {
        const std::string& s = split->AsString();
        if (s != "horizontal" && s != "vertical") return nullptr;
        n->axis = s == "horizontal" ? Axis::Horizontal : Axis::Vertical;
        n->ratio = static_cast<float>(v.Get("ratio").AsDouble(0.5));
        n->a = NodeFromJson(v.Get("a"));
        n->b = NodeFromJson(v.Get("b"));
        if (!n->a || !n->b) return nullptr;
        return n;
    }
    const json::Value& tabs = v.Get("tabs");
    if (!tabs.IsArray()) return nullptr;
    for (const auto& t : tabs.Items()) {
        if (t.IsString()) n->tabs.push_back(t.AsString());
    }
    n->active = static_cast<int>(v.Get("active").AsInt(0));
    return n;
}

void SanitizeNode(Node& n, const std::unordered_set<std::string>& known, std::unordered_set<std::string>& seen) {
    if (!n.IsLeaf()) {
        if (!std::isfinite(n.ratio)) n.ratio = 0.5f;
        n.ratio = std::clamp(n.ratio, kMinRatio, 1.f - kMinRatio);
        SanitizeNode(*n.a, known, seen);
        SanitizeNode(*n.b, known, seen);
        return;
    }
    std::vector<std::string> kept;
    for (auto& t : n.tabs) {
        if (known.count(t) && seen.insert(t).second) kept.push_back(std::move(t));
    }
    n.tabs = std::move(kept);
    ClampActive(n);
}

}  // namespace

// ---------------------------------------------------------------------------
// 配置
// ---------------------------------------------------------------------------

Layout::Layout() { main = MakeLeaf({}); }

Layout Layout::Clone() const {
    Layout c;
    c.main = CloneNode(*main);
    for (const auto& f : floating) c.floating.push_back({f.id, f.rect, CloneNode(*f.root)});
    c.nextId_ = nextId_;
    return c;
}

std::unique_ptr<Node> Layout::MakeLeaf(std::vector<std::string> tabs, int active) {
    auto n = std::make_unique<Node>();
    n->id = NewId();
    n->tabs = std::move(tabs);
    n->active = active;
    ClampActive(*n);
    return n;
}

std::unique_ptr<Node> Layout::MakeSplit(Axis axis, float ratio, std::unique_ptr<Node> a, std::unique_ptr<Node> b) {
    auto n = std::make_unique<Node>();
    n->id = NewId();
    n->axis = axis;
    n->ratio = ratio;
    n->a = std::move(a);
    n->b = std::move(b);
    return n;
}

bool Layout::Contains(std::string_view panel) const { return LeafOf(panel) != nullptr; }

const Node* Layout::LeafOf(std::string_view panel, uint32_t* floatingId) const {
    if (floatingId) *floatingId = 0;
    if (const Node* n = LeafWith(*main, panel)) return n;
    for (const auto& f : floating) {
        if (const Node* n = LeafWith(*f.root, panel)) {
            if (floatingId) *floatingId = f.id;
            return n;
        }
    }
    return nullptr;
}

const Node* Layout::FindNode(uint32_t id) const {
    if (const Node* n = FindIn(*main, id)) return n;
    for (const auto& f : floating) {
        if (const Node* n = FindIn(*f.root, id)) return n;
    }
    return nullptr;
}

Node* Layout::FindNode(uint32_t id) { return const_cast<Node*>(static_cast<const Layout*>(this)->FindNode(id)); }

uint32_t Layout::SiteOf(uint32_t nodeId) const {
    if (FindIn(*main, nodeId)) return 0;
    for (const auto& f : floating) {
        if (FindIn(*f.root, nodeId)) return f.id;
    }
    return ~0u;
}

Floating* Layout::FindFloating(uint32_t id) {
    for (auto& f : floating) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

std::vector<std::string> Layout::Panels() const {
    std::vector<std::string> out;
    auto collect = [&](const Node& n) {
        if (n.IsLeaf()) out.insert(out.end(), n.tabs.begin(), n.tabs.end());
    };
    ForEachNode(*main, collect);
    for (const auto& f : floating) ForEachNode(*f.root, collect);
    return out;
}

std::unique_ptr<Node>* Layout::FindSlot(uint32_t id) {
    if (auto* s = FindSlotIn(main, id)) return s;
    for (auto& f : floating) {
        if (auto* s = FindSlotIn(f.root, id)) return s;
    }
    return nullptr;
}

std::unique_ptr<Node> Layout::Extract(std::string_view panel, uint32_t* fromLeaf, int* fromIndex) {
    Node* leaf = const_cast<Node*>(LeafOf(panel));
    if (!leaf) return nullptr;
    const auto it = std::find(leaf->tabs.begin(), leaf->tabs.end(), panel);
    const int index = static_cast<int>(it - leaf->tabs.begin());
    if (fromLeaf) *fromLeaf = leaf->id;
    if (fromIndex) *fromIndex = index;
    auto moved = MakeLeaf({*it});
    leaf->tabs.erase(it);
    if (leaf->active > index || leaf->active >= static_cast<int>(leaf->tabs.size())) --leaf->active;
    ClampActive(*leaf);
    return moved;
}

bool Layout::Insert(std::unique_ptr<Node> sub, uint32_t targetId, Zone zone, int tabIndex) {
    std::unique_ptr<Node>* slot = FindSlot(targetId);
    if (!slot) return false;
    Node& target = **slot;
    if (zone == Zone::Center) {
        if (!target.IsLeaf()) return false;
        std::vector<std::string> incoming;
        std::string front;
        ForEachNode(*sub, [&](const Node& n) {
            if (!n.IsLeaf()) return;
            for (size_t i = 0; i < n.tabs.size(); ++i) {
                if (front.empty() && static_cast<int>(i) == n.active) front = n.tabs[i];
                incoming.push_back(n.tabs[i]);
            }
        });
        if (incoming.empty()) return false;
        const int count = static_cast<int>(target.tabs.size());
        const int at = (tabIndex < 0 || tabIndex > count) ? count : tabIndex;
        target.tabs.insert(target.tabs.begin() + at, incoming.begin(), incoming.end());
        const auto it = std::find(target.tabs.begin(), target.tabs.end(), front);
        target.active = static_cast<int>(it - target.tabs.begin());
        ClampActive(target);
        return true;
    }
    const Axis axis = (zone == Zone::Left || zone == Zone::Right) ? Axis::Horizontal : Axis::Vertical;
    const bool first = zone == Zone::Left || zone == Zone::Top;
    const float share = EdgeShare(target.IsLeaf());
    std::unique_ptr<Node> old = std::move(*slot);
    if (first) {
        *slot = MakeSplit(axis, share, std::move(sub), std::move(old));
    } else {
        *slot = MakeSplit(axis, 1.f - share, std::move(old), std::move(sub));
    }
    return true;
}

void Layout::Prune() {
    PruneSlot(main);
    if (!main) main = MakeLeaf({});
    for (auto& f : floating) PruneSlot(f.root);
    std::erase_if(floating, [](const Floating& f) { return !f.root || f.root->IsEmptyLeaf(); });
}

bool Layout::Activate(std::string_view panel) {
    Node* leaf = const_cast<Node*>(LeafOf(panel));
    if (!leaf) return false;
    const int index = static_cast<int>(std::find(leaf->tabs.begin(), leaf->tabs.end(), panel) - leaf->tabs.begin());
    if (leaf->active == index) return false;
    leaf->active = index;
    return true;
}

bool Layout::Close(std::string_view panel) {
    if (!Extract(panel, nullptr, nullptr)) return false;
    Prune();
    return true;
}

bool Layout::Open(std::string_view panel, uint32_t leafId) {
    if (Contains(panel)) return Activate(panel);
    Node* leaf = FindNode(leafId);
    if (!leaf || !leaf->IsLeaf()) leaf = const_cast<Node*>(FirstLeaf(*main));
    leaf->tabs.emplace_back(panel);
    leaf->active = static_cast<int>(leaf->tabs.size()) - 1;
    return true;
}

bool Layout::Dock(std::string_view panel, uint32_t targetId, Zone zone, int tabIndex) {
    const Node* target = FindNode(targetId);
    if (!target || !Contains(panel)) return false;
    if (zone == Zone::Center && !target->IsLeaf()) return false;
    // パネルを自分だけのタブや、自分だけでできたグループの端にドロップしても、何も変わらない。
    uint32_t fromLeaf = 0;
    int fromIndex = 0;
    const Layout before = Clone();
    auto moved = Extract(panel, &fromLeaf, &fromIndex);
    if (zone == Zone::Center && fromLeaf == targetId && tabIndex > fromIndex) --tabIndex;
    if (!Insert(std::move(moved), targetId, zone, tabIndex)) {
        *this = before.Clone();
        return false;
    }
    Prune();
    if (ToJson() == before.ToJson()) {
        *this = before.Clone();  // ウィンドウが指しているノードの id を保つ
        return false;
    }
    return true;
}

bool Layout::DockFloating(uint32_t floatingId, uint32_t targetId, Zone zone) {
    Floating* f = FindFloating(floatingId);
    if (!f || SiteOf(targetId) == floatingId) return false;
    const Node* target = FindNode(targetId);
    if (!target || (zone == Zone::Center && !target->IsLeaf())) return false;
    std::unique_ptr<Node> sub = std::move(f->root);
    std::erase_if(floating, [&](const Floating& x) { return x.id == floatingId; });
    if (!Insert(std::move(sub), targetId, zone, -1)) return false;  // 起こらない: ターゲットは上で確かめた
    Prune();
    return true;
}

uint32_t Layout::Float(std::string_view panel, ScreenRect rect) {
    uint32_t fid = 0;
    const Node* leaf = LeafOf(panel, &fid);
    if (!leaf) return 0;
    rect.w = std::max(rect.w, kMinFloatingSize);
    rect.h = std::max(rect.h, kMinFloatingSize);
    if (fid != 0) {
        Floating* f = FindFloating(fid);
        if (f->root.get() == leaf && leaf->tabs.size() == 1) {
            f->rect = rect;
            return f->id;
        }
    }
    auto moved = Extract(panel, nullptr, nullptr);
    Floating f;
    f.id = NewId();
    f.rect = rect;
    f.root = std::move(moved);
    const uint32_t id = f.id;
    floating.push_back(std::move(f));
    Prune();
    return id;
}

bool Layout::CloseFloating(uint32_t floatingId) {
    const size_t n = std::erase_if(floating, [&](const Floating& f) { return f.id == floatingId; });
    return n > 0;
}

void Layout::Renumber(Node& n) {
    ForEachNodeMut(n, [&](Node& x) { x.id = NewId(); });
}

void Layout::Sanitize(const std::vector<std::string>& knownPanels) {
    const std::unordered_set<std::string> known(knownPanels.begin(), knownPanels.end());
    std::unordered_set<std::string> seen;
    SanitizeNode(*main, known, seen);
    for (auto& f : floating) {
        SanitizeNode(*f.root, known, seen);
        f.rect.w = std::max(f.rect.w, kMinFloatingSize);
        f.rect.h = std::max(f.rect.h, kMinFloatingSize);
    }
    Prune();
}

json::Value Layout::ToJson() const {
    json::Value v = json::Value::MakeObject();
    v.Set("main", NodeToJson(*main));
    json::Value list = json::Value::MakeArray();
    for (const auto& f : floating) {
        json::Value o = json::Value::MakeObject();
        o.Set("x", f.rect.x);
        o.Set("y", f.rect.y);
        o.Set("w", f.rect.w);
        o.Set("h", f.rect.h);
        o.Set("root", NodeToJson(*f.root));
        list.Push(std::move(o));
    }
    v.Set("floating", std::move(list));
    return v;
}

std::optional<Layout> Layout::FromJson(const json::Value& v) {
    if (!v.IsObject()) return std::nullopt;
    Layout l;
    l.main = NodeFromJson(v.Get("main"));
    if (!l.main) return std::nullopt;
    l.Renumber(*l.main);
    for (const auto& o : v.Get("floating").Items()) {
        auto root = NodeFromJson(o.Get("root"));
        if (!root) continue;  // 壊れたウィンドウが 1 つあっても、残りは失わない
        Floating f;
        f.id = l.NewId();
        f.rect = {static_cast<int>(o.Get("x").AsInt()), static_cast<int>(o.Get("y").AsInt()),
                  static_cast<int>(o.Get("w").AsInt(400)), static_cast<int>(o.Get("h").AsInt(300))};
        f.root = std::move(root);
        l.Renumber(*f.root);
        l.floating.push_back(std::move(f));
    }
    return l;
}

// ---------------------------------------------------------------------------
// 幾何
// ---------------------------------------------------------------------------

void SplitChildren(const Node& split, const ui::Rect& r, float gap, ui::Rect& a, ui::Rect& b) {
    if (split.axis == Axis::Horizontal) {
        const float avail = std::max(0.f, r.w - gap);
        const float wa = std::round(avail * split.ratio);
        a = {r.x, r.y, wa, r.h};
        b = {r.x + wa + gap, r.y, avail - wa, r.h};
    } else {
        const float avail = std::max(0.f, r.h - gap);
        const float ha = std::round(avail * split.ratio);
        a = {r.x, r.y, r.w, ha};
        b = {r.x, r.y + ha + gap, r.w, avail - ha};
    }
}

void Arrange(const Node& root, const ui::Rect& area, float gap, std::vector<Placed>& out) {
    out.push_back({&root, area});
    if (root.IsLeaf()) return;
    ui::Rect ra, rb;
    SplitChildren(root, area, gap, ra, rb);
    Arrange(*root.a, ra, gap, out);
    Arrange(*root.b, rb, gap, out);
}

ui::Rect SplitterRect(const Node& split, const ui::Rect& r, float gap) {
    ui::Rect a, b;
    SplitChildren(split, r, gap, a, b);
    if (split.axis == Axis::Horizontal) return {a.Right(), r.y, gap, r.h};
    return {r.x, a.Bottom(), r.w, gap};
}

float RatioAt(const Node& split, const ui::Rect& r, float gap, float mouse, float minSize) {
    const bool h = split.axis == Axis::Horizontal;
    const float start = h ? r.x : r.y;
    const float avail = (h ? r.w : r.h) - gap;
    if (avail <= 0.f) return split.ratio;
    if (avail < minSize * 2.f) return 0.5f;
    const float want = (mouse - start - gap * 0.5f) / avail;
    return std::clamp(want, minSize / avail, 1.f - minSize / avail);
}

float EdgeShare(bool targetIsLeaf) { return targetIsLeaf ? 0.5f : 0.25f; }

ui::Rect ZonePreview(const ui::Rect& t, Zone zone, bool targetIsLeaf) {
    const float s = EdgeShare(targetIsLeaf);
    switch (zone) {
        case Zone::Left: return {t.x, t.y, t.w * s, t.h};
        case Zone::Right: return {t.Right() - t.w * s, t.y, t.w * s, t.h};
        case Zone::Top: return {t.x, t.y, t.w, t.h * s};
        case Zone::Bottom: return {t.x, t.Bottom() - t.h * s, t.w, t.h * s};
        default: return t;
    }
}

// ---------------------------------------------------------------------------
// タブの帯
// ---------------------------------------------------------------------------

float TabLeft(std::span<const float> widths, float spacing, size_t index) {
    float x = 0;
    for (size_t i = 0; i < index && i < widths.size(); ++i) x += widths[i] + spacing;
    return x;
}

float ClampTabScroll(float content, float view, float scroll) {
    if (!std::isfinite(scroll)) return 0.f;
    return std::clamp(scroll, 0.f, std::max(0.f, content - view));
}

TabStripFit FitTabStrip(std::span<const float> widths, float spacing, float available, float buttons, float scroll) {
    TabStripFit f;
    f.content = widths.empty() ? 0.f : TabLeft(widths, spacing, widths.size()) - spacing;
    available = std::max(0.f, available);
    f.overflow = f.content > available + 0.5f;
    f.view = f.overflow ? std::max(0.f, available - buttons) : available;
    f.scroll = f.overflow ? ClampTabScroll(f.content, f.view, scroll) : 0.f;
    return f;
}

float ScrollToShowTab(std::span<const float> widths, float spacing, float view, float scroll, size_t index) {
    if (index >= widths.size()) return scroll;
    const float content = TabLeft(widths, spacing, widths.size()) - spacing;
    const float left = TabLeft(widths, spacing, index);
    const float right = left + widths[index];
    if (right > scroll + view) scroll = right - view;  // 右が切れている: その右端をビューの右端に合わせる
    if (left < scroll) scroll = left;                   // 左が切れている (またはビューより広い): その左端に合わせる
    return ClampTabScroll(content, view, scroll);
}

float StepTabScroll(std::span<const float> widths, float spacing, float view, float scroll, int dir) {
    if (widths.empty()) return 0.f;
    constexpr float eps = 0.5f;
    if (dir > 0) {
        for (size_t i = 0; i < widths.size(); ++i) {
            if (TabLeft(widths, spacing, i) + widths[i] > scroll + view + eps) return ScrollToShowTab(widths, spacing, view, scroll, i);
        }
    } else if (dir < 0) {
        for (size_t i = widths.size(); i-- > 0;) {
            if (TabLeft(widths, spacing, i) < scroll - eps) return ScrollToShowTab(widths, spacing, view, scroll, i);
        }
    }
    const float content = TabLeft(widths, spacing, widths.size()) - spacing;
    return ClampTabScroll(content, view, scroll);
}

int TabInsertIndex(std::span<const float> widths, float spacing, float x) {
    int index = 0;
    for (size_t i = 0; i < widths.size(); ++i) {
        if (TabLeft(widths, spacing, i) + widths[i] * 0.5f < x) ++index;
    }
    return index;
}

int TabMoveIndex(std::span<const float> widths, float spacing, size_t from, float left) {
    if (from >= widths.size()) return 0;
    const float center = left + widths[from] * 0.5f;
    int index = 0;
    float x = 0;  // ドラッグ中のタブを抜いて詰めた並びでの左端
    for (size_t i = 0; i < widths.size(); ++i) {
        if (i == from) continue;
        if (x + widths[i] * 0.5f < center) ++index;
        x += widths[i] + spacing;
    }
    return index;
}

std::vector<float> TabLeftsMoved(std::span<const float> widths, float spacing, size_t from, size_t to) {
    std::vector<size_t> order;
    for (size_t i = 0; i < widths.size(); ++i) {
        if (i != from) order.push_back(i);
    }
    if (from < widths.size()) order.insert(order.begin() + static_cast<std::ptrdiff_t>(std::min(to, order.size())), from);
    std::vector<float> lefts(widths.size(), 0.f);
    float x = 0;
    for (size_t i : order) {
        lefts[i] = x;
        x += widths[i] + spacing;
    }
    return lefts;
}

int TabMoveToDockIndex(size_t from, int to) {
    // Dock は、同じ葉の中で後ろへ動かすとき、取り除いた分を 1 つ引く。
    return to < static_cast<int>(from) ? to : to + 1;
}

float SlideToward(float shown, float target, float dt) {
    if (!std::isfinite(shown)) return target;
    const float k = 1.f - std::exp(-std::clamp(dt, 0.f, 0.1f) * 16.f);  // 1 秒に残りの距離が e^-16 まで縮む
    const float next = shown + (target - shown) * k;
    return std::abs(target - next) < 0.5f ? target : next;
}

}  // namespace rm::editor::dock
