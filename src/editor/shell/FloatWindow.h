#pragma once
// A panel window taken out of the editor window ("floating"). It is owned by the editor window
// (stays above it, has no taskbar button) and shows one docking tree of the editor's layout.
// Everything it shows and does goes through the EditorScreen: this class is only the OS window.

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

    // The rectangle last given to or read from the window (to notice a moved layout entry).
    const dock::ScreenRect& Applied() const { return applied_; }
    void MoveTo(const dock::ScreenRect& rect);
    dock::ScreenRect CurrentRect();  // also becomes Applied()

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
