#pragma once
// Editor widgets: the core UI foundation (ui::Context) + the editor theme.
//
// Used like ImGui - one call draws a thing and says what the user did with it:
//
//   if (w::Button(c, "save", r, Tr(L"保存"), w::ButtonKind::Primary)) Save();
//   if (w::Selectable(c, "row", r, selected).clicked) Select();
//   if (w::CollapsingHeader(c, "hdr", r, title, open)) ...;
//   if (w::IconButton(c, "more", r, w::Icon::Dots)) OpenMenu();
//
// Every box on screen is one of the Surfaces below, every list row DrawRow, every tab DrawTab,
// every icon DrawIcon - one drawing each (rule 3). Screens do not fill rectangles of their own:
// a new look is made here once and shows everywhere. Colors and gradients come from Theme.h.

#include "editor/widgets/Theme.h"
#include "ui/Context.h"

#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::w {

enum class ButtonKind { Primary, Secondary, Ghost };

// --- Surfaces ------------------------------------------------------------------------------
enum class Surface {
    Panel,          // a panel / card / list: gentle gradient, border
    Raised,         // a button / header: raised gradient, a light line on top, border
    Hovered,        // Raised under the mouse
    Pressed,        // Raised, pushed in (the gradient turned over)
    Sunken,         // a text field / an unchecked box: darker at the top
    Accent,         // the primary button / a checked box
    AccentHovered,
    AccentPressed,
    Selected,       // a selected item
    Popup,          // a menu / tooltip / dialog: soft shadow, raised gradient, border
};
void DrawSurface(ui::Context& c, const ui::Rect& r, Surface s, float radius = theme::Radius);
// A panel with its edge (theme::EdgeOuter / EdgeInner): the dark line lies 1px OUTSIDE r, so two
// panels 1px apart share one dark seam. Each corner its own radius - square where it touches another.
void DrawPanelFrame(ui::Context& c, const ui::Rect& r, ui::Corners radii);

// --- A theme's pictures: always behind the content ---------------------------------------
// A theme pack's code draws them (offered at kOrnamentPoint; a theme file of the same plugin names
// one by `name` - "ornament").
struct OrnamentInfo {
    const char* name;
    // The picture of a whole window (`floating`: a panel window of its own - a lighter picture). Drawn
    // on the window's background, then again on each panel and bar (clipped to it) after its fill.
    void (*window)(ui::Context& c, const ui::Rect& window, bool floating);
    // Something of a panel's own, after the window's picture on it (winter: snow at its bottom). May be null.
    void (*panel)(ui::Context& c, const ui::Rect& r, float corner);
};
inline constexpr char kOrnamentPoint[] = "core.theme_ornament";
// First in a frame, after clearing the window: the theme's picture on its background.
void DrawBackdrop(ui::Context& c, bool floating);
// The picture again on a surface just filled (DrawPanelFrame / DrawBar do it), clipped to `r`, and
// (`panelThings`) the pack's things of a panel (winter: snow piled at its bottom) - not on bars.
void DrawBackdropOn(ui::Context& c, const ui::Rect& r, float corner = 0, bool panelThings = true);

// A strip of the window's frame (menu bar, status bar, sidebar): gentle gradient, no border.
void DrawBar(ui::Context& c, const ui::Rect& r, ui::Color base);

// --- Icons: drawn with lines, so they sit in the middle of their box at any size ------------
enum class Icon {
    Close, Dots, DotsVertical, ChevronDown, ChevronRight, ChevronUp, ChevronLeft, Check, Dash, Object, Cube, Square, Plus, Grip, Play, Pause, Step, Stop,
    // what an entity is (the 階層, the scene view's marks)
    // Point: a thing with only a place (a ring with a dot - the scene view's mark of an empty entity);
    // Effect: a sparkle (パーティクル); Scene: layers (a scene - the 階層's top row)
    Camera, Light, Point, Effect, Scene,
    // what a file is (the アセット panel, reference fields)
    Folder, File, Image, Audio, Font, Text,
};
void DrawIcon(ui::Context& c, const ui::Rect& r, Icon icon, ui::Color color);

// --- Buttons ---------------------------------------------------------------------------------
bool Button(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label,
            ButtonKind kind = ButtonKind::Secondary, bool enabled = true);
// A small square button with an icon (Ghost look: only a surface under the mouse).
bool IconButton(ui::Context& c, std::string_view id, const ui::Rect& r, Icon icon, bool enabled = true);
// An icon button that stays pressed in (the accent) while `on` - a game's ▶ while it plays. `tooltip`
// (optional) shows under the mouse.
bool IconToggle(ui::Context& c, std::string_view id, const ui::Rect& r, Icon icon, bool on, bool enabled = true,
                std::wstring_view tooltip = {});
