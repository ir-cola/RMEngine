#include "editor/lang/Lang.h"
#include "editor/dock/DockSpace.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"

#include <algorithm>
#include <cmath>

namespace rm::editor::dock {

using ui::Rect;

namespace {

constexpr float kGap = 1.f;            // between two panels: one dark seam (the panels' outer edge line)
constexpr float kSplitterGrab = 3.f;   // the seam can be grabbed this far into the panels on each side
constexpr float kPanelRadius = 6.f;    // corners on the outside of the dock area (touching ones are square)
constexpr float kStripH = 30.f;        // tab strip height
constexpr float kMinPanel = 80.f;      // a splitter never makes a side smaller than this
constexpr float kCloseW = 22.f;        // room for the x of a tab
constexpr float kStripPad = 4.f;       // before the first tab and after the last one / the buttons
constexpr float kTabSpacing = 2.f;     // between two tabs
constexpr float kStripButton = 22.f;   // < > v when the tabs do not fit
constexpr float kStripButtons = kStripButton * 3 + 4;
constexpr float kWheelStep = 60.f;     // one notch of the wheel over a tab strip
constexpr float kDragEdge = 28.f;      // a dragged tab this close to an end of a row moves the row
constexpr double kDragScrollSpeed = 500.0;  // DIPs per second
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

void DrawGuide(ui::Context& c, const Rect& box, Zone zone, bool hot) {
    auto& p = c.P();
    w::DrawSurface(c, box, hot ? w::Surface::Accent : w::Surface::Popup, 6.f);
    const Rect in = box.Inset(8);
    const ui::Color ink = (hot ? theme::OnAccent() : theme::Text).Alpha(hot ? 1.f : 0.75f);
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
    for (auto& [site, m] : tabLists_) m.popup.open = false;
    strips_.clear();  // node ids start again in the new layout
    Changed();
}

bool DockSpace::MenuOpen(uint32_t site) const {
    const auto m = menus_.find(site);
    const auto l = tabLists_.find(site);
    return (m != menus_.end() && m->second.popup.open) || (l != tabLists_.end() && l->second.popup.open);
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
            title += t ? Tr(t->title) : Utf8ToWide(id);
        }
    }
    return title;
}

void DockSpace::ForgetSite(uint32_t site) {
    sites_.erase(site);
    menus_.erase(site);
    tabLists_.erase(site);
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
    if (MenuOpen(siteId) || IsDragging()) c.SetInputEnabled(false);

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
    // Rounded only where the panel touches nothing: both sides of the corner on the dock area's edge.
    const Rect& area = sites_[siteId].area;
    constexpr float eps = 0.5f;
    const bool left = r.x <= area.x + eps, top = r.y <= area.y + eps;
    const bool right = r.Right() >= area.Right() - eps, bottom = r.Bottom() >= area.Bottom() - eps;
    auto round = [](bool a, bool b) { return a && b ? kPanelRadius : 0.f; };
    w::DrawPanelFrame(c, r, {round(top, left), round(top, right), round(bottom, right), round(bottom, left)});
    Rect body = r;
    const Rect strip = ui::TakeTop(body, kStripH);
    geo.strip = strip;
    w::Separator(c, r.x + 1, strip.Bottom() - 1, r.w - 2);

    DrawTabStrip(c, leaf, strip, siteId, geo);

    if (leaf.tabs.empty()) {
        ui::TextStyle hint = TabText(theme::TextFaint);
        hint.bold = false;
        hint.wrap = true;
        hint.h = ui::HAlign::Center;
        p.Text(body.Inset(16), Tr(L"パネルがありません。\n「ウィンドウ」メニューから開けます。"), hint);
        return;
    }
    const PanelType* type = FindPanel(leaf.tabs[static_cast<size_t>(leaf.active)]);
    if (!type || !type->draw) return;
    // A panel leaves the context as it found it: the input switched on or off, the ids and the clips.
    // One that does not (an early return between a push and its pop) is put right here - else every
    // panel drawn after it in this window would take no mouse, or have other ids - and told once.
    const bool inputWas = c.InputEnabled();
    const size_t ids = c.IdDepth(), clips = c.ClipDepth();
    c.PushId(type->id);
    c.PushClip(body);
    type->draw(c, body);
    c.PopClip();
    c.PopId();
    if (c.InputEnabled() != inputWas || c.IdDepth() != ids || c.ClipDepth() != clips) {
        static std::vector<std::string> told;
        if (std::find(told.begin(), told.end(), type->id) == told.end()) {
            told.push_back(type->id);
            log::Warn("パネル「{}」が描いた後に入力・ID・切り抜きを元に戻していません (戻しました。直すべき不具合です)", type->id);
        }
        c.SetInputEnabled(inputWas);
        c.RestoreDepths(ids, clips);
    }
}

