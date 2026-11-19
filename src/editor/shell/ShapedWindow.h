#pragma once
// A software's own window (SoftwareHost::OpenToolWindow): no Windows frame, the shape the software
// gives (a region: the union of rounded rectangles, set again whenever its size changes), owned by the
// editor's window. Moved and sized where the software's hit test says (WM_NCHITTEST); drawn on input
// and by a timer every `refreshSeconds` - never invalidated frame after frame, which would keep the
// editor's own window from being drawn (Windows paints the windows that wait one after another).

#include "editor/app/ScreenWindow.h"
#include "editor/plugin/Software.h"

namespace rm::editor {

class ShapedWindow final : public ScreenWindow, public ToolWindow {
public:
    ShapedWindow(App& app, ToolWindowSpec spec) : ScreenWindow(app), spec_(std::move(spec)) {}
    ~ShapedWindow() override { ScreenWindow::Close(); }

    bool Open(HWND owner);

    // ToolWindow
    bool IsOpen() const override { return Hwnd() != nullptr && !closing_; }
    void Close() override;
    void Redraw() override { Invalidate(); }
    void BringToFront() override;

protected:
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override { Close(); }
    void OnTimer(UINT_PTR id) override;
    bool OnWindowMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) override;

private:
    void ApplyShape();

    ToolWindowSpec spec_;
    bool closing_ = false;
};

}  // namespace rm::editor