// One of a few choices, side by side in one floating strip (the scene view's 移動 / 回転 / 拡大縮小):
// the chosen one in the accent. Returns the index clicked, -1 when none. `tooltips` (optional, one
// per label) shows under the mouse.
int Segmented(ui::Context& c, std::string_view id, const ui::Rect& r, std::span<const std::wstring_view> labels, int selected,
              std::span<const std::wstring_view> tooltips = {});

// --- Rows (lists, trees, menus, sidebars) ----------------------------------------------------
struct RowLook {
    bool selected = false;
    bool hovered = false;
    bool focused = true;  // the list has the keys: a selected row shows the accent bar
    bool flat = false;    // square, edge to edge: rows of a list split by lines (Hub's pages, project list)
};
void DrawRow(ui::Context& c, const ui::Rect& r, RowLook look);
// A clickable row (ImGui's Selectable). Draws the row; what is on it is drawn by the caller.
ui::Context::ButtonResult Selectable(ui::Context& c, std::string_view id, const ui::Rect& r, bool selected, bool flat = false);
// One theme to choose (the theme screen): a small window drawn in that theme's own colors, its name
// below, the chosen one outlined in the accent with a check. palette = nullptr: "PC の設定に合わせる"
// (half dark, half light).
ui::Context::ButtonResult ThemeCard(ui::Context& c, std::string_view id, const ui::Rect& r, const theme::Palette* palette,
                                    std::wstring_view name, bool selected);
// A page link of a sidebar: a flat row (the caller draws the lines between them). True when clicked.
bool NavItem(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label, bool selected);

// --- Tabs and headers ------------------------------------------------------------------------
void DrawTab(ui::Context& c, const ui::Rect& r, bool selected, bool hovered);
// A section that folds (ImGui's CollapsingHeader): flips `open` and returns true when clicked.
// `reserveRight` leaves room on the right for buttons the caller draws on it.
bool CollapsingHeader(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view title, bool& open,
                      float reserveRight = 0.f);

// --- A drop-down list's face (the list itself is a MenuPopup the caller opens) -------------
// `icon`: a picture in front of the text (a reference field: what kind of file).
ui::Context::ButtonResult ComboBox(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view text, bool enabled = true,
                                   std::optional<Icon> icon = std::nullopt);

// How far something is: a sunken track filled with the accent up to `fraction` (0..1).
void ProgressBar(ui::Context& c, const ui::Rect& r, float fraction);

// One fact of a list (the debug panel): its name dim on the left (`labelWidth`), its value on the right.
// `lit`: the value in the accent (a button held, something that is on).
void InfoRow(ui::Context& c, const ui::Rect& r, std::wstring_view label, std::wstring_view value, float labelWidth, bool lit = false);
// The same with the value in the danger colour (an error to be seen: a build that failed).
void InfoRowDanger(ui::Context& c, const ui::Rect& r, std::wstring_view label, std::wstring_view value, float labelWidth);
// A window shaped like a file folder (ゲーム制作's デバッグ): tabs standing on a sheet. Draw the tabs not
// chosen, then the sheet, then the chosen tab - it is the sheet's colour and joins it (no line between).
// FolderTab: true when clicked. squareTopLeft: the first tab is chosen (the sheet's corner under it is
// its side, not round).
bool FolderTab(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label, bool selected, float radius);
void DrawFolderSheet(ui::Context& c, const ui::Rect& r, float radius, bool squareTopLeft);

// Values over time, the newest on the right (the debug panel's frame times): `a` a line in the accent,
// `b` (may be empty) one in the warning colour, 0 at the bottom and `top` at the top (higher ones are cut);
// `guides`: faint lines across at those values.
void LineGraph(ui::Context& c, const ui::Rect& r, std::span<const float> a, std::span<const float> b, float top, std::span<const float> guides);

// --- Lines and tips --------------------------------------------------------------------------
void Separator(ui::Context& c, float x, float y, float width);
// A tip next to the mouse, drawn at the end of the frame (over everything).
void Tooltip(ui::Context& c, std::wstring_view text);

// Single-line text field (IME, selection, clipboard). Returns true when the value changed.
// Dragging the mouse selects; a double click selects everything. selectAllOnFocus: a click that
// enters the field selects all of it (a value to type over - the inspector's fields); a drag that
// enters it selects what it goes over instead.
bool TextField(ui::Context& c, std::string_view id, const ui::Rect& r, std::string& valueUtf8,
               std::wstring_view placeholder = {}, bool enabled = true, bool selectAllOnFocus = false);

