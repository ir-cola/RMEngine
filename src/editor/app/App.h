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

    void RequestOpenProject(const std::filesystem::path& file);
    void RequestHub(std::string errorMessage = {});
    void RequestQuit();

private:
    enum class Pending { None, OpenProject, Hub, Quit };
    void ApplyPending();
    void ShowHub(const std::string& errorMessage);
    void Replace(std::unique_ptr<ScreenWindow> next);

    platform::Gfx gfx_;
    HubSettings settings_;
    std::unique_ptr<ScreenWindow> screen_;
    Pending pending_ = Pending::None;
    std::filesystem::path pendingFile_;
    std::string pendingMessage_;
    bool running_ = true;
};

}  // namespace rm::editor
