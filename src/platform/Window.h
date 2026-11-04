#pragma once
// A top-level Win32 window: per-monitor DPI (v2), dark title bar, minimum size in DIPs.
// Messages are forwarded to a WindowListener.

#include "platform/Win.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace rm::platform {

class WindowListener {
public:
    virtual ~WindowListener() = default;
    // Set handled = true to skip DefWindowProc and return the result.
    virtual LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& handled) = 0;
};

struct WindowDesc {
    std::wstring title = L"RM Engine";
    int widthDip = 1280;
    int heightDip = 720;
    int minWidthDip = 640;
    int minHeightDip = 400;
    bool resizable = true;
    uint32_t captionRgb = 0x151618;  // title bar color (Windows 11)

    // Panels taken out of the editor: owned by the editor window (stays above it, no taskbar button).
    HWND owner = nullptr;
    bool tool = false;
    // Window rectangle in physical pixels of the virtual screen. Ignored (centered instead)
    // when it is not on any monitor any more (a saved position from an unplugged screen).
    bool hasRect = false;
    int x = 0, y = 0, w = 0, h = 0;
};

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() { Destroy(); }

    // Created hidden; centered on the monitor under the mouse cursor unless desc.hasRect.
    bool Create(const WindowDesc& desc, WindowListener* listener);
    // Stops forwarding messages first, then destroys the HWND.
    void Destroy();

    HWND Handle() const { return hwnd_; }
    float Dpi() const { return static_cast<float>(dpi_); }
    float Scale() const { return static_cast<float>(dpi_) / 96.f; }
    void SetTitle(std::wstring_view title);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    WindowListener* listener_ = nullptr;
    UINT dpi_ = 96;
    WindowDesc desc_;
};

}  // namespace rm::platform
