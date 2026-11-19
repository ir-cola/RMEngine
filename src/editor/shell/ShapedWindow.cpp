#include "editor/shell/ShapedWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace rm::editor {

namespace {
constexpr UINT_PTR kRefreshTimer = 100;
}  // namespace

bool ShapedWindow::Open(HWND owner) {
    platform::WindowDesc d;
    d.title = spec_.title;
    d.widthDip = spec_.widthDip;
    d.heightDip = spec_.heightDip;
    d.minWidthDip = spec_.minWidthDip;
    d.minHeightDip = spec_.minHeightDip;
    d.owner = owner;
    d.tool = true;
    d.frameless = true;
    if (!CreateScreenWindow(d, SW_SHOW)) return false;
    ApplyShape();
    if (spec_.refreshSeconds > 0) SetTimer(Hwnd(), kRefreshTimer, static_cast<UINT>(std::max(10.0, spec_.refreshSeconds * 1000.0)), nullptr);
    return true;
}

void ShapedWindow::Close() {
    if (closing_ || !Hwnd()) return;
    closing_ = true;
    // (from its own frame - the X button - the window goes after the frame, never under it)
    Defer([this] {
        KillTimer(Hwnd(), kRefreshTimer);
        ScreenWindow::Close();
    });
}

void ShapedWindow::BringToFront() {
    if (!Hwnd()) return;
    if (IsIconic(Hwnd())) ShowWindow(Hwnd(), SW_RESTORE);
    SetForegroundWindow(Hwnd());
}

void ShapedWindow::OnFrame(ui::Context& c, float width, float height) {
    if (spec_.draw) spec_.draw(c, width, height);
}

void ShapedWindow::OnTimer(UINT_PTR id) {
    if (id == kRefreshTimer) Invalidate();
}

void ShapedWindow::ApplyShape() {
    HWND h = Hwnd();
    if (!h || !spec_.shape) return;
    RECT rc{};
    GetClientRect(h, &rc);
    const float s = Scale();
    const std::vector<ToolRoundRect> parts = spec_.shape(static_cast<float>(rc.right) / s, static_cast<float>(rc.bottom) / s);
    if (parts.empty()) {
        SetWindowRgn(h, nullptr, TRUE);
        return;
    }
    HRGN all = CreateRectRgn(0, 0, 0, 0);
    for (const ToolRoundRect& p : parts) {
        const int l = static_cast<int>(std::lround(p.rect.x * s)), t = static_cast<int>(std::lround(p.rect.y * s));
        const int r = static_cast<int>(std::lround(p.rect.Right() * s)), b = static_cast<int>(std::lround(p.rect.Bottom() * s));
        const int d = static_cast<int>(std::lround(p.radius * 2 * s));
        HRGN one = d > 0 ? CreateRoundRectRgn(l, t, r + 1, b + 1, d, d) : CreateRectRgn(l, t, r, b);
        CombineRgn(all, all, one, RGN_OR);
        DeleteObject(one);
    }
    if (!SetWindowRgn(h, all, TRUE)) DeleteObject(all);  // (taken: the window owns it; not: still ours)
}

bool ShapedWindow::OnWindowMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    switch (msg) {
        case WM_NCCALCSIZE:
            // No frame: the whole window is the client area.
            if (wp) {
                result = 0;
                return true;
            }
            return false;
        case WM_NCHITTEST: {
            if (!spec_.hit) return false;
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(Hwnd(), &p);
            RECT rc{};
            GetClientRect(Hwnd(), &rc);
            const float s = Scale();
            const ToolHit hit = spec_.hit(static_cast<float>(p.x) / s, static_cast<float>(p.y) / s, static_cast<float>(rc.right) / s,
                                          static_cast<float>(rc.bottom) / s);
            switch (hit) {
                case ToolHit::Caption: result = HTCAPTION; break;
                case ToolHit::Left: result = HTLEFT; break;
                case ToolHit::Right: result = HTRIGHT; break;
                case ToolHit::Top: result = HTTOP; break;
                case ToolHit::Bottom: result = HTBOTTOM; break;
                case ToolHit::TopLeft: result = HTTOPLEFT; break;
                case ToolHit::TopRight: result = HTTOPRIGHT; break;
                case ToolHit::BottomLeft: result = HTBOTTOMLEFT; break;
                case ToolHit::BottomRight: result = HTBOTTOMRIGHT; break;
                case ToolHit::Nowhere: result = HTTRANSPARENT; break;
                default: result = HTCLIENT; break;
            }
            return true;
        }
        case WM_NCACTIVATE:
            // (nothing of a frame to draw as active or not)
            result = TRUE;
            return true;
        case WM_SIZE:
            ApplyShape();
            return false;  // (the screen window resizes its picture too)
        default:
            return false;
    }
}

}  // namespace rm::editor
