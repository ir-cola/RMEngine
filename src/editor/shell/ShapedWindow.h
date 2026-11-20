#pragma once
// ソフト自身のウィンドウ (SoftwareHost::OpenToolWindow): Windows のフレームはなく、形はソフトが
// 決める (領域: 角丸の四角の和集合。サイズが変わるたびに設定し直す)。エディターのウィンドウが持つ。
// ソフトのヒットテストが言う場所で動かし、サイズを変える (WM_NCHITTEST)。入力と `refreshSeconds` ごとの
// タイマーで描く - フレームごとに無効化はしない。そうするとエディター自身のウィンドウが描かれなくなる
// (Windows は待っているウィンドウを 1 つずつ描くため)。

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
