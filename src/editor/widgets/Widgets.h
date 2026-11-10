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
// The pictures of the theme in use (the seasons), laid faintly over the main window (Hub / editor):
//   スプリング  a cherry tree along the right edge with petals blowing off it, a blossom
//               (bottom-left), a flurry of petals (top-left)
//   サマー      a mound of sand with a parasol and a sand castle (bottom-right, a silhouette like
//               spring's tree), clouds (top-right), the sun (bottom-left)
//   フォール    leaves thick along the top and scattered below, a big maple leaf (bottom-left)
//   ウィンター  falling snow (held still), icicles from the top edge, snow piled at the bottom of every
//               panel (DrawPanelFrame notes them: ui::Context::Decorate), a snowflake (bottom-left)
// Drawn after the window's content and before its menus and dialogs, never over the areas the
// content asked to keep clear (ui::Context::KeepClear - the scene and game views). Nothing for most themes.
void DrawOrnament(ui::Context& c, const ui::Rect& window);
// The same for a panel window of its own (a panel dragged out): spring's petals, summer's clouds,
// winter's snow. The silhouettes stay in the main window.
void DrawFloatOrnament(ui::Context& c, const ui::Rect& window);
// A strip of the window's frame (menu bar, status bar, sidebar): gentle gradient, no border.
void DrawBar(ui::Context& c, const ui::Rect& r, ui::Color base);

// --- Icons: drawn with lines, so they sit in the middle of their box at any size ------------
enum class Icon { Close, Dots, DotsVertical, ChevronDown, ChevronRight, ChevronUp, Check, Object, Cube, Plus, Grip };
void DrawIcon(ui::Context& c, const ui::Rect& r, Icon icon, ui::Color color);

// --- Buttons ---------------------------------------------------------------------------------
bool Button(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label,
            ButtonKind kind = ButtonKind::Secondary, bool enabled = true);
// A small square button with an icon (Ghost look: only a surface under the mouse).
bool IconButton(ui::Context& c, std::string_view id, const ui::Rect& r, Icon icon, bool enabled = true);
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
ui::Context::ButtonResult ComboBox(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view text, bool enabled = true);

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
bool Checkbox(ui::Context& c, std::string_view id, const ui::Rect& r, bool& value, std::wstring_view label);
// A switch as on a phone: a long round track, its round knob slides to the right when on (the track
// turns the accent color). `offLabel` / `onLabel` (optional) stand at its left / right; the one that
// is chosen is bright. A click anywhere on it turns it over. Returns true when clicked (`on` changed).
// The width it takes: SwitchWidth.
bool Switch(ui::Context& c, std::string_view id, float x, float y, bool& on, std::wstring_view offLabel = {},
            std::wstring_view onLabel = {});
float SwitchWidth(ui::Context& c, std::wstring_view offLabel = {}, std::wstring_view onLabel = {});
inline constexpr float kSwitchH = 22.f;

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
};

struct Popup {
    bool open = false;
    bool fresh = false;  // opened during this frame: the click that opened it is not an "outside" click
    float x = 0, y = 0;
    void OpenAt(float px, float py) {
        open = true;
        fresh = true;
        x = px;
        y = py;
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
};
std::wstring ShortcutText(const Action& a);  // "Ctrl+D", "Delete", "" (no shortcut)
// Draws `popup` (when open) with `actions` at the end of the frame (not cut off by a panel) and runs
// the picked row. The list is kept by the caller until the menu closes.
void ActionMenu(ui::Context& c, Popup& popup, const std::vector<Action>& actions);

// --- Modal dialog -------------------------------------------------------------
// Dims the screen, draws the dialog box and title, returns the content area.
ui::Rect BeginModal(ui::Context& c, float width, float height, std::wstring_view title);

}  // namespace rm::editor::w
