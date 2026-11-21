#include "editor/help/HelpWindow.h"

#include "editor/app/App.h"
#include "editor/help/HelpBooks.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"

namespace rm::editor {

HelpWindow::HelpWindow(App& app) : ScreenWindow(app) {
    view_.SetGfx(&app_.Gfx());
    view_.SetLibrary(LoadHelpLibrary(app_.GetPlugins()), lang::Current() == lang::Language::English);
}

bool HelpWindow::Show(HWND owner) {
    if (Hwnd()) {
        if (IsIconic(Hwnd())) ShowWindow(Hwnd(), SW_RESTORE);
        SetForegroundWindow(Hwnd());
        return true;
    }
    platform::WindowDesc d;
    d.title = Tr(L"ヘルプ") + L" - RM Engine";
    d.widthDip = 1000;
    d.heightDip = 680;
    d.minWidthDip = 360;
    d.minHeightDip = 300;
    d.owner = owner;
    d.captionRgb = theme::CaptionRgb;
    d.darkCaption = !theme::IsLight;
    return CreateScreenWindow(d, SW_SHOW);
}

void HelpWindow::OnLookChanged() {
    view_.SetLibrary(LoadHelpLibrary(app_.GetPlugins()), lang::Current() == lang::Language::English);
    SetTitle(Tr(L"ヘルプ") + L" - RM Engine");
    ScreenWindow::OnLookChanged();
}

void HelpWindow::OnFrame(ui::Context& c, float width, float height) {
    c.P().Clear(theme::Panel);
    w::DrawBackdrop(c, true);
    view_.Draw(c, {0, 0, width, height});
}

}  // namespace rm::editor
