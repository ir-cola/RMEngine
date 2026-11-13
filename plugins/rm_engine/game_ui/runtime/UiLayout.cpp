#include "rm_engine/game_ui/runtime/UiLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace rm::game_ui {

namespace {

constexpr float kPi = 3.14159265358979323846f;

template <class T>
const T* PartOf(const game_production::SceneEntity& e) {
    const game_production::Component* c = e.FindComponent(reflect::TypeOf<T>().name);
    return c && c->Known() ? static_cast<const T*>(c->value.Get()) : nullptr;
}

Vec2 Center(const UiNode& n) { return Apply(n.self, {n.box.x + n.box.w * 0.5f, n.box.y + n.box.h * 0.5f}); }

bool Pressable(const UiButton* b) { return b && b->interactable; }

}  // namespace

Affine operator*(const Affine& m, const Affine& n) {
    Affine r;
    r.a = m.a * n.a + m.c * n.b;
    r.b = m.b * n.a + m.d * n.b;
    r.c = m.a * n.c + m.c * n.d;
    r.d = m.b * n.c + m.d * n.d;
    r.tx = m.a * n.tx + m.c * n.ty + m.tx;
    r.ty = m.b * n.tx + m.d * n.ty + m.ty;
    return r;
}

Vec2 Apply(const Affine& m, Vec2 p) { return {m.a * p.x + m.c * p.y + m.tx, m.b * p.x + m.d * p.y + m.ty}; }

std::optional<Affine> Invert(const Affine& m) {
    const float det = m.a * m.d - m.b * m.c;
    if (std::abs(det) < 1e-12f || !std::isfinite(det)) return std::nullopt;
    const float k = 1.f / det;
    Affine r;
    r.a = m.d * k;
    r.b = -m.b * k;
    r.c = -m.c * k;
    r.d = m.a * k;
    r.tx = -(r.a * m.tx + r.c * m.ty);
    r.ty = -(r.b * m.tx + r.d * m.ty);
    return r;
}

Affine Translate(Vec2 t) { return {1, 0, 0, 1, t.x, t.y}; }
Affine Scaling(float sx, float sy) { return {sx, 0, 0, sy, 0, 0}; }
Affine Rotation(float degrees) {
    const float s = std::sin(degrees * kPi / 180.f), c = std::cos(degrees * kPi / 180.f);
    return {c, s, -s, c, 0, 0};  // (1, 0) -> (c, s): with Y down, a clockwise turn
}
Affine About(Vec2 pivot, float degrees, float scale) {
    if (degrees == 0 && scale == 1) return {};
    return Translate(pivot) * Rotation(degrees) * Scaling(scale, scale) * Translate({-pivot.x, -pivot.y});
}

void AnchorRange(UiAnchor anchor, Vec2& min, Vec2& max) {
    switch (anchor) {
        case UiAnchor::TopLeft: min = max = {0, 0}; return;
        case UiAnchor::Top: min = max = {0.5f, 0}; return;
        case UiAnchor::TopRight: min = max = {1, 0}; return;
        case UiAnchor::Left: min = max = {0, 0.5f}; return;
        case UiAnchor::Center: min = max = {0.5f, 0.5f}; return;
        case UiAnchor::Right: min = max = {1, 0.5f}; return;
        case UiAnchor::BottomLeft: min = max = {0, 1}; return;
        case UiAnchor::Bottom: min = max = {0.5f, 1}; return;
        case UiAnchor::BottomRight: min = max = {1, 1}; return;
        case UiAnchor::StretchTop: min = {0, 0}, max = {1, 0}; return;
        case UiAnchor::StretchMiddle: min = {0, 0.5f}, max = {1, 0.5f}; return;
        case UiAnchor::StretchBottom: min = {0, 1}, max = {1, 1}; return;
        case UiAnchor::StretchLeft: min = {0, 0}, max = {0, 1}; return;
        case UiAnchor::StretchCenter: min = {0.5f, 0}, max = {0.5f, 1}; return;
        case UiAnchor::StretchRight: min = {1, 0}, max = {1, 1}; return;
        case UiAnchor::Stretch: min = {0, 0}, max = {1, 1}; return;
    }
    min = max = {0.5f, 0.5f};  // (a value of a newer version)
}

