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
    return {c, s, -s, c, 0, 0};  // (1, 0) -> (c, s): Y が下向きなので、時計回りの回転
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
    min = max = {0.5f, 0.5f};  // (新しいバージョンの値)
}

UiBox PlaceBox(const UiRect& r, const UiBox& parent) {
    Vec2 mn, mx;
    AnchorRange(r.anchor, mn, mx);
    // 親の中のアンカーの枠。大きさはそこに足す (点のアンカー: 大きさがそのまま大きさ)。
    const float ax = parent.x + parent.w * mn.x, ay = parent.y + parent.h * mn.y;
    const float aw = parent.w * (mx.x - mn.x), ah = parent.h * (mx.y - mn.y);
    const float w = std::max(0.f, aw + r.size.x), h = std::max(0.f, ah + r.size.y);
    // 中心 は、アンカーの枠の中の同じ割合の点にあり、位置 だけ動かす。
    const float px = ax + aw * r.pivot.x + r.position.x, py = ay + ah * r.pivot.y + r.position.y;
    return {px - w * r.pivot.x, py - h * r.pivot.y, w, h};
}

UiRect Reanchored(const UiRect& r, UiAnchor to, const UiBox& parent) {
    const UiBox box = PlaceBox(r, parent);
    UiRect out = r;
    out.anchor = to;
    Vec2 mn, mx;
    AnchorRange(to, mn, mx);
    // PlaceBox の逆: 新しいアンカーのアンカーの枠、それに足す大きさ、そこからの 中心。
    const float ax = parent.x + parent.w * mn.x, ay = parent.y + parent.h * mn.y;
    const float aw = parent.w * (mx.x - mn.x), ah = parent.h * (mx.y - mn.y);
    out.size = {box.w - aw, box.h - ah};
    out.position = {box.x + box.w * r.pivot.x - (ax + aw * r.pivot.x), box.y + box.h * r.pivot.y - (ay + ah * r.pivot.y)};
    return out;
}

Vec2 HandleSides(UiHandle h) {
    switch (h) {
        case UiHandle::TopLeft: return {-1, -1};
        case UiHandle::Top: return {0, -1};
        case UiHandle::TopRight: return {1, -1};
        case UiHandle::Right: return {1, 0};
        case UiHandle::BottomRight: return {1, 1};
        case UiHandle::Bottom: return {0, 1};
        case UiHandle::BottomLeft: return {-1, 1};
        case UiHandle::Left: return {-1, 0};
    }
    return {0, 0};
}

UiRect ResizeByHandle(const UiRect& r, const UiBox& parent, UiHandle h, Vec2 along, bool keepAspect, float minSize) {
    const UiBox box = PlaceBox(r, parent);
    const Vec2 sides = HandleSides(h);
    // どれだけ広く / 高くなったか (見える枠)。minSize を下回らない。
    float dw = along.x * sides.x, dh = along.y * sides.y;
    if (keepAspect && sides.x != 0 && sides.y != 0 && box.w > 1e-4f && box.h > 1e-4f) {
        // (変化が大きいほうが決める。もう一方は比率に従う)
        const float kx = (box.w + dw) / box.w, ky = (box.h + dh) / box.h;
        const float k = std::abs(kx - 1) >= std::abs(ky - 1) ? kx : ky;
        dw = box.w * k - box.w;
        dh = box.h * k - box.h;
    }
    dw = std::max(dw, minSize - box.w);
    dh = std::max(dh, minSize - box.h);
    UiRect out = r;
    out.size = {r.size.x + dw, r.size.y + dh};
    // 中心 は、動く辺と一緒に動く (変化のうちの自分の割合だけ)。親の単位に直したもの。
    const Vec2 shiftOwn{sides.x > 0 ? dw * r.pivot.x : sides.x < 0 ? -dw * (1 - r.pivot.x) : 0,
                        sides.y > 0 ? dh * r.pivot.y : sides.y < 0 ? -dh * (1 - r.pivot.y) : 0};
    const Affine turn = Rotation(r.rotation);
    out.position = {r.position.x + turn.a * shiftOwn.x + turn.c * shiftOwn.y, r.position.y + turn.b * shiftOwn.x + turn.d * shiftOwn.y};
    return out;
}

