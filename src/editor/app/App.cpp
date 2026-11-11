#include "editor/app/App.h"
#include "editor/app/Paths.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/hub/HubScreen.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/ThemeFile.h"
#include "editor/widgets/Widgets.h"
#include "editor/shell/EditorScreen.h"
#include "platform/Shell.h"

#include <format>
#include <tuple>
#include <algorithm>

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
        MessageBoxW(nullptr, Tr(L"グラフィックスを初期化できませんでした (Direct3D 11 / Direct2D)。").c_str(), L"RM Engine", MB_ICONERROR);
        return 1;
    }
    settings_.Load();
    LoadLanguageCatalogs();
    ApplyOptions();
    plugins_.Load(paths::EnginePlugins(), paths::InstalledPlugins());
    plugins_.RefreshFromEngine(paths::FileList());  // a new engine: new copies of the installed plugins
    LoadThemes();
    ApplyOptions();  // the theme chosen may be a pack's
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
    ReloadPlugins();
    return ok;
}

bool App::UninstallPlugin(const PluginId& id, std::string* error) {
    const bool ok = plugins_.Uninstall(id, error);
    ReloadPlugins();
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

void App::LoadLanguageCatalogs() {
    // The core's English (<exe>/Lang/en.json) and each plugin's (<exe>/Plugins/<author>/<name>/lang/en.json).
    // The engine's own copies: the plugins' code is built into this exe, so their texts match it.
    lang::Catalog& en = lang::EnglishCatalog();
    en.Clear();
    std::vector<std::filesystem::path> files{paths::LangFolder() / L"en.json"};
    std::error_code ec;
    for (std::filesystem::directory_iterator a(paths::EnginePlugins(), ec), end; !ec && a != end; a.increment(ec)) {
        std::error_code ec2;
        for (std::filesystem::directory_iterator n(a->path(), ec2), end2; !ec2 && n != end2; n.increment(ec2)) {
            files.push_back(n->path() / L"lang" / L"en.json");
        }
    }
    size_t count = 0;
    for (const auto& file : files) {
        std::error_code e3;
        if (!std::filesystem::exists(file, e3)) continue;
        std::string err;
        auto bytes = platform::ReadFileBytes(file, &err);
        json::ParseError pe;
        auto doc = bytes ? json::Parse(*bytes, &pe) : std::nullopt;
        if (!doc) {
            log::Warn("英語の表を読めませんでした: {}", PathToUtf8(file));
            continue;
        }
        count += en.Add(*doc);
    }
    log::Info("英語の表: {} 個", count);
}

void App::ReloadPlugins() {
    plugins_.Reload();
    LoadThemes();
    ApplyOptions();
}

void App::LoadThemes() {
    theme::SetPackThemes({});  // the built-in three only, while the packs are read
    const auto ornaments = plugins_.Registry().Contributions<w::OrnamentInfo>(w::kOrnamentPoint);
    std::vector<theme::ThemeInfo> themes;
    int group = 0;
    for (const Plugins::Entry& e : plugins_.Entries()) {
        if (e.status.state != PluginState::Enabled) continue;
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(plugins_.InstalledFolder(e.status.id) / L"themes", ec), end; !ec && it != end;
             it.increment(ec)) {
            if (it->path().extension() == L".json") files.push_back(it->path());
        }
        if (files.empty()) continue;
        std::vector<theme::ThemeFromFile> pack;
        for (const auto& file : files) {
            std::string err;
            auto bytes = platform::ReadFileBytes(file, &err);
            json::ParseError pe;
            auto doc = bytes ? json::Parse(*bytes, &pe) : std::nullopt;
            auto t = doc ? theme::ParseThemeFile(*doc, &err) : std::nullopt;
            if (!t) {
                log::Warn("テーマのファイルを読めませんでした: {} ({})", PathToUtf8(file), doc ? err : "JSON ではありません");
                continue;
            }
            pack.push_back(std::move(*t));
        }
        std::sort(pack.begin(), pack.end(), [](const auto& a, const auto& b) { return std::tie(a.order, a.info.id) < std::tie(b.order, b.info.id); });
        ++group;
        for (theme::ThemeFromFile& t : pack) {
            const auto same = [&](const theme::ThemeInfo& o) { return o.id == t.info.id; };
            if (std::any_of(theme::Themes().begin(), theme::Themes().end(), same) || std::any_of(themes.begin(), themes.end(), same)) {
                log::Warn("テーマの id が重なっています (後のほうを使いません): {} ({})", t.info.id, e.status.id.Text());
                continue;
            }
            if (!t.ornament.empty()) {
                // Its pictures: offered by the same plugin's code under that name.
                for (const auto& o : ornaments) {
                    if (o.plugin == e.status.id && t.ornament == o.item->name) t.info.ornament = o.item;
                }
                if (!t.info.ornament) log::Warn("テーマ {} の絵 \"{}\" がありません ({})", t.info.id, t.ornament, e.status.id.Text());
            }
            t.info.group = group;
            t.info.pack = Utf8ToWide(e.status.displayName);
            themes.push_back(std::move(t.info));
        }
    }
    log::Info("テーマ: パックから {} 個", themes.size());
    theme::SetPackThemes(std::move(themes));
    // The theme chosen is not there any more (its pack removed, or not installed): the first theme
    // from now on, written to hub.json - installing the pack again does not bring it back. An old id
    // (a theme's alias) becomes the theme's own.
    Options& o = settings_.GetOptions();
    const theme::ThemeInfo& now = theme::FindTheme(o.theme);
    if (now.id != o.theme) {
        log::Info("選んでいたテーマ {} が無いので、{} にしました", o.theme, now.id);
        o.theme = now.id;
        settings_.Save();
    }
}

