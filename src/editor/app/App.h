#pragma once
// The editor application: owns the GPU device and switches between the two screens.
//
//   Hub  --(open / create project)-->  Editor
//   Hub  <--(close editor window)----  Editor
//
// Hub and editor live in the same exe (the shipped game is a different exe and contains
// neither). Screen changes are requested and applied by the message loop afterwards
// (never from inside a window procedure), so a window is never destroyed while one of
// its own handlers is still running.

#include "editor/plugin/Plugins.h"
#include "editor/project/HubSettings.h"
#include "platform/Gfx.h"

#include <filesystem>
#include <memory>
#include <string>

namespace rm::editor {

class ScreenWindow;

struct LaunchOptions {
    std::filesystem::path projectFile;  // --project <file.rmpf>: skip the Hub (used to restart into a project)
};

class App {
public:
    App();
    ~App();

    int Run(const LaunchOptions& options);

    platform::Gfx& Gfx() { return gfx_; }
    HubSettings& Settings() { return settings_; }
    const Plugins& GetPlugins() const { return plugins_; }

    // Before opening, the plugins the project records are checked (it is not opened without them),
    // and the Hub tells once about installed plugins that lack a requirement - unless skipNotice.
    void RequestOpenProject(const std::filesystem::path& file, bool skipNotice = false);
    // The editor's plugin menu (the editor has saved): install, then open the same project again.
    void RequestInstallAndReopen(std::vector<PluginId> ids, const std::filesystem::path& project);
    // Hub only (no editor may be open): change the installed plugins and read them again.
    bool InstallPlugins(const std::vector<PluginId>& ids, std::string* error);
    bool UninstallPlugin(const PluginId& id, std::string* error);
    void RequestHub(std::string errorMessage = {});
    void RequestQuit();

private:
    enum class Pending { None, OpenProject, InstallAndReopen, Hub, Quit };
    void ApplyPending();
    void OpenProjectNow();
    void ShowHub(const std::string& errorMessage);
    class HubScreen* EnsureHub();
    void Replace(std::unique_ptr<ScreenWindow> next);

    platform::Gfx gfx_;
    HubSettings settings_;
    Plugins plugins_;
    std::unique_ptr<ScreenWindow> screen_;
    Pending pending_ = Pending::None;
    std::filesystem::path pendingFile_;
    bool pendingSkipNotice_ = false;
    std::vector<PluginId> pendingInstall_;
    std::string pendingMessage_;
    bool running_ = true;
};

}  // namespace rm::editor
