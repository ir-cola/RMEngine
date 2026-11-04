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
};

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() { Destroy(); }

    // Created hidden and centered on the monitor under the mouse cursor.
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