void DockSpace::DrawTabStrip(ui::Context& c, const Node& leaf, const Rect& strip, uint32_t siteId, LeafGeo& geo) {
    auto& p = c.P();
    std::vector<std::wstring> titles;
    for (const std::string& id : leaf.tabs) {
        const PanelType* type = FindPanel(id);
        titles.push_back(type ? Tr(type->title) : Utf8ToWide(id));
        geo.widths.push_back(p.TextWidth(titles.back(), 12, true) + 20 + kCloseW);
    }

    // Where the row is: kept per leaf, moved so that a tab that comes to the front is seen whole
    // (clicked, chosen from the v list, opened from the Window menu, dropped here).
    StripState& st = strips_[leaf.id];
    TabStripFit fit = FitTabStrip(geo.widths, kTabSpacing, strip.w - kStripPad * 2, kStripButtons, st.scroll);
    const std::string shown = leaf.tabs.empty() ? std::string() : leaf.tabs[static_cast<size_t>(leaf.active)];
    if (shown != st.shown || leaf.tabs.size() != st.count) {
        if (fit.overflow) fit.scroll = ScrollToShowTab(geo.widths, kTabSpacing, fit.view, fit.scroll, static_cast<size_t>(leaf.active));
        st.shown = shown;
        st.count = leaf.tabs.size();
    }
    if (fit.overflow && c.In().wheel != 0.f && c.IsHovered(strip)) {
        fit.scroll = ClampTabScroll(fit.content, fit.view, fit.scroll - c.In().wheel * kWheelStep);
        c.RequestRedraw();
    }
    const Rect view{strip.x + kStripPad, strip.y, fit.view, strip.h};
    geo.view = view;
    geo.overflow = fit.overflow;

    // < > v at the right end, only while the tabs do not fit.
    if (fit.overflow) {
        c.PushId(static_cast<uint64_t>(leaf.id));
        const float by = strip.y + 3 + (strip.h - 3 - kStripButton) * 0.5f;
        Rect b{view.Right() + 4, by, kStripButton, kStripButton};
        if (w::IconButton(c, "tabs.left", b, w::Icon::ChevronLeft, fit.scroll > 0.5f)) {
            fit.scroll = StepTabScroll(geo.widths, kTabSpacing, fit.view, fit.scroll, -1);
        }
        b.x += kStripButton;
        if (w::IconButton(c, "tabs.right", b, w::Icon::ChevronRight, fit.scroll < fit.content - fit.view - 0.5f)) {
            fit.scroll = StepTabScroll(geo.widths, kTabSpacing, fit.view, fit.scroll, 1);
        }
        b.x += kStripButton;
        if (w::IconButton(c, "tabs.list", b, w::Icon::ChevronDown)) {
            TabList& list = tabLists_[siteId];
            list.leaf = leaf.id;
            list.popup.OpenAt(b.x, b.Bottom() + 2);
            menus_[siteId].popup.open = false;
        }
        c.PopId();
    }
    st.scroll = fit.scroll;

    c.PushClip(view);
    float x = view.x - fit.scroll;
    for (size_t i = 0; i < leaf.tabs.size(); ++i) {
        const std::string& id = leaf.tabs[i];
        const std::wstring& title = titles[i];
        const bool sel = static_cast<int>(i) == leaf.active;
        const float tw = geo.widths[i];
        const Rect tab{x, strip.y + 3, tw, strip.h - 3};
        x += tw + kTabSpacing;
        if (tab.Right() < view.x || tab.x > view.Right()) continue;  // out of view: nothing to draw or touch

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
            drag_.fromLeaf = geo.rect;
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
            tabLists_[siteId].popup.open = false;
            Post([this, id] { layout_.Activate(id); });
        }
        c.PopId();

        w::DrawTab(c, tab, sel, b.hovered);
        p.Text({tab.x + 10, tab.y, tw - 10 - kCloseW, tab.h}, title, TabText(sel ? theme::Text : theme::TextDim));
        if (showClose) {
            if (cb.hovered) w::DrawSurface(c, close, cb.held ? w::Surface::Pressed : w::Surface::Hovered);
            w::DrawIcon(c, close, w::Icon::Close, cb.hovered ? theme::Text : theme::TextFaint);
        }
    }
    c.PopClip();
}