float RotateByDrag(float startDegrees, Vec2 center, Vec2 from, Vec2 to, float step) {
    const float a0 = std::atan2(from.y - center.y, from.x - center.x), a1 = std::atan2(to.y - center.y, to.x - center.x);
    float d = (a1 - a0) * 180.f / kPi;
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    float deg = startDegrees + d;
    if (step > 0) deg = std::round(deg / step) * step;
    return deg;
}

UiRect MovePivot(const UiRect& r, const UiBox& parent, Vec2 pivot) {
    const UiBox box = PlaceBox(r, parent);
    Vec2 mn, mx;
    AnchorRange(r.anchor, mn, mx);
    const float aw = parent.w * (mx.x - mn.x), ah = parent.h * (mx.y - mn.y);
    // 新しい 中心 は、古い中心に、枠の中での (回した) 動きを足したもの。位置 も、中心の割合の所の
    // アンカーの枠から測るので、それも一緒に動く。
    const Vec2 inside{(pivot.x - r.pivot.x) * box.w, (pivot.y - r.pivot.y) * box.h};
    const Affine turn = Rotation(r.rotation);
    const Vec2 turned{turn.a * inside.x + turn.c * inside.y, turn.b * inside.x + turn.d * inside.y};
    UiRect out = r;
    out.pivot = pivot;
    out.position = {r.position.x + turned.x - aw * (pivot.x - r.pivot.x), r.position.y + turned.y - ah * (pivot.y - r.pivot.y)};
    return out;
}

float SnapShift(std::span<const float> edges, std::span<const float> targets, float reach, std::optional<float>* line) {
    float best = 0;
    float bestDistance = reach;
    std::optional<float> at;
    for (const float e : edges) {
        for (const float t : targets) {
            const float d = std::abs(t - e);
            if (d <= bestDistance && (!at || d < bestDistance)) {
                bestDistance = d;
                best = t - e;
                at = t;
            }
        }
    }
    if (line) *line = at;
    return at ? best : 0.f;
}

float GridShift(float value, float step) {
    if (!(step > 0)) return 0;
    return std::round(value / step) * step - value;
}

namespace {
constexpr UiAnchor kAnchorGrid[4][4] = {
    {UiAnchor::TopLeft, UiAnchor::Top, UiAnchor::TopRight, UiAnchor::StretchTop},
    {UiAnchor::Left, UiAnchor::Center, UiAnchor::Right, UiAnchor::StretchMiddle},
    {UiAnchor::BottomLeft, UiAnchor::Bottom, UiAnchor::BottomRight, UiAnchor::StretchBottom},
    {UiAnchor::StretchLeft, UiAnchor::StretchCenter, UiAnchor::StretchRight, UiAnchor::Stretch},
};
}  // namespace

UiAnchor AnchorAt(int row, int column) { return kAnchorGrid[std::clamp(row, 0, 3)][std::clamp(column, 0, 3)]; }

void AnchorCell(UiAnchor anchor, int& row, int& column) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (kAnchorGrid[r][c] == anchor) {
                row = r;
                column = c;
                return;
            }
        }
    }
    row = column = 1;  // (新しいバージョンの値: 真ん中)
}

UiRect Rehomed(const UiRect& r, const UiBox& oldParent, const UiBox& newParent, Vec2 pivotAt, float rotation) {
    const UiBox box = PlaceBox(r, oldParent);
    Vec2 mn, mx;
    AnchorRange(r.anchor, mn, mx);
    const float ax = newParent.x + newParent.w * mn.x, ay = newParent.y + newParent.h * mn.y;
    const float aw = newParent.w * (mx.x - mn.x), ah = newParent.h * (mx.y - mn.y);
    UiRect out = r;
    out.size = {box.w - aw, box.h - ah};
    out.position = {pivotAt.x - (ax + aw * r.pivot.x), pivotAt.y - (ay + ah * r.pivot.y)};
    out.rotation = rotation;
    return out;
}

UiTree OnlyCanvas(const UiTree& tree, int canvas) {
    UiTree out;
    std::vector<int> moved(tree.nodes.size(), -1);  // 古い添字 -> 新しい添字
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        if (tree.nodes[i].canvas != canvas) continue;
        UiNode n = tree.nodes[i];
        n.parent = n.parent >= 0 ? moved[static_cast<size_t>(n.parent)] : -1;
        n.clip = n.clip >= 0 ? moved[static_cast<size_t>(n.clip)] : -1;
        moved[i] = static_cast<int>(out.nodes.size());
        // (1 つのキャンバスのノードは並んでいる: 下の範囲は同じだけずれる)
        n.end = moved[i] + (n.end - static_cast<int>(i));
        out.nodes.push_back(n);
    }
    return out;
}

