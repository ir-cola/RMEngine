#include "editor/shell/FloatWindow.h"

#include "editor/shell/EditorScreen.h"

namespace rm::editor {

bool FloatWindow::Open(const dock::ScreenRect& rect, HWND owner, const std::wstring& title, bool activate) {
    platform::WindowDesc d;
    d.title = title;
    d.minWidthDip = 240;
    d.minHeightDip = 160;
    d.owner = owner;
    d.tool = true;
    d.hasRect = true;
    d.x = rect.x;
    d.y = rect.y;
    d.w = rect.w;
    d.h = rect.h;
    if (!CreateScreenWindow(d, activate ? SW_SHOW : SW_SHOWNOACTIVATE)) return false;
    CurrentRect();
    return true;
}

void FloatWindow::MoveTo(const dock::ScreenRect& r) {
    SetWindowPos(Hwnd(), nullptr, r.x, r.y, r.w, r.h, SWP_NOZORDER | SWP_NOACTIVATE);
    applied_ = r;
}

dock::ScreenRect FloatWindow::CurrentRect() {
    RECT r{};
    if (Hwnd() && !IsIconic(Hwnd()) && GetWindowRect(Hwnd(), &r)) {
        applied_ = {r.left, r.top, r.right - r.left, r.bottom - r.top};
    }
    return applied_;
}

void FloatWindow::OnFrame(ui::Context& c, float width, float height) { editor_.DrawFloating(c, width, height, *this); }

void FloatWindow::OnCloseRequested() { editor_.CloseFloatingWindow(id_); }

void FloatWindow::OnOtherMessage(UINT msg, WPARAM, LPARAM) {
    // ウィンドウをタイトルバーでエディターの上に動かすと、そこにドロップのガイドが出る。
    // ガイドの上で離すと、このウィンドウのパネルがすべてドックに戻る。
    if (msg == WM_MOVING) editor_.FloatingMoving(id_, Hwnd());
    if (msg == WM_EXITSIZEMOVE) editor_.FloatingMoveEnded(id_);
}

}  // namespace rm::editor
