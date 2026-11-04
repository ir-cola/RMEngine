#pragma once
// The Hub: pick, create and open projects. (Plugin management will live here too.)

#include "editor/app/ScreenWindow.h"
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
    void ShowError(std::wstring message, std::filesystem::path removableFile = {});

protected:
    void OnCreated() override;
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;

private:
    enum class Page { Projects, Plugins };
    enum class Modal { None, NewProject, Error };

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
    void DrawNewProject(ui::Context& c);
    void DrawError(ui::Context& c);
    void DrawRowMenu(ui::Context& c);

    void OpenRow(const Row& row);
    void BrowseOpen();
    void BeginNewProject();
    void BrowseLocation();
    void CreateNewProject();

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

    // Error dialog
    std::wstring errorText_;
    std::filesystem::path errorRemovable_;
};

}  // namespace rm::editor