std::vector<Vec2> AlignShifts(std::span<const UiBox> boxes, UiAlign how) {
    std::vector<Vec2> out(boxes.size(), Vec2{0, 0});
    if (boxes.empty()) return out;
    float x0 = boxes[0].x, y0 = boxes[0].y, x1 = boxes[0].Right(), y1 = boxes[0].Bottom();
    for (const UiBox& b : boxes) {
        x0 = std::min(x0, b.x);
        y0 = std::min(y0, b.y);
        x1 = std::max(x1, b.Right());
        y1 = std::max(y1, b.Bottom());
    }
    for (size_t i = 0; i < boxes.size(); ++i) {
        const UiBox& b = boxes[i];
        switch (how) {
            case UiAlign::Left: out[i].x = x0 - b.x; break;
            case UiAlign::CenterX: out[i].x = (x0 + x1) * 0.5f - (b.x + b.w * 0.5f); break;
            case UiAlign::Right: out[i].x = x1 - b.Right(); break;
            case UiAlign::Top: out[i].y = y0 - b.y; break;
            case UiAlign::CenterY: out[i].y = (y0 + y1) * 0.5f - (b.y + b.h * 0.5f); break;
            case UiAlign::Bottom: out[i].y = y1 - b.Bottom(); break;
        }
    }
    return out;
}

