#pragma once
// 最上位の Win32 ウィンドウ: モニターごとの DPI (v2)、暗いタイトルバー、DIP での最小サイズ。
// メッセージは WindowListener に転送する。

#include "platform/Win.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace rm::platform {

class WindowListener {
public:
    virtual ~WindowListener() = default;
    // handled = true にすると DefWindowProc を飛ばして、その結果を返す。
    virtual LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& handled) = 0;
};

struct WindowDesc {
    std::wstring title = L"RM Engine";
    int widthDip = 1280;
    int heightDip = 720;
    int minWidthDip = 640;
    int minHeightDip = 400;
    bool resizable = true;
    uint32_t captionRgb = 0x151618;  // タイトルバーの色 (Windows 11)
    bool darkCaption = true;         // タイトルバーに明るい文字 (Windows 10 / 11)

    // エディターから取り出したパネル: エディターのウィンドウが持つ (その上に出る。タスクバーのボタンは無し)。
    HWND owner = nullptr;
    bool tool = false;
    // Windows の枠を一切付けない (形のあるウィンドウは自分で描き、ドラッグとサイズ変更の位置を
    // WM_NCHITTEST で伝える): 渡す大きさがウィンドウ全体。
    bool frameless = false;
    // 仮想スクリーンの物理ピクセルでのウィンドウの矩形。どのモニターにももう無いとき (外したモニターの
    // 保存位置) は無視する (中央に置く)。
    bool hasRect = false;
    int x = 0, y = 0, w = 0, h = 0;
};

class Window {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() { Destroy(); }

    // 隠した状態で作る。desc.hasRect でなければ、マウスカーソルのあるモニターの中央に置く。
    bool Create(const WindowDesc& desc, WindowListener* listener);
    // 先にメッセージの転送をやめ、それから HWND を壊す。
    void Destroy();

    HWND Handle() const { return hwnd_; }
    float Dpi() const { return static_cast<float>(dpi_); }
    float Scale() const { return static_cast<float>(dpi_) / 96.f; }
    void SetTitle(std::wstring_view title);
    // タイトルバーはエディターの色のテーマに従う。
    void SetCaption(bool dark, uint32_t rgb);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    WindowListener* listener_ = nullptr;
    UINT dpi_ = 96;
    WindowDesc desc_;
};

}  // namespace rm::platform
