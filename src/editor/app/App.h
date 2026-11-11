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

    // Applies the options (color theme, language) to every window. At start and after a change.
    void ApplyOptions();
    // Windows' light / dark setting changed (matters only for "PC の設定に合わせる").
    void SystemThemeChanged();

    // The GPU is lost (Gfx::Lost): its devices made again, every window drawn again (each makes its
    // own drawing again on the next frame). False when it cannot be now - the window asking tries
    // again in a moment; after a few failures the user is told and offered to save.
    bool RecoverGraphics(ScreenWindow& asking);

private:
    enum class Pending { None, OpenProject, InstallAndReopen, Hub, Quit };
    void ApplyPending();
    void OpenProjectNow();
    void ShowHub(const std::string& errorMessage);
    class HubScreen* EnsureHub();
    void Replace(std::unique_ptr<ScreenWindow> next);
    void LoadLanguageCatalogs();
    // The themes of the theme packs: themes/*.json of every enabled plugin (+ its pictures, offered by
    // its code at w::kOrnamentPoint). After each reading of the plugins. A theme chosen whose pack is
    // gone is replaced by the first theme in the options (for good).
    void LoadThemes();
    // The plugins read again (after installing / removing), then their themes, then the look.
    void ReloadPlugins();

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
    int graphicsFailures_ = 0;  // Recreate failed this many times in a row
    bool graphicsTold_ = false; // ... and the user was told
};

}  // namespace rm::editor