std::vector<Vec2> DistributeShifts(std::span<const UiBox> boxes, bool across) {
    std::vector<Vec2> out(boxes.size(), Vec2{0, 0});
    if (boxes.size() < 3) return out;
    auto lo = [&](size_t i) { return across ? boxes[i].x : boxes[i].y; };
    auto len = [&](size_t i) { return across ? boxes[i].w : boxes[i].h; };
    std::vector<size_t> order(boxes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return lo(a) + len(a) * 0.5f < lo(b) + len(b) * 0.5f; });
    // 最初の始まりから最後の終わりまでの広さから、それらが埋める分を引いたもの: そのあいだの
    // 等しい隙間。
    float filled = 0;
    for (const size_t i : order) filled += len(i);
    const float start = lo(order.front()), end = lo(order.back()) + len(order.back());
    const float gap = (end - start - filled) / static_cast<float>(order.size() - 1);
    float at = start;
    for (const size_t i : order) {
        (across ? out[i].x : out[i].y) = at - lo(i);
        at += len(i) + gap;
    }
    return out;
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
    // 階層: 各エンティティの子を、その順に (手前の順の最後の決め手)。
    std::unordered_map<int, std::vector<const game_production::SceneEntity*>> children;
    for (const auto& t : scene.TreeOrder()) children[t.entity->data.parent].push_back(t.entity);
    // 切ってあるが、まだ動いている (その 消えるとき が終わっていない): 入っているものとして描く。
    auto going = [&](const game_production::SceneEntity* e) {
        const UiMotion* m = PartOf<UiMotion>(*e);
        return m && m->phase == static_cast<int>(UiMotionPhase::Hiding);
    };
    auto shownFully = [&](const game_production::SceneEntity* e) {
        for (size_t steps = 0; e && steps <= scene.Entities().size(); ++steps, e = scene.Find(e->data.parent)) {
            if (!e->data.active && !going(e)) return false;
        }
        return true;
    };
    // キャンバス (ほかのキャンバスの下でないもの)。入っていて、それを持つものと一緒に - または
    // `alsoShow` のどれかを持つもの。
    std::unordered_set<int> forced;
    for (const int id : alsoShow) {
        int canvas = 0;
        size_t steps = 0;
        for (const game_production::SceneEntity* e = scene.Find(id); e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
            if (PartOf<UiCanvas>(*e)) canvas = e->data.id;  // (一番外側が勝つ)
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
        // 出る・消える動き が今動かしているウィンドウ: その真ん中を中心に。
        const UiLive& wl = ca.c->live;
        const Vec2 middle{root.box.w * 0.5f, root.box.h * 0.5f};
        root.toScreen = root.self = Translate({area.x, area.y}) * Scaling(k, k) * Translate(wl.offset) * About(middle, wl.turn, wl.scale);
        root.alpha = std::clamp(wl.alpha, 0.f, 1.f);
        root.closing = going(ca.e);
        root.canvasPart = ca.c;
        root.place = PartOf<UiRect>(*ca.e);
        root.text = PartOf<UiText>(*ca.e);
        root.image = PartOf<UiImage>(*ca.e);
        root.bar = PartOf<UiBar>(*ca.e);
        root.style = PartOf<UiStyle>(*ca.e);
        root.video = PartOf<UiVideo>(*ca.e);
        root.scroll = PartOf<UiScroll>(*ca.e);
        root.inner = root.scroll ? root.self * Translate({-root.scroll->offset.x, -root.scroll->offset.y}) : root.self;
        tree.nodes.push_back(root);
        // 深さ優先。UI の配置 を持たないものは、親の枠をそのまま子に渡す。
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
            for (auto k2 = kids.rbegin(); k2 != kids.rend(); ++k2) stack.push_back({*k2, parentNode});  // (順に取り出される)
        };
        push(root.entity, 0 + static_cast<int>(tree.nodes.size()) - 1);
        size_t guard = 0;
        while (!stack.empty() && guard++ <= scene.Entities().size() * 2 + 8) {
            const Step s = stack.back();
            stack.pop_back();
            if (!s.e->data.active && !going(s.e)) continue;  // 切ってある: その下にあるものも一緒に
            const UiNode& parent = tree.nodes[static_cast<size_t>(s.parentNode)];
            const UiRect* place = PartOf<UiRect>(*s.e);
            if (!place) {
                push(s.e->data.id, s.parentNode);  // (ノードにはしない: その子は同じ枠に入る)
                continue;
            }
            const UiLive& live = place->live;
            UiNode n;
            n.entity = s.e->data.id;
            n.canvas = root.entity;
            n.parent = s.parentNode;
            n.place = place;
            n.text = PartOf<UiText>(*s.e);
            n.image = PartOf<UiImage>(*s.e);
            n.button = PartOf<UiButton>(*s.e);
            n.bar = PartOf<UiBar>(*s.e);
            n.style = PartOf<UiStyle>(*s.e);
            n.video = PartOf<UiVideo>(*s.e);
            n.input = PartOf<UiInputField>(*s.e);
            n.toggle = PartOf<UiToggle>(*s.e);
            n.slider = PartOf<UiSlider>(*s.e);
            n.scroll = PartOf<UiScroll>(*s.e);
            n.box = PlaceBox(*place, parent.box);
            // 今動いている (出る・消える動き、道、スクリプトのトゥイーン): 枠が動き、回転と大きさはその 中心 を
            // 中心にする。
            n.box.x += live.offset.x;
            n.box.y += live.offset.y;
            // (親がスクロールなら、その中身の単位: 動かした分ずれ、枠の外は切り取られる)
            n.toScreen = parent.inner;
            n.self = parent.inner * About(PivotPoint(*place, n.box), place->rotation + live.turn, (n.button ? ButtonScale(*n.button) : 1.f) * live.scale);
            n.inner = n.scroll ? n.self * Translate({-n.scroll->offset.x, -n.scroll->offset.y}) : n.self;
            n.clip = parent.scroll ? s.parentNode : parent.clip;
            n.alpha = parent.alpha * std::clamp(live.alpha, 0.f, 1.f);
            n.closing = parent.closing || going(s.e);
            tree.nodes.push_back(n);
            push(n.entity, static_cast<int>(tree.nodes.size()) - 1);
        }
    }
    // 下のノードの範囲 (end) と、子の箱を合わせた中身 (スクロール)。ノードは深さ優先なので、後ろから親へ。
    std::vector<bool> hasChild(tree.nodes.size(), false);
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        tree.nodes[i].end = static_cast<int>(i) + 1;
        tree.nodes[i].content = tree.nodes[i].box;
    }
    for (size_t i = tree.nodes.size(); i-- > 0;) {
        const int p = tree.nodes[i].parent;
        if (p < 0 || p >= static_cast<int>(tree.nodes.size())) continue;
        UiNode& par = tree.nodes[static_cast<size_t>(p)];
        const UiNode& kid = tree.nodes[i];
        par.end = std::max(par.end, kid.end);
        if (!hasChild[static_cast<size_t>(p)]) {
            hasChild[static_cast<size_t>(p)] = true;
            par.content = kid.box;
        } else {
            const float l = std::min(par.content.x, kid.box.x), t = std::min(par.content.y, kid.box.y);
            par.content = {l, t, std::max(par.content.Right(), kid.box.Right()) - l, std::max(par.content.Bottom(), kid.box.Bottom()) - t};
        }
    }
    return tree;
}