void App::ApplyOptions() {
    const Options& o = settings_.GetOptions();
    theme::ApplyTheme(theme::FindTheme(o.theme), platform::SystemPrefersLight());
    lang::SetLanguage(o.language == UiLanguage::English ? lang::Language::English : lang::Language::Japanese);
    if (screen_) screen_->OnLookChanged();
}

namespace {

// Every window of the editor (the Hub or the editor, and its panel windows) drawn again.
void RedrawAllWindows() {
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND h, LPARAM) -> BOOL {
            InvalidateRect(h, nullptr, FALSE);
            return TRUE;
        },
        0);
}

}  // namespace

bool App::RecoverGraphics(ScreenWindow& asking) {
    if (!gfx_.Lost()) return true;
    if (graphicsFailures_ == 0) {
        log::Warn("GPU が使えなくなりました (0x{:08X})。デバイスを作り直します", static_cast<uint32_t>(gfx_.LostReason()));
    }
    if (gfx_.Recreate()) {
        graphicsFailures_ = 0;
        graphicsTold_ = false;
        RedrawAllWindows();
        return true;
    }
    ++graphicsFailures_;
    log::Error("GPU のデバイスを作り直せませんでした ({} 回目)。少ししてからもう一度試します", graphicsFailures_);
    constexpr int kTellAfter = 3;
    if (graphicsFailures_ >= kTellAfter && !graphicsTold_) {
        graphicsTold_ = true;
        // Outside the frame (a message box runs its own message loop). Drawn by Windows, not by the GPU.
        asking.Defer([this, hwnd = asking.Hwnd()] {
            ScreenWindow* screen = screen_.get();
            const std::wstring title = Tr(L"GPU が使えません");
            if (screen) {
                const int answer = MessageBoxW(hwnd,
                                               Tr(L"GPU (グラフィックス) が使えなくなり、作り直せませんでした。画面が正しく出ないことがあります。\n\n今の作業を保存しますか? 保存したら、エディターを閉じて開き直してください (PC の再起動が要ることもあります)。").c_str(),
                                               title.c_str(), MB_YESNO | MB_ICONERROR);
                if (answer != IDYES) return;
                const std::optional<bool> saved = screen->SaveWork();
                const std::wstring text = !saved ? Tr(L"保存するものはありません。エディターを閉じて開き直してください。")
                                          : *saved ? Tr(L"保存しました。エディターを閉じて開き直してください。")
                                                   : Tr(L"保存できませんでした。くわしくはコンソールとログを見てください。");
                MessageBoxW(hwnd, text.c_str(), title.c_str(), MB_OK | (saved.value_or(true) ? MB_ICONINFORMATION : MB_ICONERROR));
            }
        });
    }
    return false;
}

void App::SystemThemeChanged() {
    if (!theme::FindTheme(settings_.GetOptions().theme).palette) ApplyOptions();
}

void App::Replace(std::unique_ptr<ScreenWindow> next) {
    std::unique_ptr<ScreenWindow> old = std::move(screen_);
    screen_ = std::move(next);
    if (old) old->Close();  // after the new window is shown, so the app never has zero windows
}

void App::ShowHub(const std::string& errorMessage) {
    auto hub = std::make_unique<HubScreen>(*this);
    if (!hub->Open()) {
        MessageBoxW(nullptr, Tr(L"Hub のウィンドウを作れませんでした。").c_str(), L"RM Engine", MB_ICONERROR);
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
        if (!used) err = TrU8("プロジェクトファイルの「plugins」の形が正しくありません: ") + err;
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
    // Open in another RM Engine: never twice (both would save over each other's changes).
    auto lock = std::make_unique<platform::FileLock>();
    std::string lockErr;
    const auto taken = lock->Take(ProjectLockFile(project->file), &lockErr);
    if (taken == platform::FileLock::Result::HeldByOther) {
        const std::string msg = TrU8F("このプロジェクトは別の RM Engine で開いています。そちらを閉じてから開いてください。\n{}", PathToUtf8(project->file));
        log::Warn("{}", msg);
        if (HubScreen* hub = EnsureHub()) hub->ShowError(Utf8ToWide(msg));
        return;
    }
    if (taken == platform::FileLock::Result::Failed) {
        // A folder where nothing can be written (read only ...): opened, but not guarded.
        log::Warn("プロジェクトの印 (ほかの RM Engine で開かないための) を作れませんでした。そのまま開きます: {}", lockErr);
    }
    settings_.Touch(project->file);
    settings_.Save();
    log::Info("プロジェクトを開きます: {}", PathToUtf8(project->file));
    auto editor = std::make_unique<EditorScreen>(*this, std::move(*project));
    editor->AdoptLock(std::move(lock));
    if (!editor->Open()) {
        ShowHub(TrU8("エディターのウィンドウを作れませんでした。"));
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
                ReloadPlugins();
                RequestOpenProject(pendingFile_, true);
                break;
            }
            case Pending::None: break;
        }
    }
}

}  // namespace rm::editor
