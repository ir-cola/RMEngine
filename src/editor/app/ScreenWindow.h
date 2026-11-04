#pragma once
// One top-level window that shows one screen (the Hub or the editor).
// Collects input, runs the immediate-mode UI and presents - only when something changed.
//
// Frame flow:  input message -> Invalidate -> WM_PAINT -> RunFrame (OnFrame + draw + Present)
//              resize         -> RunFrame directly inside WM_SIZE (no frozen picture while dragging)
// Nothing runs while the user is idle (0% CPU), except the caret blink timer of a focused field.

#include "platform/Gfx.h"
#include "platform/Window.h"
#include "ui/Context.h"

#include <functional>
#include <string_view>
#include <vector>

namespace rm::editor {

class App;

class ScreenWindow : public platform::WindowListener {
public:
    explicit ScreenWindow(App& app) : app_(app) {}
    ~ScreenWindow() override;
    ScreenWindow(const ScreenWindow&) = delete;
    ScreenWindow& operator=(const ScreenWindow&) = delete;

    // Releases the swap chain and destroys the window. Safe to call twice.
    void Close();

    HWND Hwnd() const { return window_.Handle(); }
    void Invalidate();
    // Runs fn later from the message loop (outside drawing). Use it for anything that opens
    // a modal OS dialog: those run their own message loop and would re-enter the frame.
    void Defer(std::function<void()> fn);
    void SetTitle(std::wstring_view title) { window_.SetTitle(title); }

protected:
    bool CreateScreenWindow(const platform::WindowDesc& desc, int showCmd);

    virtual void OnCreated() {}
    virtual void OnFrame(ui::Context& c, float width, float height) = 0;
    virtual void OnCloseRequested() = 0;  // the title bar X / Alt+F4

    App& app_;
    platform::Window window_;
    platform::SwapTarget target_;
    ui::Painter painter_;
    ui::Context ctx_;
    ui::Input input_;

private:
    LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
    void RunFrame();
    void OnInput();
    void MouseButton(int button, LPARAM lp, bool down, bool doubleClick);
    void UpdateMouse(LPARAM lp);
    void UpdateModifiers();
    void UpdateIme();
    void RunDeferred();

    bool alive_ = false;
    bool inFrame_ = false;
    bool trackingMouse_ = false;
    bool imeOn_ = true;
    bool caretTimer_ = false;
    int chainedFrames_ = 0;
    ui::Cursor cursor_ = ui::Cursor::Arrow;
    std::vector<std::function<void()>> deferred_;
};

}  // namespace rm::editor