// --- マウス、キー --------------------------------------------------------------------------

bool Hits(const UiNode& n, Vec2 p) {
    const auto inv = Invert(n.self);
    return inv && n.box.Contains(Apply(*inv, p));
}

bool InsideClips(const UiTree& tree, const UiNode& n, Vec2 p) {
    size_t guard = 0;
    for (int c = n.clip; c >= 0 && c < static_cast<int>(tree.nodes.size()) && guard++ <= tree.nodes.size(); c = tree.nodes[static_cast<size_t>(c)].clip) {
        if (!Hits(tree.nodes[static_cast<size_t>(c)], p)) return false;
    }
    return true;
}

int ScrollAt(const UiTree& tree, Vec2 p) {
    for (size_t i = tree.nodes.size(); i-- > 0;) {
        const UiNode& n = tree.nodes[i];
        if (n.scroll && !n.closing && Hits(n, p) && InsideClips(tree, n, p)) return static_cast<int>(i);
    }
    return -1;
}

UiBox BoxInScroll(const UiTree& tree, int scroll, const UiNode& item) {
    if (scroll < 0 || scroll >= static_cast<int>(tree.nodes.size())) return item.box;
    const auto inv = Invert(tree.nodes[static_cast<size_t>(scroll)].inner);
    if (!inv) return item.box;
    const UiBox& b = item.box;
    const Vec2 corners[4] = {{b.x, b.y}, {b.Right(), b.y}, {b.Right(), b.Bottom()}, {b.x, b.Bottom()}};
    float l = std::numeric_limits<float>::max(), t = l, r = -l, bt = -l;
    for (const Vec2& c : corners) {
        const Vec2 q = Apply(*inv, Apply(item.self, c));
        l = std::min(l, q.x), t = std::min(t, q.y), r = std::max(r, q.x), bt = std::max(bt, q.y);
    }
    return {l, t, r - l, bt - t};
}

