#pragma once
// Where the panels are: the docking tree (pure - no windows, no drawing, tested in RMTests).
//
//   Layout
//     main       the tree inside the editor window (never null; may be one empty leaf)
//     floating   panels taken out of the editor into their own OS windows, one tree each
//
//   Node = split (two children side by side or stacked, with a ratio)
//        | leaf  (a tab stack of panel ids, one of them shown)
//
// Every panel id appears at most once in the whole layout. A panel that is not in it is closed.
// The layout is the user's view of the editor, not project data: it is not undoable and does
// not make the project "unsaved" (it is saved next to the project, in UserSettings/).

#include "base/Json.h"
#include "ui/Types.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::dock {

// Horizontal: a | b (side by side).  Vertical: a over b.
enum class Axis : uint8_t { Horizontal, Vertical };
// Where a dragged panel goes relative to a target node.
enum class Zone : uint8_t { Center, Left, Right, Top, Bottom };

struct Node {
    uint32_t id = 0;  // identifies the node while the editor runs (not saved)

    // Split (when a and b are set). ratio = share of a, 0..1.
    Axis axis = Axis::Horizontal;
    float ratio = 0.5f;
    std::unique_ptr<Node> a, b;

    // Leaf: the tab stack.
    std::vector<std::string> tabs;
    int active = 0;

    bool IsLeaf() const { return !a; }
    bool IsEmptyLeaf() const { return IsLeaf() && tabs.empty(); }
};

struct ScreenRect {  // window rectangle in physical pixels of the virtual screen
    int x = 0, y = 0, w = 0, h = 0;
};

struct Floating {
    uint32_t id = 0;
    ScreenRect rect;
    std::unique_ptr<Node> root;
};

class Layout {
public:
    Layout();
    Layout(Layout&&) noexcept = default;
    Layout& operator=(Layout&&) noexcept = default;
    Layout Clone() const;

    std::unique_ptr<Node> main;
    std::vector<Floating> floating;

    // --- Building (default layouts) ---
    std::unique_ptr<Node> MakeLeaf(std::vector<std::string> tabs, int active = 0);
    std::unique_ptr<Node> MakeSplit(Axis axis, float ratio, std::unique_ptr<Node> a, std::unique_ptr<Node> b);

    // --- Queries ---
    bool Contains(std::string_view panel) const;
    // The leaf that holds the panel; floatingId = 0 when it is in the main tree.
    const Node* LeafOf(std::string_view panel, uint32_t* floatingId = nullptr) const;
    Node* FindNode(uint32_t id);
    const Node* FindNode(uint32_t id) const;
    // 0 = main tree, otherwise the id of the floating window that holds the node, ~0u = nowhere.
    uint32_t SiteOf(uint32_t nodeId) const;
    Floating* FindFloating(uint32_t id);
    std::vector<std::string> Panels() const;  // every open panel (tree order)

    // --- Edits. Each returns true when the layout changed. ---
    bool Activate(std::string_view panel);
    bool Close(std::string_view panel);
    // Opens a closed panel as a tab of the given leaf (a missing leaf -> the first leaf of the main tree).
    // An open panel is only brought to front.
    bool Open(std::string_view panel, uint32_t leafId);
    // Moves a panel next to / into a target node. Center needs a leaf target; tabIndex = position in
    // its tab strip (-1 = end). Edges split the target (which may be a split: then the whole group is wrapped).
    bool Dock(std::string_view panel, uint32_t targetId, Zone zone, int tabIndex = -1);
    // Moves a whole floating window back (all of its panels).
    bool DockFloating(uint32_t floatingId, uint32_t targetId, Zone zone);
    // Takes a panel out into a new floating window at rect. Returns its id (0 = nothing done).
    // A panel that is already alone in a floating window just moves that window.
    uint32_t Float(std::string_view panel, ScreenRect rect);
    // Closes a floating window and every panel in it.
    bool CloseFloating(uint32_t floatingId);

    // Removes unknown and repeated panels, fixes ratios / active tabs, drops empty leaves and windows.
    // Run after loading: a saved layout may name panels of a plugin that is gone.
    void Sanitize(const std::vector<std::string>& knownPanels);

    // --- Save format ---
    json::Value ToJson() const;
    static std::optional<Layout> FromJson(const json::Value& v);

private:
    uint32_t NewId() { return nextId_++; }
    std::unique_ptr<Node>* FindSlot(uint32_t id);
    // Removes the panel from its leaf without pruning (target nodes must survive until the insert).
    std::unique_ptr<Node> Extract(std::string_view panel, uint32_t* fromLeaf, int* fromIndex);
    bool Insert(std::unique_ptr<Node> sub, uint32_t targetId, Zone zone, int tabIndex);
    void Prune();
    void Renumber(Node& n);

    uint32_t nextId_ = 1;
};

// --- Geometry (pure) ---------------------------------------------------------------------

struct Placed {
    const Node* node = nullptr;
    ui::Rect rect;
};

// Rectangles of every node of a tree, parents before children. gap = splitter thickness.
void Arrange(const Node& root, const ui::Rect& area, float gap, std::vector<Placed>& out);
// Rectangles of the two children of a split placed at r.
void SplitChildren(const Node& split, const ui::Rect& r, float gap, ui::Rect& a, ui::Rect& b);
// The splitter bar of a split placed at r.
ui::Rect SplitterRect(const Node& split, const ui::Rect& r, float gap);
// The ratio that puts the splitter under the mouse, keeping both sides at least minSize.
float RatioAt(const Node& split, const ui::Rect& r, float gap, float mouse, float minSize);
// Where a panel dropped on target with zone would end up (for the preview).
ui::Rect ZonePreview(const ui::Rect& target, Zone zone, bool targetIsLeaf);
// Share the new part gets when an edge drop splits a node.
float EdgeShare(bool targetIsLeaf);

// --- Tab strip: a row of tabs that may be longer than the strip (pure) ----------------------
//
//   | pad | tab0 | spacing | tab1 | ... |  < > v | pad |
//         '-------- view (moved by scroll) -------'  buttons only while the tabs do not fit
//
// Positions are measured from the start of the row (the left of tab0 when scroll = 0).
struct TabStripFit {
    float content = 0;      // all tabs side by side, with the spacing between them
    float view = 0;         // the width the tabs are shown in
    float scroll = 0;       // how far the row is moved to the left (kept in 0 .. content - view)
    bool overflow = false;  // the tabs do not fit: the buttons take room at the right
};
// available = the strip width minus its padding; buttons = room the < > v buttons need.
TabStripFit FitTabStrip(std::span<const float> widths, float spacing, float available, float buttons, float scroll);
float TabLeft(std::span<const float> widths, float spacing, size_t index);
float ClampTabScroll(float content, float view, float scroll);
// The scroll that shows tab `index` whole, moving as little as possible (a tab wider than the
// view shows its left side).
float ScrollToShowTab(std::span<const float> widths, float spacing, float view, float scroll, size_t index);
// One press of < (dir -1) or > (dir +1): brings the next tab that is cut off or hidden on that side
// fully into view.
float StepTabScroll(std::span<const float> widths, float spacing, float view, float scroll, int dir);
// Where a tab dropped at x (relative to the row start, scroll already added) is inserted.
int TabInsertIndex(std::span<const float> widths, float spacing, float x);

}  // namespace rm::editor::dock
