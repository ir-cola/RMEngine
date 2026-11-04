#pragma once
// Editor widgets: the core UI foundation (ui::Context) + the editor theme.

#include "ui/Context.h"

#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::w {

enum class ButtonKind { Primary, Secondary, Ghost };

bool Button(ui::Context& c, std::string_view id, const ui::Rect& r, std::wstring_view label,
            ButtonKind kind = ButtonKind::Secondary, bool enabled = true);

// Single-line text field (IME, selection, clipboard). Returns true when the value changed.
bool TextField(ui::Context& c, std::string_view id, const ui::Rect& r, std::string& valueUtf8,
               std::wstring_view placeholder = {}, bool enabled = true);

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

// --- Modal dialog -------------------------------------------------------------
// Dims the screen, draws the dialog box and title, returns the content area.
ui::Rect BeginModal(ui::Context& c, float width, float height, std::wstring_view title);

}  // namespace rm::editor::w
