#pragma once
// The Hub: pick, create and open projects, and manage the plugins (install / reinstall / remove).
// Plugins are changed only here or from the editor's plugin menu (install only) - removing while a
// project is open can never happen (design RME_PLATFORM 5-a).

#include "core/PluginId.h"
#include "editor/app/ScreenWindow.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Widgets.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rm::editor {

class HubScreen final : public ScreenWindow {
public:
    explicit HubScreen(App& app) : ScreenWindow(app) {}
    bool Open();
    void OnLookChanged() override;  // texts made earlier (the list) are made again in the new language
    void ShowError(std::wstring message, std::filesystem::path removableFile = {}, std::wstring title = Tr(L"開けませんでした"));
    // Asked by the App before opening a project.
    //   blocking: the project uses plugins that cannot be used -> "起動できません。前提プラグインをインストールしますか？"
    //   limited:  installed plugins lack a requirement -> told once, [インストール] [このまま開く]
    void AskBlockingPlugins(const std::filesystem::path& project, const std::vector<PluginId>& blocking);
    void AskLimitedPlugins(const std::filesystem::path& project, const std::vector<PluginId>& limited);

protected:
    void OnCreated() override;
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;

private:
    enum class Page { Projects, Plugins, Updates, Options };
    enum class Modal { None, NewProject, Rename, Error, Plugin, Integrity, Theme };
    enum class PromptKind { Blocking, Limited, Install, Remove };

    // One plugin question at a time (design: pop-ups one by one).
    struct Prompt {
        PromptKind kind = PromptKind::Install;
        std::wstring title;
        std::wstring body;
        std::filesystem::path project;    // Blocking / Limited: opened afterwards
        PluginId target;                  // Remove
        std::vector<PluginId> toInstall;  // what the install button copies
        std::vector<PluginId> limited;    // Limited: the set "do not show again" remembers
        bool hideAgain = false;
    };

    struct Row {
        std::filesystem::path file;
        std::wstring name;
        std::wstring folder;
        std::wstring modifiedText;
        int64_t modifiedSort = 0;
        bool missing = false;
    };

    void Refresh();
    void SortRows();
    std::vector<size_t> FilteredRows() const;

    void DrawSidebar(ui::Context& c, ui::Rect r);
    void DrawProjects(ui::Context& c, ui::Rect r);
    void DrawPlugins(ui::Context& c, ui::Rect r);
    // Below the plugins, in the same scrolled rows (`area` moves down past them).
    void DrawConnectors(ui::Context& c, ui::Rect& area, const ui::Rect& view);
    void DrawOptions(ui::Context& c, ui::Rect r);
    void DrawUpdates(ui::Context& c, ui::Rect r);
    void DrawIntegrity(ui::Context& c);
    void DrawThemePicker(ui::Context& c);
    void RunIntegrityCheck();
    void DrawNewProject(ui::Context& c);
    void DrawRename(ui::Context& c);
    void DrawError(ui::Context& c);
    void DrawRowMenu(ui::Context& c);
    void DrawPluginPrompt(ui::Context& c);

    void BeginInstall(const PluginId& id);
    void BeginRemove(const PluginId& id);
    void RunInstall(const std::vector<PluginId>& ids);
    std::wstring PluginLine(const PluginId& id) const;
    std::wstring PluginNames(const std::vector<PluginId>& ids) const;
    int RecentProjectsUsing(const PluginId& id) const;
    void ShowPrompt(Prompt prompt);

    void OpenRow(const Row& row);
    void BrowseOpen();
    void BeginNewProject();
    void BrowseLocation();
    void CreateNewProject();
    // Rename (folder + .rmpf + name inside). Only from the Hub: no project is open while it shows.
    void BeginRename(const Row& row);
    void RunRename();

    Page page_ = Page::Projects;
    Modal modal_ = Modal::None;
    std::vector<Row> rows_;
    int sortColumn_ = 1;  // 0 name, 1 modified, 2 location
    bool sortAscending_ = false;
    std::filesystem::path selected_;
    std::string search_;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> logo_;

    w::Popup rowMenu_;
    std::filesystem::path rowMenuFile_;

    // New project dialog
    std::string newName_;
    std::string newLocation_;
    size_t newTemplate_ = 0;
    std::wstring newError_;
    bool focusNewName_ = false;

    // Rename dialog
    std::filesystem::path renameFile_;
    std::wstring renameOld_;
    std::string renameName_;
    std::wstring renameError_;
    bool focusRename_ = false;

    Prompt prompt_;

    // Options page (the options are drawn by the inspector from their type)
    inspector::State optionsInspector_;
    // Integrity check result
    std::wstring integrityText_;
    std::vector<PluginId> integrityStale_;  // installed plugins that differ from the engine's copies

    // Error dialog
    std::wstring errorTitle_;
    std::wstring errorText_;
    std::filesystem::path errorRemovable_;
};

}  // namespace rm::editor
