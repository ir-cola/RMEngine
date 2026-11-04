#pragma once
// Draws a docking tree in a window and lets the user rearrange it (editor only).
//
//   tab strip      click = show, drag = move, middle click / x = close, right click = menu
//   splitter       drag = resize (the ratio is kept, so the panels scale with the window)
//   drop guides    while dragging, guides appear on the panel under the cursor (and on the
//                  edges of the window). Releasing on a guide docks there; on a tab strip it
//                  becomes a tab; anywhere else the panel goes out into its own window.
//
// One DockSpace serves every window of the editor: the editor window (site 0) and each
// floating window (site = its floating id). A drag started in one window can end in another,
// so the drop target is found from the screen position and the window under it.
// The layout is edited only at the end of a frame (never while its nodes are being drawn).

#include "editor/dock/DockLayout.h"
#include "editor/widgets/Widgets.h"
#include "platform/Win.h"
#include "ui/Context.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::editor::dock {

struct PanelType {
    std::string id;       // "core.console" - also the key in the saved layout
    std::wstring title;   // tab label
    std::function<void(ui::Context&, const ui::Rect&)> draw;
};

class DockSpace {
public:
    // Called after a change of the tree structure (panels moved / opened / closed, windows added
    // or removed). The owner creates / destroys floating windows to match the layout.
    std::function<void()> onStructureChanged;
    // Redraw every window that shows a dock (a drag shows guides in another window).
    std::function<void()> onRedrawAll;

    void SetPanelTypes(std::vector<PanelType> types) { types_ = std::move(types); }
    const std::vector<PanelType>& PanelTypes() const { return types_; }
    const PanelType* FindPanel(std::string_view id) const;
    std::vector<std::string> PanelIds() const;

    Layout& GetLayout() { return layout_; }
    const Layout& GetLayout() const { return layout_; }
    void SetLayout(Layout layout);

    // --- Per window ---
    void Draw(ui::Context& c, const ui::Rect& area, uint32_t site, HWND hwnd, float scale);
    // Drop guides, the dragged tab and the tab menu. Draw last, with input enabled.
    void DrawOverlay(ui::Context& c, uint32_t site);
    void ForgetSite(uint32_t site);

    // --- Floating window moved by its title bar (WM_MOVING / WM_EXITSIZEMOVE) ---
    void WindowMoving(uint32_t floatingId, HWND hwnd);
    void WindowMoveEnded(uint32_t floatingId);

    // --- Commands (menus) ---
    void TogglePanel(std::string_view id);
    void FloatPanel(std::string_view id);  // into a new window next to where it is now
    void CloseFloating(uint32_t floatingId);  // the X of a floating window: its panels are closed
    bool IsDragging() const { return drag_.kind == DragKind::Tab || drag_.kind == DragKind::Window; }
    // Window title for a floating window: its panels' names.
    std::wstring FloatingTitle(uint32_t floatingId) const;

private:
    enum class DragKind { None, Pending, Tab, Window };

    struct LeafGeo {
        uint32_t id = 0;
        ui::Rect rect;   // whole leaf
        ui::Rect strip;  // tab strip
        std::vector<ui::Rect> tabs;
    };
    struct Site {
        HWND hwnd = nullptr;
        float scale = 1.f;
        ui::Rect area;
        uint32_t rootId = 0;
        bool rootIsLeaf = true;
        std::vector<LeafGeo> leaves;
    };
    struct Guide {
        ui::Rect rect;
        uint32_t node = 0;
        Zone zone = Zone::Center;
        bool leaf = true;
        ui::Rect target;  // rectangle of the target node (for the preview)
    };
    struct Target {
        bool valid = false;
        uint32_t site = 0;
        uint32_t node = 0;
        Zone zone = Zone::Center;
        int tabIndex = -1;
        ui::Rect preview;  // in the target window, DIPs
    };
    struct Drag {
        DragKind kind = DragKind::None;
        std::string panel;      // Tab
        uint32_t floating = 0;  // Window
        uint32_t fromSite = 0;
        float startX = 0, startY = 0;
        ui::Rect fromLeaf;
        // hover (updated from the cursor position on the screen)
        uint32_t hoverSite = ~0u;
        float hoverX = 0, hoverY = 0;  // in the hovered window, DIPs
        uint32_t hoverLeaf = 0;        // leaf under the cursor (its guides are shown)
        Target target;
    };
    struct TabMenu {
        w::Popup popup;
        std::string panel;
    };

    const Node* RootOf(uint32_t site) const;
    void DrawLeaf(ui::Context& c, const Node& leaf, const ui::Rect& r, uint32_t site, LeafGeo& geo);
    void DrawSplitter(ui::Context& c, const Node& split, const ui::Rect& r);
    std::vector<Guide> GuidesAt(const Site& site, float x, float y) const;
    void UpdateHover(HWND exclude);
    void FinishTabDrag();
    void CancelDrag();
    void Post(std::function<void()> fn) { post_.push_back(std::move(fn)); }
    void Changed();
    ScreenRect FloatRectNear(const Site& site, const ui::Rect& leafRect, POINT at) const;
    uint32_t LargestMainLeaf() const;

    std::vector<PanelType> types_;
    Layout layout_;
    std::unordered_map<uint32_t, Site> sites_;
    std::unordered_map<uint32_t, TabMenu> menus_;
    Drag drag_;
    std::vector<std::function<void()>> post_;
};

}  // namespace rm::editor::dock