int TopAt(const UiTree& tree, Vec2 p) {
    for (auto it = tree.nodes.rbegin(); it != tree.nodes.rend(); ++it) {
        if (it->parent < 0) continue;  // キャンバス自身は何も受け取らない (ウィンドウの一部が空でもよい)
        if (it->closing) continue;     // 消えていく途中 (消えるとき): 見えるが、触れない
        if (!(it->image || it->bar || it->button || it->video || it->scroll || (it->style && it->style->background.enabled))) continue;
        if (Hits(*it, p) && InsideClips(tree, *it, p)) return it->entity;
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
    if (state.pressed && !tree.Find(state.pressed)) state.pressed = 0;  // なくなった (切った、壊した)
    if (wentUp || !down) {
        if (wentUp && state.pressed && state.pressed == r.hovered) r.clicked = state.pressed;
        state.pressed = 0;
    }
    if (state.pressed && state.pressed == r.hovered) r.held = state.pressed;
    return r;
}

int NavigationCanvas(const UiTree& tree) {
    for (auto it = tree.nodes.rbegin(); it != tree.nodes.rend(); ++it) {
        if (!Pressable(it->button) || it->closing) continue;  // (去っていくウィンドウはキーを受け取らない)
        const UiNode* c = tree.Find(it->canvas);
        if (!c || !c->canvasPart || !c->canvasPart->navigation) continue;
        // ボタンを持つ一番手前のキャンバス: そのキャンバス自身のもの。ノードはキャンバスごとに奥から手前へ
        // 並ぶため。
        return it->canvas;
    }
    return 0;
}

int FirstFocus(const UiTree& tree, int canvas, int preferred) {
    int best = 0;
    Vec2 bestAt{};
    for (const UiNode& n : tree.nodes) {
        if (n.canvas != canvas || !Pressable(n.button) || n.closing) continue;
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
        if (n.canvas != canvas || n.entity == current || !Pressable(n.button) || n.closing) continue;
        const Vec2 c = Center(n);
        const float ax = c.x - o.x, ay = c.y - o.y;
        const float along = ax * static_cast<float>(dx) + ay * static_cast<float>(dy);
        if (along <= 0.5f) continue;  // その向きではない
        const float aside = std::abs(ax * static_cast<float>(dy) - ay * static_cast<float>(dx));
        const float score = along + aside * 2.f;
        if (score < bestScore) {
            bestScore = score;
            best = n.entity;
        }
    }
    return best;
}

int NextButton(const UiTree& tree, int canvas, int current, int dx, int dy) {
    // まずその向きの自分のリンク (上 / 下 / 左 / 右で移る先)。それが、押せるそのウィンドウのボタン
    // のとき。そうでなければ、画面でその向きに一番近いもの。
    if (const UiNode* from = tree.Find(current); from && from->button && (dx != 0) != (dy != 0)) {
        const UiButton& b = *from->button;
        const EntityRef& link = dy < 0 ? b.up : dy > 0 ? b.down : dx < 0 ? b.left : b.right;
        const UiNode* to = link.id ? tree.Find(static_cast<int>(link.id)) : nullptr;
        if (to && to->canvas == canvas && to->button && to->button->interactable) return to->entity;
    }
    return MoveFocus(tree, canvas, current, dx, dy);
}

// --- ボタンの見え方 ----------------------------------------------------------------------------

namespace {

// 見た目に使う状態: 押せなければ 使えない、選ばれている (キーで選んだ) ものは 乗った と同じ。
UiButtonState LooksAs(const UiButton& b) {
    if (!b.interactable) return UiButtonState::Disabled;
    const auto s = static_cast<UiButtonState>(b.state);
    if (s == UiButtonState::Normal && b.focused) return UiButtonState::Hover;
    return s;
}

}  // namespace

UiButtonLook ButtonTarget(const UiButton& b) {
    const UiButtonState s = LooksAs(b);
    Color color = b.normal;
    float outline = 0;
    Color outlineColor{1, 1, 1, 1};
    UiButtonLook l;
    switch (s) {
        case UiButtonState::Hover:
            color = b.hover, l.scale = b.hoverScale, outline = b.hoverOutline, outlineColor = b.hoverOutlineColor;
            break;
        case UiButtonState::Pressed:
            color = b.pressed, l.scale = b.pressedScale, outline = b.pressedOutline, outlineColor = b.pressedOutlineColor;
            break;
        case UiButtonState::Disabled: color = b.disabled; break;
        default: break;
    }
    if (b.colorMode == UiButtonColorMode::Replace && s != UiButtonState::Normal) {
        // その状態の色そのもの (その r g b)。透明度はやはり掛ける。
        l.tint = b.normal.Alpha(color.a);
        l.replace = color.Alpha(1);
        l.replaceAmount = 1;
    } else {
        l.tint = color;
    }
    if (outline > 0) {
        l.outline = outline;
        l.outlineColor = outlineColor;
        l.outlineAmount = 1;
    }
    return l;
}

float EaseAmount(float dt, float seconds) {
    if (!(seconds > 1e-4f)) return 1;
    if (!(dt > 0)) return 0;
    return 1 - std::exp(-dt / seconds);
}

UiButtonLook EaseLook(const UiButtonLook& now, const UiButtonLook& target, float amount) {
    UiButtonLook goal = target;
    // 輪郭の無い状態: 最後のものが消えていく (細くならず、白に寄らない)。まだ見えていないもの
    // (その量が 0): 新しいものの太さと色をすぐに - 量だけが増える。
    if (goal.outlineAmount <= 0) goal.outline = now.outline, goal.outlineColor = now.outlineColor;
    if (goal.replaceAmount <= 0) goal.replace = now.replace;
    UiButtonLook from = now;
    if (from.outlineAmount <= 0) from.outline = goal.outline, from.outlineColor = goal.outlineColor;
    if (from.replaceAmount <= 0) from.replace = goal.replace;
    const float a = std::clamp(amount, 0.f, 1.f);
    auto go = [a](float c, float g) {
        const float v = c + (g - c) * a;
        return std::abs(g - v) < 1e-4f ? g : v;
    };
    auto goColor = [&](Color c, Color g) { return Color{go(c.r, g.r), go(c.g, g.g), go(c.b, g.b), go(c.a, g.a)}; };
    UiButtonLook out;
    out.tint = goColor(from.tint, goal.tint);
    out.replace = goColor(from.replace, goal.replace);
    out.replaceAmount = go(from.replaceAmount, goal.replaceAmount);
    out.scale = go(from.scale, goal.scale);
    out.outline = go(from.outline, goal.outline);
    out.outlineColor = goColor(from.outlineColor, goal.outlineColor);
    out.outlineAmount = go(from.outlineAmount, goal.outlineAmount);
    return out;
}

UiButtonLook ButtonNow(const UiButton& b) { return b.ready ? b.now : ButtonTarget(b); }

float ButtonScale(const UiButton& b) { return ButtonNow(b).scale; }

const AssetRef& ButtonImage(const UiButton& b, const AssetRef& own) {
    switch (LooksAs(b)) {
        case UiButtonState::Pressed:
            if (!b.pressedImage.Empty()) return b.pressedImage;
            return b.hoverImage.Empty() ? own : b.hoverImage;
        case UiButtonState::Hover: return b.hoverImage.Empty() ? own : b.hoverImage;
        case UiButtonState::Disabled: return b.disabledImage.Empty() ? own : b.disabledImage;
        default: return own;
    }
}

UiPaint PaintOf(const UiButtonLook& look) { return {look.tint, look.replace, look.replaceAmount}; }

Color Paint(Color c, const UiPaint& p) {
    const Color m = Multiply(c, p.tint);
    if (p.amount <= 0) return m;
    return Mix(m, p.replace.Alpha(m.a), std::min(p.amount, 1.f));
}

bool IsPlain(const UiPaint& p) { return p.tint == Color{1, 1, 1, 1} && p.amount <= 0; }

UiStroke ButtonOutline(const UiButtonLook& look, UiStroke own) {
    const float a = std::clamp(look.outlineAmount, 0.f, 1.f);
    if (own.width <= 0) return {look.outline, look.outlineColor.Alpha(look.outlineColor.a * a)};
    return {own.width + (look.outline - own.width) * a, Mix(own.color, look.outlineColor, a)};
}

Color Multiply(Color a, Color b) { return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a}; }

