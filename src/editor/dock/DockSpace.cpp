#include "editor/dock/DockSpace.h"

#include "base/Utf.h"
#include "editor/widgets/Theme.h"
#include "platform/Shell.h"

#include <algorithm>
#include <cmath>

namespace rm::editor::dock {

using ui::Rect;

namespace {

constexpr float kGap = 4.f;            // splitter thickness
constexpr float kStripH = 30.f;        // tab strip height
constexpr float kMinPanel = 80.f;      // a splitter never makes a side smaller than this
constexpr float kCloseW = 22.f;        // room for the x of a tab
constexpr float kGuide = 32.f;         // drop guide size
constexpr float kGuideStep = 38.f;     // distance between the guides of a cross
constexpr float kDragThreshold = 6.f;  // a tab moves only after the mouse moved this far

bool EscapePressed(const ui::Input& in) {
    return std::any_of(in.keys.begin(), in.keys.end(), [](const ui::KeyEvent& k) { return k.key == ui::Key::Escape; });
}

ui::TextStyle TabText(ui::Color color) {
    ui::TextStyle ts;
    ts.size = 12;
    ts.bold = true;
    ts.color = color;
    return ts;
}

Rect Centered(float cx, float cy, float size) { return {cx - size * 0.5f, cy - size * 0.5f, size, size}; }

void DrawGuide(ui::Painter& p, const Rect& box, Zone zone, bool hot) {
    p.FillRoundRect({box.x + 1, box.y + 2, box.w, box.h}, 6, theme::Shadow);
    p.FillRoundRect(box, 6, hot ? theme::Accent : theme::PanelHeader);
    p.StrokeRoundRect(box, 6, hot ? theme::AccentHover : theme::BorderHover);
    const Rect in = box.Inset(8);
    const ui::Color ink = theme::Text.Alpha(hot ? 1.f : 0.75f);
    p.StrokeRect(in, ink);
    Rect part = in;
    switch (zone) {
        case Zone::Left: part.w *= 0.45f; break;
        case Zone::Right: part = {in.Right() - in.w * 0.45f, in.y, in.w * 0.45f, in.h}; break;
        case Zone::Top: part.h *= 0.45f; break;
        case Zone::Bottom: part = {in.x, in.Bottom() - in.h * 0.45f, in.w, in.h * 0.45f}; break;
        default: part = in.Inset(3); break;
    }
    p.FillRect(part, ink.Alpha(hot ? 0.9f : 0.55f));
}

}  // namespace

// ---------------------------------------------------------------------------
// Panels and layout
// ---------------------------------------------------------------------------

const PanelType* DockSpace::FindPanel(std::string_view id) const {
    for (const auto& t : types_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

std::vector<std::string> DockSpace::PanelIds() const {
    std::vector<std::string> ids;
    for (const auto& t : types_) ids.push_back(t.id);
    return ids;
}

void DockSpace::SetLayout(Layout layout) {
    layout_ = std::move(layout);
    drag_ = {};
    for (auto& [site, m] : menus_) m.popup.open = false;
    Changed();
}

void DockSpace::Changed() {
    if (onStructureChanged) onStructureChanged();
    if (onRedrawAll) onRedrawAll();
}

const Node* DockSpace::RootOf(uint32_t site) const {
    if (site == 0) return layout_.main.get();
    for (const auto& f : layout_.floating) {
        if (f.id == site) return f.root.get();
    }
    return nullptr;
}

std::wstring DockSpace::FloatingTitle(uint32_t floatingId) const {
    const Node* root = RootOf(floatingId);
    std::wstring title;
    if (!root || floatingId == 0) return title;
    std::vector<const Node*> stack{root};
    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();
        if (!n->IsLeaf()) {
            stack.push_back(n->b.get());
            stack.push_back(n->a.get());
            continue;
        }
        for (const auto& id : n->tabs) {
            if (!title.empty()) title += L" / ";
            const PanelType* t = FindPanel(id);
            title += t ? t->title : Utf8ToWide(id);
        }
    }
    return title;
}

void DockSpace::ForgetSite(uint32_t site) {
    sites_.erase(site);
    menus_.erase(site);
    if (drag_.kind != DragKind::None && drag_.fromSite == site) drag_ = {};
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void DockSpace::Draw(ui::Context& c, const Rect& area, uint32_t siteId, HWND hwnd, float scale) {
    Site& site = sites_[siteId];
    site.hwnd = hwnd;
    site.scale = scale;
    site.area = area;
    site.leaves.clear();
    const Node* root = RootOf(siteId);
    if (!root) return;  // a floating window whose panels were just moved away (closed after this frame)
    site.rootId = root->id;
    site.rootIsLeaf = root->IsLeaf();

    // While a tab menu is open or something is being dragged, the panels only show.
    const bool inputWas = c.InputEnabled();
    if (menus_[siteId].popup.open || IsDragging()) c.SetInputEnabled(false);

    std::vector<Placed> placed;
    Arrange(*root, area, kGap, placed);
    for (const auto& pl : placed) {
        if (!pl.node->IsLeaf()) continue;
        LeafGeo geo;
        DrawLeaf(c, *pl.node, pl.rect, siteId, geo);
        site.leaves.push_back(std::move(geo));
    }
    for (const auto& pl : placed) {
        if (!pl.node->IsLeaf()) DrawSplitter(c, *pl.node, pl.rect);
    }
    c.SetInputEnabled(inputWas);

    // The window where a tab drag started has the mouse capture, so it follows the whole drag.
    if (drag_.fromSite == siteId && (drag_.kind == DragKind::Pending || drag_.kind == DragKind::Tab)) {
        const ui::Input& in = c.In();
        if (drag_.kind == DragKind::Pending) {
            if (!in.down[0]) {
                drag_ = {};
            } else if (std::hypot(in.mouseX - drag_.startX, in.mouseY - drag_.startY) > kDragThreshold) {
                drag_.kind = DragKind::Tab;
                for (auto& [s, m] : menus_) m.popup.open = false;
            }
        }
        if (drag_.kind == DragKind::Tab) {
            if (EscapePressed(in)) {
                CancelDrag();
            } else if (in.released[0] || !in.down[0]) {
                FinishTabDrag();
            } else {
                UpdateHover(nullptr);
            }
        }
    }

    // Edits collected during the frame are applied now, after every node was drawn.
    auto edits = std::move(post_);
    post_.clear();
    for (auto& fn : edits) fn();
}

void DockSpace::DrawLeaf(ui::Context& c, const Node& leaf, const Rect& r, uint32_t siteId, LeafGeo& geo) {
    auto& p = c.P();
    geo.id = leaf.id;
    geo.rect = r;
    p.FillRoundRect(r, 6, theme::Panel);
    Rect body = r;
    const Rect strip = ui::TakeTop(body, kStripH);
    geo.strip = strip;
    p.FillRect({r.x, strip.Bottom() - 1, r.w, 1}, theme::Border);

    c.PushClip(strip);
    float x = strip.x + 4;
    for (size_t i = 0; i < leaf.tabs.size(); ++i) {
        const std::string& id = leaf.tabs[i];
        const PanelType* type = FindPanel(id);
        const std::wstring title = type ? type->title : Utf8ToWide(id);
        const bool sel = static_cast<int>(i) == leaf.active;
        const float tw = p.TextWidth(title, 12, true) + 20 + kCloseW;
        const Rect tab{x, strip.y + 3, tw, strip.h - 3};
        geo.tabs.push_back(tab);

        c.PushId(id);
        const auto b = c.ButtonBehavior(c.MakeId("tab"), tab);
        const Rect close{tab.Right() - kCloseW, tab.y + (tab.h - 18) * 0.5f, 18, 18};
        const bool showClose = sel || b.hovered;
        const auto cb = showClose ? c.ButtonBehavior(c.MakeId("close"), close) : ui::Context::ButtonResult{};
        if (b.hovered && c.MousePressed() && !cb.hovered) {
            Post([this, id] { layout_.Activate(id); });
            drag_ = {};
            drag_.kind = DragKind::Pending;
            drag_.panel = id;
            drag_.fromSite = siteId;
            drag_.startX = c.In().mouseX;
            drag_.startY = c.In().mouseY;
            drag_.fromLeaf = r;
        }
        if ((b.hovered && c.MousePressed(ui::Mouse::Middle)) || cb.clicked) {
            Post([this, id] {
                if (layout_.Close(id)) Changed();
            });
        }
        if (b.rightClicked) {
            TabMenu& m = menus_[siteId];
            m.panel = id;
            m.popup.OpenAt(c.In().mouseX, c.In().mouseY);
            Post([this, id] { layout_.Activate(id); });
        }
        c.PopId();

        if (sel) {
            p.FillRoundRect(tab, 4, theme::PanelHeader);
            p.FillRect({tab.x + 8, tab.Bottom() - 2, tab.w - 16, 2}, theme::Accent);
        } else if (b.hovered) {
            p.FillRoundRect(tab, 4, theme::Pressed);
        }
        p.Text({tab.x + 10, tab.y, tw - 10 - kCloseW, tab.h}, title, TabText(sel ? theme::Text : theme::TextDim));
        if (showClose) {
            if (cb.hovered) p.FillRoundRect(close, 4, theme::Hover);
            ui::TextStyle xs = TabText(cb.hovered ? theme::Text : theme::TextFaint);
            xs.h = ui::HAlign::Center;
            xs.bold = false;
            p.Text(close, L"×", xs);
        }
        x += tw + 2;
    }
    c.PopClip();

    if (leaf.tabs.empty()) {
        ui::TextStyle hint = TabText(theme::TextFaint);
        hint.bold = false;
        hint.wrap = true;
        hint.h = ui::HAlign::Center;
        p.Text(body.Inset(16), L"パネルがありません。\n「ウィンドウ」メニューから開けます。", hint);
        return;
    }
    const PanelType* type = FindPanel(leaf.tabs[static_cast<size_t>(leaf.active)]);
    if (!type || !type->draw) return;
    c.PushId(type->id);
    c.PushClip(body);
    type->draw(c, body);
    c.PopClip();
    c.PopId();
}

void DockSpace::DrawSplitter(ui::Context& c, const Node& split, const Rect& r) {
    const bool horizontal = split.axis == Axis::Horizontal;
    const Rect bar = SplitterRect(split, r, kGap);
    const Rect hit = horizontal ? bar.Inset(-2, 0, -2, 0) : bar.Inset(0, -2, 0, -2);
    c.PushId(static_cast<uint64_t>(split.id));
    const ui::Id id = c.MakeId("splitter");
    c.PopId();
    const auto b = c.ButtonBehavior(id, hit);
    const bool dragging = c.Active() == id;
    if (b.hovered || dragging) {
        c.SetCursor(horizontal ? ui::Cursor::SizeWE : ui::Cursor::SizeNS);
        const Rect line = horizontal ? Rect{bar.x + bar.w * 0.5f - 1, bar.y, 2, bar.h} : Rect{bar.x, bar.y + bar.h * 0.5f - 1, bar.w, 2};
        c.P().FillRect(line, dragging ? theme::Accent : theme::BorderHover);
    }
    if (dragging && c.In().down[0]) {
        const float mouse = horizontal ? c.In().mouseX : c.In().mouseY;
        const float ratio = RatioAt(split, r, kGap, mouse, kMinPanel);
        if (ratio != split.ratio) {
            if (Node* n = layout_.FindNode(split.id)) n->ratio = ratio;  // not a structure change
            c.RequestRedraw();
        }
    }
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

std::vector<DockSpace::Guide> DockSpace::GuidesAt(const Site& site, float x, float y) const {
    std::vector<Guide> out;
    for (const auto& leaf : site.leaves) {
        if (!leaf.rect.Contains(x, y)) continue;
        const float cx = leaf.rect.x + leaf.rect.w * 0.5f;
        const float cy = leaf.rect.y + leaf.rect.h * 0.5f;
        out.push_back({Centered(cx, cy, kGuide), leaf.id, Zone::Center, true, leaf.rect});
        if (leaf.rect.w >= kGuideStep * 3 && leaf.rect.h >= kGuideStep * 3) {
            out.push_back({Centered(cx - kGuideStep, cy, kGuide), leaf.id, Zone::Left, true, leaf.rect});
            out.push_back({Centered(cx + kGuideStep, cy, kGuide), leaf.id, Zone::Right, true, leaf.rect});
            out.push_back({Centered(cx, cy - kGuideStep, kGuide), leaf.id, Zone::Top, true, leaf.rect});
            out.push_back({Centered(cx, cy + kGuideStep, kGuide), leaf.id, Zone::Bottom, true, leaf.rect});
        }
        break;
    }
    // The edges of the whole window: put the panel along a side of everything.
    if (!site.rootIsLeaf) {
        const Rect& a = site.area;
        const float m = kGuide * 0.5f + 8;
        out.push_back({Centered(a.x + m, a.y + a.h * 0.5f, kGuide), site.rootId, Zone::Left, false, a});
        out.push_back({Centered(a.Right() - m, a.y + a.h * 0.5f, kGuide), site.rootId, Zone::Right, false, a});
        out.push_back({Centered(a.x + a.w * 0.5f, a.y + m, kGuide), site.rootId, Zone::Top, false, a});
        out.push_back({Centered(a.x + a.w * 0.5f, a.Bottom() - m, kGuide), site.rootId, Zone::Bottom, false, a});
    }
    return out;
}

void DockSpace::UpdateHover(HWND exclude) {
    POINT pt{};
    GetCursorPos(&pt);
    const HWND under = platform::TopLevelWindowAt(pt, exclude);
    uint32_t hover = ~0u;
    for (const auto& [id, s] : sites_) {
        if (under && s.hwnd == under) hover = id;
    }
    Target t;
    float hx = 0, hy = 0;
    uint32_t leafUnder = 0;
    if (hover != ~0u) {
        const Site& s = sites_.at(hover);
        POINT cp = pt;
        ScreenToClient(s.hwnd, &cp);
        hx = static_cast<float>(cp.x) / s.scale;
        hy = static_cast<float>(cp.y) / s.scale;
        for (const auto& leaf : s.leaves) {
            if (leaf.rect.Contains(hx, hy)) leafUnder = leaf.id;
        }
        for (const Guide& g : GuidesAt(s, hx, hy)) {
            if (!g.rect.Contains(hx, hy)) continue;
            t.valid = true;
            t.site = hover;
            t.node = g.node;
            t.zone = g.zone;
            t.preview = g.zone == Zone::Center ? g.target : ZonePreview(g.target, g.zone, g.leaf);
            break;
        }
        if (!t.valid) {
            for (const auto& leaf : s.leaves) {
                if (!leaf.strip.Contains(hx, hy)) continue;
                int index = 0;
                for (const Rect& tab : leaf.tabs) {
                    if (tab.x + tab.w * 0.5f < hx) ++index;
                }
                t.valid = true;
                t.site = hover;
                t.node = leaf.id;
                t.zone = Zone::Center;
                t.tabIndex = index;
                t.preview = leaf.rect;
                break;
            }
        }
    }
    // Redraw only when what the guides show changes. Redrawing on every call would make the
    // dragging window invalidate itself from inside its own frame forever, and its endless
    // WM_PAINTs starve the other windows (they would never show their guides).
    const Target& o = drag_.target;
    const bool changed = hover != drag_.hoverSite || leafUnder != drag_.hoverLeaf || t.valid != o.valid ||
                         t.site != o.site || t.node != o.node || t.zone != o.zone || t.tabIndex != o.tabIndex;
    drag_.hoverSite = hover;
    drag_.hoverX = hx;
    drag_.hoverY = hy;
    drag_.hoverLeaf = leafUnder;
    drag_.target = t;
    if (changed && onRedrawAll) onRedrawAll();
}

ScreenRect DockSpace::FloatRectNear(const Site& site, const Rect& leafRect, POINT at) const {
    const float s = site.scale;
    ScreenRect r;
    r.w = static_cast<int>(std::clamp(leafRect.w, 320.f, 1000.f) * s);
    r.h = static_cast<int>((std::clamp(leafRect.h, 240.f, 800.f) + 32.f) * s);
    r.x = at.x - static_cast<int>(60 * s);
    r.y = at.y - static_cast<int>(14 * s);
    return r;
}

void DockSpace::FinishTabDrag() {
    const Drag d = drag_;
    drag_ = {};
    if (d.target.valid) {
        Post([this, d] {
            if (layout_.Dock(d.panel, d.target.node, d.target.zone, d.target.tabIndex)) Changed();
        });
    } else {
        // Released away from every guide and tab strip: the panel goes out into its own window.
        POINT pt{};
        GetCursorPos(&pt);
        const auto it = sites_.find(d.fromSite);
        const ScreenRect r = it != sites_.end() ? FloatRectNear(it->second, d.fromLeaf, pt) : ScreenRect{pt.x, pt.y, 480, 360};
        Post([this, d, r] {
            if (layout_.Float(d.panel, r)) Changed();
        });
    }
    if (onRedrawAll) onRedrawAll();
}

void DockSpace::CancelDrag() {
    drag_ = {};
    if (onRedrawAll) onRedrawAll();
}

void DockSpace::WindowMoving(uint32_t floatingId, HWND hwnd) {
    if (drag_.kind != DragKind::Window || drag_.floating != floatingId) {
        drag_ = {};
        drag_.kind = DragKind::Window;
        drag_.floating = floatingId;
        drag_.fromSite = floatingId;
    }
    UpdateHover(hwnd);  // the moving window itself is under the cursor: look below it
}

void DockSpace::WindowMoveEnded(uint32_t floatingId) {
    if (drag_.kind != DragKind::Window || drag_.floating != floatingId) return;
    const Target t = drag_.target;
    drag_ = {};
    if (t.valid && layout_.DockFloating(floatingId, t.node, t.zone)) {
        Changed();
    } else if (onRedrawAll) {
        onRedrawAll();
    }
}

// ---------------------------------------------------------------------------
// Overlay and commands
// ---------------------------------------------------------------------------

void DockSpace::DrawOverlay(ui::Context& c, uint32_t siteId) {
    auto& p = c.P();
    const auto siteIt = sites_.find(siteId);
    if (IsDragging() && drag_.hoverSite == siteId && siteIt != sites_.end()) {
        if (drag_.target.valid && drag_.target.site == siteId) {
            const Rect& pr = drag_.target.preview;
            p.FillRoundRect(pr, 6, theme::Accent.Alpha(0.18f));
            p.StrokeRoundRect(pr.Inset(1), 6, theme::Accent.Alpha(0.9f), 2);
        }
        for (const Guide& g : GuidesAt(siteIt->second, drag_.hoverX, drag_.hoverY)) {
            DrawGuide(p, g.rect, g.zone, g.rect.Contains(drag_.hoverX, drag_.hoverY));
        }
    }
    // The tab being dragged follows the mouse inside the window it came from.
    if (drag_.kind == DragKind::Tab && drag_.fromSite == siteId && c.Viewport().Contains(c.In().mouseX, c.In().mouseY)) {
        const PanelType* type = FindPanel(drag_.panel);
        const std::wstring title = type ? type->title : Utf8ToWide(drag_.panel);
        const float tw = p.TextWidth(title, 12, true) + 24;
        const Rect ghost{c.In().mouseX + 12, c.In().mouseY + 10, tw, 26};
        p.FillRoundRect({ghost.x + 1, ghost.y + 2, ghost.w, ghost.h}, 5, theme::Shadow);
        p.FillRoundRect(ghost, 5, theme::PanelHeader);
        p.StrokeRoundRect(ghost, 5, theme::Accent);
        ui::TextStyle ts = TabText(theme::Text);
        ts.h = ui::HAlign::Center;
        p.Text(ghost, title, ts);
    }

    const auto menuIt = menus_.find(siteId);
    if (menuIt == menus_.end() || !menuIt->second.popup.open) return;
    TabMenu& m = menuIt->second;
    uint32_t fid = 0;
    const Node* leaf = layout_.LeafOf(m.panel, &fid);
    if (!leaf) {
        m.popup.open = false;
        return;
    }
    const Node* floatRoot = RootOf(fid);
    const bool aloneInWindow = fid != 0 && floatRoot == leaf && leaf->tabs.size() == 1;
    const std::vector<w::MenuItem> items = {
        {L"閉じる", L"中クリック"},
        {L"別ウィンドウに出す", L"", !aloneInWindow},
        {L"エディターのウィンドウに戻す", L"", fid != 0},
    };
    const std::string panel = m.panel;
    const int pick = w::MenuPopup(c, m.popup, items, 250);
    if (pick == 0) {
        if (layout_.Close(panel)) Changed();
    } else if (pick == 1) {
        FloatPanel(panel);
    } else if (pick == 2) {
        const uint32_t target = LargestMainLeaf();
        if (layout_.Dock(panel, target, Zone::Center)) Changed();
    }
}

uint32_t DockSpace::LargestMainLeaf() const {
    uint32_t target = 0;
    float best = -1;
    if (const auto main = sites_.find(0); main != sites_.end()) {
        for (const auto& g : main->second.leaves) {
            if (g.rect.w * g.rect.h > best) {
                best = g.rect.w * g.rect.h;
                target = g.id;
            }
        }
    }
    const Node* n = layout_.FindNode(target);
    if (n && n->IsLeaf()) return target;
    // not drawn yet: the first leaf of the editor window
    const Node* first = layout_.main.get();
    while (!first->IsLeaf()) first = first->a.get();
    return first->id;
}

void DockSpace::TogglePanel(std::string_view id) {
    if (layout_.Contains(id)) {
        layout_.Close(id);
    } else {
        layout_.Open(id, LargestMainLeaf());  // as a tab of the biggest panel of the editor window
    }
    Changed();
}

void DockSpace::CloseFloating(uint32_t floatingId) {
    if (layout_.CloseFloating(floatingId)) Changed();
}

void DockSpace::FloatPanel(std::string_view id) {
    uint32_t fid = 0;
    const Node* leaf = layout_.LeafOf(id, &fid);
    if (!leaf) return;
    // A little below and to the right of where the panel is now.
    ScreenRect r{200, 200, 480, 360};
    if (const auto it = sites_.find(fid); it != sites_.end()) {
        const Site& s = it->second;
        for (const auto& g : s.leaves) {
            if (g.id != leaf->id) continue;
            POINT at{static_cast<LONG>((g.rect.x + 100) * s.scale), static_cast<LONG>((g.rect.y + 50) * s.scale)};
            ClientToScreen(s.hwnd, &at);
            r = FloatRectNear(s, g.rect, at);
        }
    }
    if (layout_.Float(id, r)) Changed();
}

}  // namespace rm::editor::dock
