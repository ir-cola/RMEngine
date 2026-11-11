#pragma once
// The editor window for one open project.
//
// Layout (design doc "body -> software -> panels"):
//   menu bar                      File / Edit / Window / Plugins / Help
//   software tabs                 one tab per "software" (game, illustration, music ...) that plugins add
//   panels of the selected software, docked (editor/dock) - they can also go out into their own windows
//   status bar
//
// Each software is a Workspace: its own panels, layout, floating windows and undo history
// (Ctrl+Z in one software never undoes another's edit). The project has one "unsaved" mark:
// an edit pushed to any workspace's history sets it. With no software installed, a single
// workspace shows the core panels alone.
//
// Closing (title bar X, "Back to Hub", "Quit") always goes through RequestClose,
// which asks about unsaved changes - one road, never two.
//
// The panel layout is the user's view, not project data: it is not undoable, does not make the
// project unsaved, and is kept in <project>/UserSettings/Layout.json (one entry per software,
// plus which software was shown last). Layouts saved under a name are kept there too, per software
// (Window menu: save / switch / manage - EditorLayouts.cpp).

#include "core/UndoHistory.h"
#include "editor/app/ScreenWindow.h"
#include "editor/assets/AssetLibrary.h"
#include "editor/assets/AssetPanel.h"
#include "editor/dock/DockSpace.h"
#include "editor/dock/NamedLayouts.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/plugin/Software.h"
#include "editor/project/Project.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"

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
    // The project's lock (ProjectLockFile), held while this editor is open.
    void AdoptLock(std::unique_ptr<platform::FileLock> lock) { lock_ = std::move(lock); }
    void Close() override;  // closes the floating panel windows first
    void ShowError(std::wstring message);
    void OnLookChanged() override;  // the panel windows too

    // --- Called by the floating panel windows ---
    void DrawFloating(ui::Context& c, float width, float height, FloatWindow& window);
    // A thing dragged out of a panel (an asset, an item of the 階層) is over another window of this
    // editor: that window is told where, so a reference field there takes it (w::SetDragElsewhere).
    void ForwardDrag(ui::Context& c, HWND self, float width, float height);
    void CloseFloatingWindow(uint32_t floatingId);
    void FloatingMoving(uint32_t floatingId, HWND hwnd);
    void FloatingMoveEnded(uint32_t floatingId);

protected:
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;
    void OnTimer(UINT_PTR id) override;  // auto-save