UiBox PlaceBox(const UiRect& r, const UiBox& parent) {
    Vec2 mn, mx;
    AnchorRange(r.anchor, mn, mx);
    // The anchors' box in the parent; the size adds to it (a point anchor: the size is the size).
    const float ax = parent.x + parent.w * mn.x, ay = parent.y + parent.h * mn.y;
    const float aw = parent.w * (mx.x - mn.x), ah = parent.h * (mx.y - mn.y);
    const float w = std::max(0.f, aw + r.size.x), h = std::max(0.f, ah + r.size.y);
    // The 中心 sits at the anchors' box's point of the same fraction, moved by 位置.
    const float px = ax + aw * r.pivot.x + r.position.x, py = ay + ah * r.pivot.y + r.position.y;
    return {px - w * r.pivot.x, py - h * r.pivot.y, w, h};
}

Vec2 PivotPoint(const UiRect& r, const UiBox& box) { return {box.x + box.w * r.pivot.x, box.y + box.h * r.pivot.y}; }

float CanvasScale(UiScaleMode mode, Vec2 reference, Vec2 area) {
    const float rw = std::max(reference.x, 1.f), rh = std::max(reference.y, 1.f);
    const float sx = area.x / rw, sy = area.y / rh;
    float k = 1;
    switch (mode) {
        case UiScaleMode::Fit: k = std::min(sx, sy); break;
        case UiScaleMode::Fill: k = std::max(sx, sy); break;
        case UiScaleMode::Width: k = sx; break;
        case UiScaleMode::Height: k = sy; break;
        case UiScaleMode::None: k = 1; break;
    }
    return std::isfinite(k) && k > 1e-4f ? k : 1e-4f;
}

UiBox SafeBox(Vec2 screen, Vec2 topLeft, Vec2 bottomRight) {
    const float l = std::max(0.f, topLeft.x), t = std::max(0.f, topLeft.y);
    const float w = std::max(0.f, screen.x - l - std::max(0.f, bottomRight.x));
    const float h = std::max(0.f, screen.y - t - std::max(0.f, bottomRight.y));
    return {std::min(l, screen.x), std::min(t, screen.y), w, h};
}

const UiNode* UiTree::Find(int entity) const {
    for (const UiNode& n : nodes) {
        if (n.entity == entity) return &n;
    }
    return nullptr;
}