UiBox CropRect(Vec2 imagePx, Vec2 offset, Vec2 size) {
    const float w = std::max(imagePx.x, 0.f), h = std::max(imagePx.y, 0.f);
    const float x = std::clamp(std::isfinite(offset.x) ? offset.x : 0.f, 0.f, w), y = std::clamp(std::isfinite(offset.y) ? offset.y : 0.f, 0.f, h);
    const float cw = size.x > 0 ? std::min(size.x, w - x) : w - x, ch = size.y > 0 ? std::min(size.y, h - y) : h - y;
    return {x, y, std::max(cw, 0.f), std::max(ch, 0.f)};
}

Color RainbowColor(const UiRainbow& r, double seconds) {
    double hue = std::fmod(static_cast<double>(r.phase) + seconds * r.speed, 1.0);
    if (!std::isfinite(hue)) hue = 0;
    if (hue < 0) hue += 1;
    const float s = std::clamp(r.saturation, 0.f, 1.f), v = std::clamp(r.brightness, 0.f, 1.f);
    // HSV から RGB へ: 色相環の 6 つの区間。
    const float h6 = static_cast<float>(hue) * 6, f = h6 - std::floor(h6);
    const float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (static_cast<int>(h6) % 6) {
        case 0: return {v, t, p, 1};
        case 1: return {q, v, p, 1};
        case 2: return {p, v, t, 1};
        case 3: return {p, q, v, 1};
        case 4: return {t, p, v, 1};
        default: return {v, p, q, 1};
    }
}

UiTextRange TextRange(std::wstring_view text, int start, int length) {
    auto unitAt = [&](int chars) {
        size_t i = 0;
        for (int c = 0; c < chars && i < text.size(); ++c) {
            const bool pair = text[i] >= 0xD800 && text[i] <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF;
            i += pair ? 2 : 1;
        }
        return i;
    };
    const size_t a = unitAt(std::max(start, 0));
    const size_t b = unitAt(std::max(start, 0) + std::max(length, 0));
    return {static_cast<uint32_t>(a), static_cast<uint32_t>(b - a)};
}

bool BevelShown(const UiStyle& s) { return s.bevel.enabled && s.bevel.width > 0; }

bool BevelInset(const UiStyle& s, const UiButton* button) {
    const bool pressed = button && s.bevel.pressInset && button->interactable && button->state == static_cast<int>(UiButtonState::Pressed);
    return s.bevel.inset != pressed;
}

float Rounding(const UiStyle& s, const UiBox& box) {
    const float r = std::isfinite(s.rounding) ? std::max(s.rounding, 0.f) : 0.f;
    return std::min({r, std::max(box.w, 0.f) * 0.5f, std::max(box.h, 0.f) * 0.5f});
}

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