private:
    static constexpr UINT_PTR kAutoSaveTimer = 100;
    enum class CloseAction { None, ToHub, Quit };
    enum class Modal { None, UnsavedChanges, Error, About, InstallPlugin, SaveLayoutAs, Layouts };
    enum class Menu { None = -1, File, Edit, Window, Plugins, Help, Debug, Count };

    void RequestClose(CloseAction action);
    void FinishClose(CloseAction action);
    bool Save();
    void SetDirty(bool dirty);
    void UpdateTitle();
    // What reference fields of the インスペクター show (assets, the 階層's items of the software shown).
    inspector::Refs MakeRefs();
    std::vector<w::MenuItem> MenuItems(Menu m) const;
    void RunMenu(Menu m, int index);
    void HandleShortcuts(ui::Context& c);
    void UndoRedo(bool redo);  // the shown software's history (not while it runs)
    // Plugin menu: installing from the editor saves, installs and opens the same project again
    // (design RME_PLATFORM 5-a, decided 2026-09-29). Removing is only possible in the Hub.
    void BeginInstallPlugin(const PluginId& id);
    void RedrawAll();

    // One software inside this project (or, with no software installed, the core panels alone).
    class Workspace final : public SoftwareHost {
    public:
        explicit Workspace(EditorScreen& editor) : editor_(editor) {}
        UndoHistory& History() override { return history; }
        const Project& GetProject() const override { return editor_.project_; }
        void Redraw() override { editor_.RedrawAll(); }
        void Inspect(Inspectable target) override {
            inspected = std::move(target);
            inspectorState = {};
            editor_.RedrawAll();
        }
        const PluginRegistry& Registry() const override;
        platform::Gfx& Graphics() override;
        bool SetClipboard(const std::string& textUtf8) override;
        std::string Clipboard() override;
        std::filesystem::path AssetFile(const AssetRef& ref) const override;
        AssetRef AssetOf(const std::filesystem::path& file) override;
        void SelectInTree(std::vector<tree::ItemId> ids, bool rename) override {
            tree::RequestSelect(treeState, std::move(ids), rename);
            editor_.RedrawAll();
        }
        dock::Layout DefaultLayout() const;
        std::wstring Title() const { return Tr(type ? type->title : TrKey(L"コア")); }  // in the language in use

        const SoftwareType* type = nullptr;  // nullptr: no software (core panels + "start")
        UndoHistory history;                 // declared before `software`, so it outlives it
        dock::DockSpace dock;
        std::unique_ptr<Software> software;
        Inspectable inspected;  // what the インスペクター shows
        inspector::State inspectorState;
        tree::State treeState;  // the 階層: selection, open items, search
        dock::NamedLayouts named;  // layouts the user saved under a name (UserSettings)
        dock::NamedLayouts shared;  // named layouts every project can use (%APPDATA%\RM_Engine\Layouts.json)
        std::string namedShown;    // the named layout switched to / saved last (checked in the menu)
        bool namedShownShared = false;  // ... and which list it is in
        dock::NamedLayouts& Named(bool sharedList) { return sharedList ? shared : named; }
        const dock::NamedLayouts& Named(bool sharedList) const { return sharedList ? shared : named; }
        std::unique_ptr<AssetPanel> assetPanel;  // the アセット panel (made when first drawn)
        float assetThumbnail = 40.f;             // its thumbnails' size (UserSettings: "assets": {"thumbnail"})

    private:
        EditorScreen& editor_;
    };

    // Workspaces, panels and their layout
    void BuildWorkspaces();
    // .rmpf "softwareData": each software's own part of the project (read once, written on save).
    void LoadSoftwareData();
    void StoreSoftwareData();
    std::vector<dock::PanelType> CorePanels(bool withStart);
    Workspace& Active() { return *workspaces_[active_]; }
    const Workspace& Active() const { return *workspaces_[active_]; }
    void SwitchSoftware(size_t index);
    std::filesystem::path LayoutFile() const;
    void LoadLayout();
    void SaveLayout();
    void ResetLayout();
    void CaptureFloatingRects();
    void LoadSharedLayouts();
    void SaveSharedLayouts();  // where the user put the panel windows, into the layout
    // Named layouts (EditorLayouts.cpp)
    void BeginSaveLayoutAs();
    bool SaveNamedLayout(const std::string& name, bool shared);
    void ApplyNamedLayout(const std::string& name, bool shared);
    void SaveNamedList(bool shared);  // writes the file the list lives in
    void DrawSaveLayoutAs(ui::Context& c);
    void DrawLayouts(ui::Context& c);
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
    std::unique_ptr<platform::FileLock> lock_;  // let go in Close() (and when destroyed)
    bool dirty_ = false;
    double dirtySince_ = 0;  // platform::NowSeconds() of the first unsaved change

    Modal modal_ = Modal::None;
    CloseAction pendingClose_ = CloseAction::None;
    std::wstring modalText_;
    std::wstring installName_;
    std::vector<PluginId> installPlan_;
    // the named layout dialogs
    std::string layoutName_;      // typed in "save as"
    bool layoutShared_ = false;   // "save as" into the shared list
    bool focusLayoutName_ = false;
    std::string renamingLayout_;  // the row being renamed in the list ("" = none; a row key: list + name)
    std::string renameText_;
    bool focusRename_ = false;
    std::string confirmDelete_;   // the row whose 削除 was pressed once

    w::Popup menuPopup_;
    Menu openMenu_ = Menu::None;
    ui::Rect menuTitleRects_[static_cast<int>(Menu::Count)] = {};

    // Declared BEFORE the workspaces, so they are destroyed AFTER them (members go in reverse order):
    // every workspace's AssetPanel, its ImportCommands in the undo history and Workspace::AssetFile
    // point at them while the workspaces are being torn down.
    std::unique_ptr<AssetLibrary> assets_;    // the project's Assets/ (ids, file operations) - every software's
    std::unique_ptr<Thumbnails> thumbnails_;  // the assets' small pictures - every software's panel shares them (its thread ends here)

    std::vector<std::unique_ptr<Workspace>> workspaces_;  // never empty
    size_t active_ = 0;
    std::vector<std::unique_ptr<FloatWindow>> floats_;  // of the active workspace only
    bool syncQueued_ = false;
    json::Value layoutDoc_;        // the whole Layout.json (other softwares and unknown keys are kept)
    bool layoutReadOnly_ = false;  // written by a newer engine: used but never overwritten
    json::Value sharedDoc_;        // the whole shared Layouts.json (other softwares and unknown keys are kept)
    bool sharedReadOnly_ = false;

    HWND dragTarget_ = nullptr;  // the window last told about a drag over it (ForwardDrag)

    int logSink_ = 0;
    // Last log entry each window's console has followed (a panel can move between windows).
    std::unordered_map<const ui::Context*, uint64_t> consoleSeenSeq_;
};

}  // namespace rm::editor
