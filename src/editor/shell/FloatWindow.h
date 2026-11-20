#pragma once
// エディターのウィンドウから取り出したパネルのウィンドウ ("フローティング")。エディターの
// ウィンドウが持ち (その上に出て、タスクバーのボタンはない)、エディターのレイアウトのドッキングツリーを 1 つ出す。
// これが出すものも、することも、すべて EditorScreen を通る: このクラスは OS のウィンドウでしかない。

#include "editor/app/ScreenWindow.h"
#include "editor/dock/DockLayout.h"

#include <cstdint>
#include <string>

namespace rm::editor {

class EditorScreen;

class FloatWindow final : public ScreenWindow {
public:
    FloatWindow(App& app, EditorScreen& editor, uint32_t floatingId)
        : ScreenWindow(app), editor_(editor), id_(floatingId) {}

    bool Open(const dock::ScreenRect& rect, HWND owner, const std::wstring& title, bool activate);
    uint32_t Id() const { return id_; }

    // 最後にウィンドウへ渡した、またはウィンドウから読んだ矩形 (レイアウトの項目が動いたのに気づくため)。
    const dock::ScreenRect& Applied() const { return applied_; }
    void MoveTo(const dock::ScreenRect& rect);
    dock::ScreenRect CurrentRect();  // Applied() にもなる

protected:
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;
    void OnOtherMessage(UINT msg, WPARAM wp, LPARAM lp) override;

private:
    EditorScreen& editor_;
    uint32_t id_;
    dock::ScreenRect applied_;
};

}  // namespace rm::editor
