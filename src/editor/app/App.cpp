#include "editor/app/App.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/hub/HubScreen.h"
#include "editor/shell/EditorScreen.h"
#include "platform/Shell.h"

#include <format>

namespace rm::editor {

App::App() = default;
App::~App() = default;

int App::Run(const LaunchOptions& options) {
    if (!gfx_.Init()) {
        MessageBoxW(nullptr, L"グラフィックスを初期化できませんでした (Direct3D 11 / Direct2D)。", L"RM Engine", MB_ICONERROR);
        return 1;
    }
    settings_.Load();
    plugins_.Load(platform::ExeDirectory() / L"Plugins");
    if (!options.projectFile.empty()) {
        RequestOpenProject(options.projectFile);
    } else {
        RequestHub();
    }
    ApplyPending();

    MSG msg;
    while (running_ && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        ApplyPending();
    }
    if (screen_) screen_->Close();
    screen_.reset();
    return 0;
}

void App::RequestOpenProject(const std::filesystem::path& file) {
    pending_ = Pending::OpenProject;
    pendingFile_ = file;
}

void App::RequestHub(std::string errorMessage) {
    pending_ = Pending::Hub;
    pendingMessage_ = std::move(errorMessage);
}

void App::RequestQuit() { pending_ = Pending::Quit; }

void App::Replace(std::unique_ptr<ScreenWindow> next) {
    std::unique_ptr<ScreenWindow> old = std::move(screen_);
    screen_ = std::move(next);
    if (old) old->Close();  // after the new window is shown, so the app never has zero windows
}

void App::ShowHub(const std::string& errorMessage) {
    auto hub = std::make_unique<HubScreen>(*this);
    if (!hub->Open()) {
        MessageBoxW(nullptr, L"Hub のウィンドウを作れませんでした。", L"RM Engine", MB_ICONERROR);
        running_ = false;
        return;
    }
    if (!errorMessage.empty()) hub->ShowError(Utf8ToWide(errorMessage));
    Replace(std::move(hub));
}

std::string App::MissingPluginsMessage(const Project& project) const {
    // Design RME_PLATFORM 5-a: a project that records a plugin which is not installed (or cannot be
    // used) is not opened - saving it without the plugin could only lose that plugin's data.
    std::string err;
    auto used = ParsePluginIdList(project.doc.Get("plugins"), &err);
    if (!used) return std::format("プロジェクトファイルの「plugins」の形が正しくありません: {}", err);
    std::string missing;
    for (const PluginId& id : *used) {
        if (plugins_.IsEnabled(id)) continue;
        const PluginStatus* st = plugins_.Find(id);
        missing += std::format("\n・{}{}", st ? st->displayName + " (" + id.Text() + ")" : id.Text(),
                               st ? " — " + st->reason : std::string(" — インストールされていません"));
    }
    if (missing.empty()) return {};
    return std::format("このプロジェクトが使っているプラグインがそろっていないため、開けません。{}\n\n"
                       "データを守るため、開かずに止めました。",
                       missing);
}

void App::ApplyPending() {
    while (pending_ != Pending::None) {
        const Pending kind = pending_;
        pending_ = Pending::None;
        switch (kind) {
            case Pending::Quit:
                if (screen_) screen_->Close();
                screen_.reset();
                running_ = false;
                return;
            case Pending::Hub:
                ShowHub(pendingMessage_);
                pendingMessage_.clear();
                break;
            case Pending::OpenProject: {
                std::string err;
                auto project = LoadProject(pendingFile_, &err);
                if (project) err = MissingPluginsMessage(*project);
                if (!project || !err.empty()) {
                    log::Error("{}", err);
                    if (auto* hub = dynamic_cast<HubScreen*>(screen_.get())) {
                        hub->ShowError(Utf8ToWide(err));
                    } else {
                        ShowHub(err);
                    }
                    break;
                }
                settings_.Touch(project->file);
                settings_.Save();
                log::Info("プロジェクトを開きます: {}", PathToUtf8(project->file));
                auto editor = std::make_unique<EditorScreen>(*this, std::move(*project));
                if (!editor->Open()) {
                    ShowHub("エディターのウィンドウを作れませんでした。");
                    break;
                }
                Replace(std::move(editor));
                break;
            }
            case Pending::None: break;
        }
    }
}

}  // namespace rm::editor
