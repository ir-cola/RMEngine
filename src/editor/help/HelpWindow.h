#pragma once
// Hub から開くヘルプの窓 (Hub にはドッキングが無い: 作業内容.md 2026-11-21 23:23 の決定 1)。中身は
// エディターのヘルプのパネルと同じ HelpView で描く (憲法 3)。Hub が 1 つだけ持つ (開いていれば前に出す)。

#include "editor/app/ScreenWindow.h"
#include "editor/help/HelpView.h"

namespace rm::editor {

class HelpWindow final : public ScreenWindow {
public:
    explicit HelpWindow(App& app);
    // 窓を開く (もう開いていれば前に出す)。`owner`: Hub の窓 (その上に出る)。
    bool Show(HWND owner);
    bool IsOpen() const { return Hwnd() != nullptr; }
    void OnLookChanged() override;  // 言語が変わった: 本を読み直す

protected:
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override { Close(); }

private:
    HelpView view_;
};

}  // namespace rm::editor
