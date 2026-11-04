#include "editor/app/App.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/hub/HubScreen.h"
#include "editor/shell/EditorScreen.h"
#include "platform/Shell.h"

#include <format>

namespace rm::editor {

namespace {

// A request may come from a message that was *sent* to a window (WM_CLOSE from taskkill or from
// Windows shutting down). Sent messages are handled inside GetMessage, which then keeps waiting -
// so the request would wait for the next unrelated message. A posted no-op wakes the loop up.
void WakeMessageLoop() { PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0); }

}  // namespace

App::App() = default;
App::~App() = default;

int App::Run(const LaunchOptions& options) {
    if (!gfx_.Init()) {
        MessageBoxW(nullptr, L"グラフィックスを初期化できませんでした (Direct3D 11 / Direct2D)。", L"RM Engine", MB_ICONERROR);
        return 1;
    }
    settings_.Load();
    plugins_.Load(platform::ExeDirectory() / L"Plugins",
                  platform::GetKnownFolder(platform::KnownFolder::LocalAppData) / L"RM_Engine" / L"Plugins");
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

void App::RequestOpenProject(const std::filesystem::path& file, bool skipNotice) {
    pending_ = Pending::OpenProject;
    pendingFile_ = file;
    pendingSkipNotice_ = skipNotice;
    WakeMessageLoop();
}

void App::RequestInstallAndReopen(std::vector<PluginId> ids, const std::filesystem::path& project) {
    pending_ = Pending::InstallAndReopen;
    pendingInstall_ = std::move(ids);
    pendingFile_ = project;
    WakeMessageLoop();
}

bool App::InstallPlugins(const std::vector<PluginId>& ids, std::string* error) {
    const bool ok = plugins_.Install(ids, error);
    plugins_.Reload();
    return ok;
}

bool App::UninstallPlugin(const PluginId& id, std::string* error) {
    const bool ok = plugins_.Uninstall(id, error);
    plugins_.Reload();
    return ok;
}

void App::RequestHub(std::string errorMessage) {
    pending_ = Pending::Hub;
    pendingMessage_ = std::move(errorMessage);
    WakeMessageLoop();
}

void App::RequestQuit() {
    pending_ = Pending::Quit;
    WakeMessageLoop();
}

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

HubScreen* App::EnsureHub() {
    if (auto* hub = dynamic_cast<HubScreen*>(screen_.get())) return hub;
    ShowHub({});
    return dynamic_cast<HubScreen*>(screen_.get());
}

void App::OpenProjectNow() {
    std::string err;
    auto project = LoadProject(pendingFile_, &err);
    std::optional<std::vector<PluginId>> used;
    if (project) {
        used = ParsePluginIdList(project->doc.Get("plugins"), &err);
        if (!used) err = "プロジェクトファイルの「plugins」の形が正しくありません: " + err;
    }
    if (!project || !used) {
        log::Error("{}", err);
        if (HubScreen* hub = EnsureHub()) hub->ShowError(Utf8ToWide(err));
        return;
    }
    // Design RME_PLATFORM 5-a: a project that records a plugin which cannot be used is not opened -
    // saving it without the plugin could only lose that plugin's data.
    std::vector<PluginId> blocking;
    for (const PluginId& id : *used) {
        if (!plugins_.IsEnabled(id)) blocking.push_back(id);
    }
    if (!blocking.empty()) {
        log::Warn("プラグインがそろっていないため、開きません: {}", PathToUtf8(pendingFile_));
        if (HubScreen* hub = EnsureHub()) hub->AskBlockingPlugins(pendingFile_, blocking);
        return;
    }
    // Installed plugins that lack a requirement: told once, before opening (decided 2026-11-04).
    std::vector<PluginId> limited;
    for (const auto& e : plugins_.Entries()) {
        if (e.installed && e.status.state != PluginState::Enabled) limited.push_back(e.status.id);
    }
    if (!limited.empty() && !pendingSkipNotice_ && !settings_.LimitedNoticeHidden(limited)) {
        if (HubScreen* hub = EnsureHub()) hub->AskLimitedPlugins(pendingFile_, limited);
        return;
    }
    settings_.Touch(project->file);
    settings_.Save();
    log::Info("プロジェクトを開きます: {}", PathToUtf8(project->file));
    auto editor = std::make_unique<EditorScreen>(*this, std::move(*project));
    if (!editor->Open()) {
        ShowHub("エディターのウィンドウを作れませんでした。");
        return;
    }
    Replace(std::move(editor));
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
            case Pending::OpenProject:
                OpenProjectNow();
                break;
            case Pending::InstallAndReopen: {
                std::string err;
                if (!plugins_.Install(pendingInstall_, &err)) {
                    log::Error("{}", err);
                    if (auto* editor = dynamic_cast<EditorScreen*>(screen_.get())) editor->ShowError(Utf8ToWide(err));
                    break;
                }
                // The editor closes before the plugins are read again: its workspaces point into the registry.
                if (screen_) screen_->Close();
                screen_.reset();
                plugins_.Reload();
                RequestOpenProject(pendingFile_, true);
                break;
            }
            case Pending::None: break;
        }
    }
}

}  // namespace rm::editor
