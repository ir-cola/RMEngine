#pragma once
// The editor window for one open project.
//
// Layout (design doc "body -> software -> panels"):
//   menu bar                      File / Edit / Plugins / Help
//   software tabs                 one tab per "software" (game, illustration, music ...) that plugins add
//   panels of the selected software
//   status bar
//
// Closing (title bar X, "Back to Hub", "Quit") always goes through RequestClose,
// which asks about unsaved changes - one road, never two.

#include "core/UndoHistory.h"
#include "editor/app/ScreenWindow.h"
#include "editor/project/Project.h"
#include "editor/widgets/Widgets.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rm::editor {

class EditorScreen final : public ScreenWindow {
public:
    EditorScreen(App& app, Project project);
    ~EditorScreen() override;
    bool Open();

protected:
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;

private:
    enum class CloseAction { None, ToHub, Quit };
    enum class Modal { None, UnsavedChanges, Error, About };
    enum class Menu { None = -1, File, Edit, Plugins, Help, Debug, Count };

    void RequestClose(CloseAction action);
    void FinishClose(CloseAction action);
    bool Save();
    void SetDirty(bool dirty);
    void UpdateTitle();
    void RefreshAssets();
    std::vector<w::MenuItem> MenuItems(Menu m) const;
    void RunMenu(Menu m, int index);

    void DrawMenuBar(ui::Context& c, ui::Rect r);
    void DrawSoftTabs(ui::Context& c, ui::Rect r);
    void DrawWorkspace(ui::Context& c, ui::Rect r);
    void DrawBottomPanel(ui::Context& c, ui::Rect r);
    void DrawConsole(ui::Context& c, ui::Rect r);
    void DrawAssets(ui::Context& c, ui::Rect r);
    void DrawStatusBar(ui::Context& c, ui::Rect r);
    void DrawOpenMenu(ui::Context& c);
    void DrawModal(ui::Context& c);

    Project project_;
    UndoHistory history_;
    bool dirty_ = false;

    Modal modal_ = Modal::None;
    CloseAction pendingClose_ = CloseAction::None;
    std::wstring modalText_;

    w::Popup menuPopup_;
    Menu openMenu_ = Menu::None;
    ui::Rect menuTitleRects_[static_cast<int>(Menu::Count)] = {};

    int bottomTab_ = 0;  // 0 console, 1 assets
    struct AssetEntry {
        std::wstring name;
        bool folder = false;
    };
    std::vector<AssetEntry> assets_;

    int logSink_ = 0;
    uint64_t consoleSeenSeq_ = 0;
};

}  // namespace rm::editor