UiTree BuildUi(const game_production::Scene& scene, const UiScreen& screen, std::span<const int> alsoShow) {
    UiTree tree;
    // The 階層: each entity's children in its order (the last tie-break of 手前の順).
    std::unordered_map<int, std::vector<const game_production::SceneEntity*>> children;
    for (const auto& t : scene.TreeOrder()) children[t.entity->data.parent].push_back(t.entity);
    auto shownFully = [&](const game_production::SceneEntity* e) {
        for (size_t steps = 0; e && steps <= scene.Entities().size(); ++steps, e = scene.Find(e->data.parent)) {
            if (!e->data.active) return false;
        }
        return true;
    };
    // The canvases (not under another canvas), switched on with what holds them - or holding one of
    // `alsoShow`.
    std::unordered_set<int> forced;
    for (const int id : alsoShow) {
        int canvas = 0;
        size_t steps = 0;
        for (const game_production::SceneEntity* e = scene.Find(id); e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
            if (PartOf<UiCanvas>(*e)) canvas = e->data.id;  // (the outermost wins)
        }
        if (canvas) forced.insert(canvas);
    }
    struct CanvasAt {
        const game_production::SceneEntity* e;
        const UiCanvas* c;
    };
    std::vector<CanvasAt> canvases;
    for (const auto& t : scene.TreeOrder()) {
        const UiCanvas* c = PartOf<UiCanvas>(*t.entity);
        if (!c) continue;
        bool nested = false;
        for (const game_production::SceneEntity* p = scene.Find(t.entity->data.parent); p && !nested; p = scene.Find(p->data.parent)) {
            nested = PartOf<UiCanvas>(*p) != nullptr;
        }
        if (nested) continue;
        if (!forced.contains(t.entity->data.id) && !shownFully(t.entity)) continue;
        canvases.push_back({t.entity, c});
    }
    std::stable_sort(canvases.begin(), canvases.end(), [](const CanvasAt& a, const CanvasAt& b) { return a.c->order < b.c->order; });

    for (const CanvasAt& ca : canvases) {
        const UiBox area = ca.c->safeArea ? SafeBox(screen.size, screen.safeTopLeft, screen.safeBottomRight) : UiBox{0, 0, screen.size.x, screen.size.y};
        const float k = CanvasScale(ca.c->scale, ca.c->reference, {area.w, area.h});
        UiNode root;
        root.entity = ca.e->data.id;
        root.canvas = root.entity;
        root.box = {0, 0, area.w / k, area.h / k};
        root.toScreen = root.self = Translate({area.x, area.y}) * Scaling(k, k);
        root.canvasPart = ca.c;
        root.place = PartOf<UiRect>(*ca.e);
        root.text = PartOf<UiText>(*ca.e);
        root.image = PartOf<UiImage>(*ca.e);
        root.bar = PartOf<UiBar>(*ca.e);
        tree.nodes.push_back(root);
        // Depth first; a thing without UI の配置 passes its parent's box to its children.
        struct Step {
            const game_production::SceneEntity* e;
            int parentNode;
        };
        std::vector<Step> stack;
        auto push = [&](int parentId, int parentNode) {
            auto it = children.find(parentId);
            if (it == children.end()) return;
            std::vector<const game_production::SceneEntity*> kids = it->second;
            std::stable_sort(kids.begin(), kids.end(), [&](const auto* a, const auto* b) {
                const UiRect* ra = PartOf<UiRect>(*a);
                const UiRect* rb = PartOf<UiRect>(*b);
                return (ra ? ra->order : 0) < (rb ? rb->order : 0);
            });
            for (auto k2 = kids.rbegin(); k2 != kids.rend(); ++k2) stack.push_back({*k2, parentNode});  // (popped in order)
        };
        push(root.entity, 0 + static_cast<int>(tree.nodes.size()) - 1);
        size_t guard = 0;
        while (!stack.empty() && guard++ <= scene.Entities().size() * 2 + 8) {
            const Step s = stack.back();
            stack.pop_back();
            if (!s.e->data.active) continue;  // switched off: with what is under it
            const UiNode& parent = tree.nodes[static_cast<size_t>(s.parentNode)];
            const UiRect* place = PartOf<UiRect>(*s.e);
            if (!place) {
                push(s.e->data.id, s.parentNode);  // (not one: its children go into the same box)
                continue;
            }
            UiNode n;
            n.entity = s.e->data.id;
            n.canvas = root.entity;
            n.parent = s.parentNode;
            n.place = place;
            n.text = PartOf<UiText>(*s.e);
            n.image = PartOf<UiImage>(*s.e);
            n.button = PartOf<UiButton>(*s.e);
            n.bar = PartOf<UiBar>(*s.e);
            n.box = PlaceBox(*place, parent.box);
            n.toScreen = parent.self;
            n.self = parent.self * About(PivotPoint(*place, n.box), place->rotation, n.button ? ButtonScale(*n.button) : 1.f);
            tree.nodes.push_back(n);
            push(n.entity, static_cast<int>(tree.nodes.size()) - 1);
        }
    }
    return tree;
}

// --- The mouse, the keys -----------------------------------------------------------------------

bool Hits(const UiNode& n, Vec2 p) {
    const auto inv = Invert(n.self);
    return inv && n.box.Contains(Apply(*inv, p));
}

int TopAt(const UiTree& tree, Vec2 p) {
    for (auto it = tree.nodes.rbegin(); it != tree.nodes.rend(); ++it) {
        if (it->parent < 0) continue;  // a canvas itself takes nothing (a window may be partly empty)
        if (!(it->image || it->bar || it->button)) continue;
        if (Hits(*it, p)) return it->entity;
    }
    return 0;
}

int ButtonOf(const UiTree& tree, int entity) {
    for (const UiNode* n = tree.Find(entity); n;) {
        if (n->button) return n->entity;
        if (n->parent < 0) break;
        n = &tree.nodes[static_cast<size_t>(n->parent)];
    }
    return 0;
}

int ButtonAt(const UiTree& tree, Vec2 p) {
    const int b = ButtonOf(tree, TopAt(tree, p));
    const UiNode* n = b ? tree.Find(b) : nullptr;
    return n && Pressable(n->button) ? b : 0;
}