void DockSpace::DrawSplitter(ui::Context& c, const Node& split, const Rect& r) {
    const bool horizontal = split.axis == Axis::Horizontal;
    const Rect bar = SplitterRect(split, r, kGap);
    const Rect hit = horizontal ? bar.Inset(-kSplitterGrab, 0, -kSplitterGrab, 0) : bar.Inset(0, -kSplitterGrab, 0, -kSplitterGrab);
    c.PushId(static_cast<uint64_t>(split.id));
    const ui::Id id = c.MakeId("splitter");
    c.PopId();
    const auto b = c.ButtonBehavior(id, hit);
    const bool dragging = c.Active() == id;
    if (b.hovered || dragging) {
        c.SetCursor(horizontal ? ui::Cursor::SizeWE : ui::Cursor::SizeNS);
        // The seam lights up (2px, over the panels' edges).
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
                if (leaf.overflow) ScrollWhileDragging(leaf, hx, pt);
                // Counted in the row (hidden tabs too); a drop on the buttons goes after the last tab seen.
                const auto st = strips_.find(leaf.id);
                const float scroll = leaf.overflow && st != strips_.end() ? st->second.scroll : 0.f;
                const float rx = std::clamp(hx, leaf.view.x, leaf.view.Right()) - leaf.view.x + scroll;
                const int index = TabInsertIndex(leaf.widths, kTabSpacing, rx);
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
    drag_.lastCursor = pt;
    if (changed && onRedrawAll) onRedrawAll();
}

void DockSpace::ScrollWhileDragging(const LeafGeo& leaf, float x, POINT cursor) {
    // Moved only when the cursor moves: redrawing on its own would make the dragging window paint
    // itself forever (see UpdateHover). Held still, the row stays; a little wiggle keeps it going.
    const double now = platform::NowSeconds();
    const double dt = std::min(now - drag_.lastScrollTime, 0.05);
    drag_.lastScrollTime = now;
    if (drag_.kind != DragKind::Tab || (cursor.x == drag_.lastCursor.x && cursor.y == drag_.lastCursor.y)) return;
    int dir = 0;
    if (x < leaf.view.x + kDragEdge) dir = -1;
    if (x > leaf.view.Right() - kDragEdge) dir = 1;  // the < > v buttons count as the right end
    if (dir == 0) return;
    StripState& st = strips_[leaf.id];
    const TabStripFit fit = FitTabStrip(leaf.widths, kTabSpacing, leaf.strip.w - kStripPad * 2, kStripButtons, st.scroll);
    const float next = ClampTabScroll(fit.content, fit.view, st.scroll + static_cast<float>(dir * kDragScrollSpeed * dt));
    if (next == st.scroll) return;
    st.scroll = next;
    if (onRedrawAll) onRedrawAll();
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
            DrawGuide(c, g.rect, g.zone, g.rect.Contains(drag_.hoverX, drag_.hoverY));
        }
    }
    // The tab being dragged follows the mouse inside the window it came from.
    if (drag_.kind == DragKind::Tab && drag_.fromSite == siteId && c.Viewport().Contains(c.In().mouseX, c.In().mouseY)) {
        const PanelType* type = FindPanel(drag_.panel);
        const std::wstring title = type ? Tr(type->title) : Utf8ToWide(drag_.panel);
        const float tw = p.TextWidth(title, 12, true) + 24;
        const Rect ghost{c.In().mouseX + 12, c.In().mouseY + 10, tw, 26};
        w::DrawSurface(c, ghost, w::Surface::Popup, 5.f);
        p.StrokeRoundRect(ghost, 5, theme::Accent);
        ui::TextStyle ts = TabText(theme::Text);
        ts.h = ui::HAlign::Center;
        p.Text(ghost, title, ts);
    }

    DrawTabList(c, siteId);

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
        {Tr(L"閉じる"), Tr(L"中クリック")},
        {Tr(L"別ウィンドウに出す"), L"", !aloneInWindow},
        {Tr(L"エディターのウィンドウに戻す"), L"", fid != 0},
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

void DockSpace::DrawTabList(ui::Context& c, uint32_t siteId) {
    const auto it = tabLists_.find(siteId);
    if (it == tabLists_.end() || !it->second.popup.open) return;
    TabList& list = it->second;
    const Node* leaf = layout_.FindNode(list.leaf);
    if (!leaf || !leaf->IsLeaf() || leaf->tabs.empty()) {
        list.popup.open = false;
        return;
    }
    std::vector<w::MenuItem> items;
    float width = 200;
    for (size_t i = 0; i < leaf->tabs.size(); ++i) {
        const PanelType* type = FindPanel(leaf->tabs[i]);
        w::MenuItem item;
        item.label = type ? Tr(type->title) : Utf8ToWide(leaf->tabs[i]);
        item.checked = static_cast<int>(i) == leaf->active;
        width = std::max(width, c.P().TextWidth(item.label, theme::FontSize) + 64);
        items.push_back(std::move(item));
    }
    const std::vector<std::string> tabs = leaf->tabs;  // a copy: activating edits the layout
    const int pick = w::MenuPopup(c, list.popup, items, width);
    if (pick >= 0) {
        // In front = moved into view (DrawTabStrip, next frame).
        layout_.Activate(tabs[static_cast<size_t>(pick)]);
        if (onRedrawAll) onRedrawAll();
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