// Check box with a label. Returns true when the value changed.
// mixed: the objects edited together do not agree - a dash instead of the check; a click makes it on.
bool Checkbox(ui::Context& c, std::string_view id, const ui::Rect& r, bool& value, std::wstring_view label, bool mixed = false);
// A number in a text field (a view's setting - the scene view's steps): the text as typed while the field
// has the focus, the value otherwise (`text` keeps it between frames). True when `value` changed: a number
// typed (brought into min .. max); text that is not one changes nothing.
bool NumberField(ui::Context& c, std::string_view id, const ui::Rect& r, float& value, float min, float max, std::string& text);
// The rectangle being dragged out to select what is in it (a scene view): faint accent with its edge.
void DrawSelectionBand(ui::Context& c, const ui::Rect& r);
// A switch as on a phone: a long round track, its round knob slides to the right when on (the track
// turns the accent color). `offLabel` / `onLabel` (optional) stand at its left / right; the one that
// is chosen is bright. A click anywhere on it turns it over. Returns true when clicked (`on` changed).
// The width it takes: SwitchWidth.
bool Switch(ui::Context& c, std::string_view id, float x, float y, bool& on, std::wstring_view offLabel = {},
            std::wstring_view onLabel = {});
float SwitchWidth(ui::Context& c, std::wstring_view offLabel = {}, std::wstring_view onLabel = {});
inline constexpr float kSwitchH = 22.f;

// A small picture of a file (the アセット panel's thumbnails): on a faint square, fitted inside `r`
// keeping its shape; faint when `dimmed`.
void DrawThumbnail(ui::Context& c, const ui::Rect& r, ID2D1Bitmap* bitmap, bool dimmed = false);

// A slider: a thin track (the accent up to the knob) and a round knob. Pressing anywhere on it and
// dragging sets `value` between min and max (it follows the mouse outside too, until let go).
// Returns true while it changes the value.
bool Slider(ui::Context& c, std::string_view id, const ui::Rect& r, float& value, float min, float max);

// Rounded panel with a title bar. Returns the content area.
ui::Rect Panel(ui::Context& c, const ui::Rect& r, std::wstring_view title);

// Vertical scrolling. Draw the content between Begin and End, offset by the returned value.
float BeginScroll(ui::Context& c, std::string_view id, const ui::Rect& view, float contentHeight);
void EndScroll(ui::Context& c, std::string_view id, const ui::Rect& view, float contentHeight);
// Jump to the end on the next BeginScroll (consoles that follow new output).
void ScrollToEnd(ui::Context& c, std::string_view id);

// --- Popup menu ---------------------------------------------------------------
struct MenuItem {
    std::wstring label;
    std::wstring shortcut;
    bool enabled = true;
    bool separator = false;
    bool checked = false;  // a check mark in front of the label
    bool submenu = false;  // opens another menu (a ▸ at the right; ActionMenu's folders)
};

struct Popup {
    bool open = false;
    bool fresh = false;  // opened during this frame: the click that opened it is not an "outside" click
    float x = 0, y = 0;
    std::wstring sub;  // ActionMenu: the folder whose menu is open now ("追加/3D オブジェクト"; "" = none)
    void OpenAt(float px, float py) {
        open = true;
        fresh = true;
        x = px;
        y = py;
        sub.clear();
    }
};

// Draws an open popup. Returns the clicked item index, or -1.
// Closes on selection, Escape, or a click outside (the click is swallowed).
int MenuPopup(ui::Context& c, Popup& popup, const std::vector<MenuItem>& items, float width = 240.f);
// The rectangle MenuPopup will use (after keeping it inside the viewport).
ui::Rect MenuRect(const ui::Context& c, const Popup& popup, const std::vector<MenuItem>& items, float width = 240.f);