PointerResult StepPointer(const UiTree& tree, std::optional<Vec2> p, bool down, bool wentDown, bool wentUp, PointerState& state) {
    PointerResult r;
    r.hovered = p ? ButtonAt(tree, *p) : 0;
    if (wentDown) state.pressed = r.hovered;
    if (state.pressed && !tree.Find(state.pressed)) state.pressed = 0;  // gone (switched off, destroyed)
    if (wentUp || !down) {
        if (wentUp && state.pressed && state.pressed == r.hovered) r.clicked = state.pressed;
        state.pressed = 0;
    }
    if (state.pressed && state.pressed == r.hovered) r.held = state.pressed;
    return r;
}

int NavigationCanvas(const UiTree& tree) {
    for (auto it = tree.nodes.rbegin(); it != tree.nodes.rend(); ++it) {
        if (!Pressable(it->button)) continue;
        const UiNode* c = tree.Find(it->canvas);
        if (!c || !c->canvasPart || !c->canvasPart->navigation) continue;
        // The frontmost canvas with one: its own, since nodes go back to front by canvas.
        return it->canvas;
    }
    return 0;
}

int FirstFocus(const UiTree& tree, int canvas, int preferred) {
    int best = 0;
    Vec2 bestAt{};
    for (const UiNode& n : tree.nodes) {
        if (n.canvas != canvas || !Pressable(n.button)) continue;
        if (n.entity == preferred) return preferred;
        const Vec2 c = Center(n);
        if (!best || c.y < bestAt.y - 0.5f || (std::abs(c.y - bestAt.y) <= 0.5f && c.x < bestAt.x)) {
            best = n.entity;
            bestAt = c;
        }
    }
    return best;
}

int MoveFocus(const UiTree& tree, int canvas, int current, int dx, int dy) {
    const UiNode* from = tree.Find(current);
    if (!from || (dx == 0 && dy == 0)) return current;
    const Vec2 o = Center(*from);
    int best = current;
    float bestScore = std::numeric_limits<float>::max();
    for (const UiNode& n : tree.nodes) {
        if (n.canvas != canvas || n.entity == current || !Pressable(n.button)) continue;
        const Vec2 c = Center(n);
        const float ax = c.x - o.x, ay = c.y - o.y;
        const float along = ax * static_cast<float>(dx) + ay * static_cast<float>(dy);
        if (along <= 0.5f) continue;  // not that way
        const float aside = std::abs(ax * static_cast<float>(dy) - ay * static_cast<float>(dx));
        const float score = along + aside * 2.f;
        if (score < bestScore) {
            bestScore = score;
            best = n.entity;
        }
    }
    return best;
}

// --- How a button looks --------------------------------------------------------------------------

Color ButtonColor(const UiButton& b) {
    if (!b.interactable) return b.disabled;
    switch (static_cast<UiButtonState>(b.state)) {
        case UiButtonState::Pressed: return b.pressed;
        case UiButtonState::Hover: return b.hover;
        case UiButtonState::Disabled: return b.disabled;
        default: return b.focused ? b.hover : b.normal;
    }
}

float ButtonScale(const UiButton& b) {
    if (!b.interactable) return 1;
    switch (static_cast<UiButtonState>(b.state)) {
        case UiButtonState::Pressed: return b.pressedScale;
        case UiButtonState::Hover: return b.hoverScale;
        default: return b.focused ? b.hoverScale : 1.f;
    }
}

Color Multiply(Color a, Color b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }

UiBox BarFill(const UiBar& bar, const UiBox& box) {
    const float pad = std::max(0.f, bar.padding);
    UiBox in{box.x + pad, box.y + pad, std::max(0.f, box.w - 2 * pad), std::max(0.f, box.h - 2 * pad)};
    const float v = std::isfinite(bar.value) ? std::clamp(bar.value, 0.f, 1.f) : 0.f;
    switch (bar.direction) {
        case UiBarDirection::LeftToRight: in.w *= v; break;
        case UiBarDirection::RightToLeft: in.x += in.w * (1 - v), in.w *= v; break;
        case UiBarDirection::TopToBottom: in.h *= v; break;
        case UiBarDirection::BottomToTop: in.y += in.h * (1 - v), in.h *= v; break;
    }
    return in;
}

}  // namespace rm::game_ui
