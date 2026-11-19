#include "platform/Window.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>

#include "resource.h"

namespace rm::platform {

namespace {

constexpr wchar_t kClassName[] = L"RMEngineWindow";

void RegisterClassOnce(WNDPROC proc) {
    static bool done = false;
    if (done) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x1B, 0x1C, 0x1F));
    wc.lpszClassName = kClassName;
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);
    done = true;
}

DWORD StyleFor(const WindowDesc& d) {
    if (d.frameless) return WS_POPUP | WS_THICKFRAME;  // (THICKFRAME: sizing by its edges; the frame itself is taken away: WM_NCCALCSIZE)
    if (d.tool) return WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX;
    return d.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
}

DWORD ExStyleFor(const WindowDesc& d) { return d.tool ? WS_EX_TOOLWINDOW : 0; }

}  // namespace

bool Window::Create(const WindowDesc& desc, WindowListener* listener) {
    desc_ = desc;
    listener_ = listener;
    RegisterClassOnce(&Window::WndProc);

    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);

    const DWORD style = StyleFor(desc);
    const DWORD exStyle = ExStyleFor(desc);
    const float s = dpiX / 96.f;
    RECT r{0, 0, static_cast<LONG>(desc.widthDip * s), static_cast<LONG>(desc.heightDip * s)};
    if (!desc.frameless) AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpiX);
    const RECT& wa = mi.rcWork;
    int w = std::min<int>(r.right - r.left, wa.right - wa.left);
    int h = std::min<int>(r.bottom - r.top, wa.bottom - wa.top);
    int x = wa.left + (wa.right - wa.left - w) / 2;
    int y = wa.top + (wa.bottom - wa.top - h) / 2;
    if (desc.hasRect) {
        const RECT want{desc.x, desc.y, desc.x + desc.w, desc.y + desc.h};
        if (HMONITOR on = MonitorFromRect(&want, MONITOR_DEFAULTTONULL)) {
            // Kept inside the work area of the monitor it is on (not under the taskbar, not off screen).
            MONITORINFO om{sizeof(om)};
            GetMonitorInfoW(on, &om);
            const RECT& owa = om.rcWork;
            w = std::min(desc.w, static_cast<int>(owa.right - owa.left));
            h = std::min(desc.h, static_cast<int>(owa.bottom - owa.top));
            x = std::clamp(desc.x, static_cast<int>(owa.left), static_cast<int>(owa.right) - w);
            y = std::clamp(desc.y, static_cast<int>(owa.top), static_cast<int>(owa.bottom) - h);
        } else {
            w = std::min(desc.w, static_cast<int>(wa.right - wa.left));
            h = std::min(desc.h, static_cast<int>(wa.bottom - wa.top));
            x = wa.left + (wa.right - wa.left - w) / 2;
            y = wa.top + (wa.bottom - wa.top - h) / 2;
        }
    }

    hwnd_ = CreateWindowExW(exStyle, kClassName, desc.title.c_str(), style, x, y, w, h, desc.owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    dpi_ = GetDpiForWindow(hwnd_);

    SetCaption(desc.darkCaption, desc.captionRgb);
    return true;
}

void Window::SetCaption(bool dark, uint32_t rgb) {
    if (!hwnd_) return;
    const BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &d, sizeof(d));
    const COLORREF caption = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    DwmSetWindowAttribute(hwnd_, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
}

void Window::Destroy() {
    listener_ = nullptr;
    if (hwnd_) {
        HWND h = hwnd_;
        hwnd_ = nullptr;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        DestroyWindow(h);
    }
}

void Window::SetTitle(std::wstring_view title) {
    if (hwnd_) SetWindowTextW(hwnd_, std::wstring(title).c_str());
}

LRESULT CALLBACK Window::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        // CreateWindowExW is still running: remember the object for every later message.
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        auto* created = static_cast<Window*>(cs->lpCreateParams);
        created->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        return DefWindowProcW(h, m, w, l);
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, m, w, l);  // before WM_NCCREATE or after Destroy()

    switch (m) {
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(l);
            const UINT dpi = GetDpiForWindow(h);
            const float s = dpi / 96.f;
            RECT r{0, 0, static_cast<LONG>(self->desc_.minWidthDip * s), static_cast<LONG>(self->desc_.minHeightDip * s)};
            AdjustWindowRectExForDpi(&r, StyleFor(self->desc_), FALSE, ExStyleFor(self->desc_), dpi);
            mmi->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
            return 0;
        }
        case WM_DPICHANGED: {
            self->dpi_ = HIWORD(w);
            if (self->listener_) {
                bool handled = false;
                self->listener_->OnMessage(h, m, w, l, handled);
            }
            const RECT* rc = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;  // everything is drawn by Direct2D
        default:
            break;
    }

    if (self->listener_) {
        bool handled = false;
        const LRESULT r = self->listener_->OnMessage(h, m, w, l, handled);
        if (handled) return r;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace rm::platform