// A menu row that does something, optionally with a shortcut. Used by menus that a software fills
// (the 階層's right-click menu, the inspector's block menus ...).
struct Action {
    std::wstring label;
    std::function<void()> run;  // empty for a separator
    bool enabled = true;
    bool separator = false;
    ui::Key key = ui::Key::None;
    bool ctrl = false, shift = false;
    // The folder it is in ("3D", "UI", "物理/2D"; empty = at the top). ActionPicker: folders that open
    // and close in the list. ActionMenu: a row of the folder's name opens a menu of what is in it (to the
    // side, when the mouse is on the row): 追加 ▸ 3D オブジェクト ▸ 箱.
    std::wstring folder;
};
std::wstring ShortcutText(const Action& a);  // "Ctrl+D", "Delete", "" (no shortcut)
// Draws `popup` (when open) with `actions` at the end of the frame (not cut off by a panel) and runs
// the picked row. The list is kept by the caller until the menu closes. Actions in folders
// (Action::folder) are in menus of their own (Esc closes the last one opened; a click outside all of
// them closes them all).
void ActionMenu(ui::Context& c, Popup& popup, const std::vector<Action>& actions);

// A panel to pick one action from (the インスペクター's 部品を足す): `title` and × on top, a search box,
// then the actions in their folders (Action::folder), each folder opened and closed by its row. Like
// ActionMenu it is drawn at the end of the frame, runs the picked action and closes; × / Esc / a click
// outside close it. Enter runs the first one found by what was typed. Its top (where the title is)
// drags it somewhere else. Which folders are open and where it was moved to are the caller's (kept from
// one opening to the next); the search starts empty each time it opens. It is wider than a menu and
// covers other panels: while it is open the screen under it should take no input (EditorScreen does).
struct PickerState {
    std::string search;
    std::set<std::wstring> open;  // folder paths shown open (all closed at first)
    bool placed = false;          // moved by its top: opens at x, y (else where the caller opens it)
    float x = 0, y = 0;
    bool dragging = false;        // its top held: the mouse's place in it
    float grabX = 0, grabY = 0;
};
void ActionPicker(ui::Context& c, Popup& popup, PickerState& state, const std::vector<Action>& actions, std::wstring title,
                  float width = 320.f);

// A list to choose one thing from, with a search box on top (a reference field's choices). Call it at
// the end of the frame, like MenuPopup. `search` is what is typed (kept by the caller, who lists only
// the rows that match it). Returns the row clicked - or the first one on Enter - and closes; -1 for none.
struct PickRow {
    std::wstring name;
    std::wstring detail;  // faint, on the right (where it is)
    bool hasIcon = false;
    Icon icon = Icon::File;
};
// `picture` (optional): a small picture for row i, asked only for the rows in view (null: its icon);
// `rowHeight` makes room for it.
int PickerPopup(ui::Context& c, Popup& popup, std::string_view id, std::string& search, std::span<const PickRow> rows,
                float width = 320.f, float rowHeight = 26.f, const std::function<ID2D1Bitmap*(size_t row, float size)>& picture = {});

// --- Dragging a thing from one panel to another (an asset or an item of the 階層 onto a reference
// field of the インスペクター). One mouse, so one thing at a time for the whole program. The panel it
// comes from sets it while the drag goes on and ends it when the button is let go; a place that takes
// it asks DropTarget. The window that has the mouse captured sees the drag; when the mouse is over
// another window of the program, the editor tells that window where (SetDragElsewhere), so its places
// light up and take the thing too (a panel moved out into its own window).
struct DragThing {
    std::string kind;                 // what it is: "asset" (values: ids) / "item" (values: ids of the 階層's items)
    std::vector<std::string> values;  // one or more
};
void SetDragThing(DragThing thing);
// Ends the drag at the end of this frame: places drawn later in the same frame still see it let go.
void EndDragThing(ui::Context& c);
const DragThing* DraggedThing();
// A place it can be dropped on. While a thing that `accepts` takes is over `r`, outlines it; returns
// the thing when the mouse is let go there (else null).
const DragThing* DropTarget(ui::Context& c, const ui::Rect& r, const std::function<bool(const DragThing&)>& accepts);
// Whether a DropTarget under the mouse took the thing in this frame (the tag next to the mouse shows
// it). Reading it forgets it - call it once a frame, at its end.
bool TakeDragAccepted();
// The dragged thing is over another window (`target`: its context; x, y: the mouse there, in its
// DIPs); `released`: let go there. Null target: over no other window of ours.
void SetDragElsewhere(const ui::Context* target, float x, float y, bool released);
// Where the drag is now, for the window it was told to (SetDragElsewhere): null when none.
const ui::Context* DragElsewhereTarget();
// At the end of a window's frame: a drop handed to it is used up (taken or not).
void EndDragFrame(ui::Context& c);

// --- Modal dialog -------------------------------------------------------------
// Dims the screen, draws the dialog box and title, returns the content area.
ui::Rect BeginModal(ui::Context& c, float width, float height, std::wstring_view title);

}  // namespace rm::editor::w
