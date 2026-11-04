#pragma once
// The editor window for one open project.
//
// Layout (design doc "body -> software -> panels"):
//   menu bar                      File / Edit / Window / Plugins / Help
//   software tabs                 one tab per "software" (game, illustration, music ...) that plugins add
//   panels of the selected software, docked (editor/dock) - they can also go out into their own windows
//   status bar
//
// Closing (title bar X, "Back to Hub", "Quit") always goes through RequestClose,
// which asks about unsaved changes - one road, never two.
//
// The panel layout is the user's view, not project data: it is not undoable, does not make the
// project unsaved, and is kept in <project>/UserSettings/Layout.json (one entry per software).

#include "core/UndoHistory.h"
#include "editor/app/ScreenWindow.h"
#include "editor/dock/DockSpace.h"
#include "editor/project/Project.h"
#include "editor/widgets/Widgets.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::editor {

class FloatWindow;

class EditorScreen final : public ScreenWindow {
public:
    EditorScreen(App& app, Project project);
    ~EditorScreen() override;
    bool Open();
    void Close() override;  // closes the floating panel windows first

    // --- Called by the floating panel windows ---
    void DrawFloating(ui::Context& c, float width, float height, FloatWindow& window);
    void CloseFloatingWindow(uint32_t floatingId);
    void FloatingMoving(uint32_t floatingId, HWND hwnd);
    void FloatingMoveEnded(uint32_t floatingId);

protected:
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;

private:
    enum class CloseAction { None, ToHub, Quit };
    enum class Modal { None, UnsavedChanges, Error, About };
    enum class Menu { None = -1, File, Edit, Window, Plugins, Help, Debug, Count };

    void RequestClose(CloseAction action);
    void FinishClose(CloseAction action);
    bool Save();
    void SetDirty(bool dirty);
    void UpdateTitle();
    void RefreshAssets();
    std::vector<w::MenuItem> MenuItems(Menu m) const;
    void RunMenu(Menu m, int index);
    void HandleShortcuts(ui::Context& c);
    void RedrawAll();

    // Panels and their layout
    void RegisterPanels();
    dock::Layout DefaultLayout() const;
    std::filesystem::path LayoutFile() const;
    void LoadLayout();
    void SaveLayout();
    void ResetLayout();
    void RequestSyncFloating();
    void SyncFloating(bool activate);
    void CloseFloats();

    void DrawMenuBar(ui::Context& c, ui::Rect r);
    void DrawSoftTabs(ui::Context& c, ui::Rect r);
    void DrawStatusBar(ui::Context& c, ui::Rect r);
    void DrawOpenMenu(ui::Context& c);
    void DrawModal(ui::Context& c);

    void DrawHierarchy(ui::Context& c, ui::Rect r);
    void DrawInspector(ui::Context& c, ui::Rect r);
    void DrawStart(ui::Context& c, ui::Rect r);
    void DrawConsole(ui::Context& c, ui::Rect r);
    void DrawAssets(ui::Context& c, ui::Rect r);

    Project project_;
    UndoHistory history_;
    bool dirty_ = false;

    Modal modal_ = Modal::None;
    CloseAction pendingClose_ = CloseAction::None;
    std::wstring modalText_;

    w::Popup menuPopup_;
    Menu openMenu_ = Menu::None;
    ui::Rect menuTitleRects_[static_cast<int>(Menu::Count)] = {};

    dock::DockSpace dock_;
    std::vector<std::unique_ptr<FloatWindow>> floats_;
    bool syncQueued_ = false;
    json::Value layoutDoc_;        // the whole Layout.json (other softwares and unknown keys are kept)
    bool layoutReadOnly_ = false;  // written by a newer engine: used but never overwritten

    struct AssetEntry {
        std::wstring name;
        bool folder = false;
    };
    std::vector<AssetEntry> assets_;

    int logSink_ = 0;
    // Last log entry each window's console has followed (a panel can move between windows).
    std::unordered_map<const ui::Context*, uint64_t> consoleSeenSeq_;
};

}  // namespace rm::editor
