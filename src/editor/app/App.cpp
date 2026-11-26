#include "editor/app/App.h"
#include "editor/project/Templates.h"
#include "editor/project/Project.h"
#include "editor/app/Paths.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/hub/HubScreen.h"
#include "editor/plugin/PluginDownload.h"
#include "platform/Http.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/ThemeFile.h"
#include "editor/widgets/Widgets.h"
#include "editor/shell/EditorScreen.h"
#include "platform/Shell.h"

#include <ctime>
#include <format>
#include <tuple>
#include <algorithm>

namespace rm::editor {

namespace {

// 要求は、ウィンドウに *送られた* メッセージ (taskkill や Windows のシャットダウンによる
// WM_CLOSE) から来ることがある。送られたメッセージは GetMessage の中で処理され、GetMessage は
// そのまま待ち続ける。だから要求は、次の無関係なメッセージまで待たされる。何もしないメッセージを
// post して、ループを起こす。
void WakeMessageLoop() { PostThreadMessageW(GetCurrentThreadId(), WM_NULL, 0, 0); }

}  // namespace

App::App() = default;

int App::RunExportCheck(const LaunchOptions& options) {
    // テンプレートから作ったプロジェクト (--new-from-template / --export-template)。
    auto makeProject = [&](const std::vector<std::wstring>& args, std::filesystem::path* file) {
        const std::string author = WideToUtf8(args[0]), plugin = WideToUtf8(args[1]), id = WideToUtf8(args[2]);
        const std::filesystem::path work = args[3];
        const TemplateTree& tree = plugins_.Templates();
        size_t node = static_cast<size_t>(-1);
        for (size_t i = 0; i < tree.nodes.size(); ++i) {
            const ProjectTemplate* t = tree.At(i);
            if (t && t->id == id && ((!t->plugin && author.empty()) || (t->plugin && t->plugin->author == author && t->plugin->name == plugin))) node = i;
        }
        if (node == static_cast<size_t>(-1)) {
            log::Error("書き出しの確かめ: テンプレートがありません: {} / {} / {}", author, plugin, id);
            return false;
        }
        std::vector<PluginId> unavailable;
        if (const auto plan = plugins_.TemplateInstallPlan(node, &unavailable); !plan.empty()) {
            std::string err;
            if (!InstallPlugins(plan, &err)) {
                log::Error("書き出しの確かめ: プラグインを入れられません: {}", err);
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::remove_all(work / L"Project", ec);
        std::filesystem::create_directories(work, ec);
        std::string err;
        auto project = CreateProject(work, "Project", *plugins_.Templates().At(node), &err);
        if (!project) {
            log::Error("書き出しの確かめ: プロジェクトを作れません: {}", err);
            return false;
        }
        *file = project->file;
        log::Info("書き出しの確かめ: プロジェクトを作りました: {}", PathToUtf8(*file));
        return true;
    };
    // 書き出したプロジェクト (--export / --export-template): ウィンドウなしで開き、各ソフトのデータを読む。
    auto exportProject = [&](const std::filesystem::path& file, const std::filesystem::path& out) {
        std::string err;
        auto project = LoadProject(file, &err);
        if (!project) {
            log::Error("書き出しの確かめ: {}", err);
            return false;
        }
        // プロジェクトが記録したプラグインで、まだ入っていないもの (と、その前提とつなぎ) を入れる。エディターで
        // 開くときに「入れて開く」を出すのと同じ (ここはウィンドウが無いので、聞かずに入れる)。
        if (const auto recorded = ParsePluginIdList(project->doc.Get("plugins"), nullptr)) {
            std::vector<PluginId> plan;
            for (const PluginId& id : *recorded) {
                if (plugins_.IsEnabled(id)) continue;
                std::vector<PluginId> unavailable;
                for (const PluginId& p : plugins_.InstallPlan(id, &unavailable)) {
                    if (std::find(plan.begin(), plan.end(), p) == plan.end()) plan.push_back(p);
                }
            }
            if (!plan.empty() && !InstallPlugins(plan, &err)) {
                log::Error("書き出しの確かめ: プラグインを入れられません: {}", err);
                return false;
            }
        }
        EditorScreen editor(*this, std::move(*project));
        if (!editor.ExportForCheck(out, &err)) {
            log::Error("書き出しの確かめ: 書き出せませんでした: {}", err);
            return false;
        }
        log::Info("書き出しの確かめ: 書き出しました: {}", PathToUtf8(out));
        return true;
    };
    std::filesystem::path file;
    if (options.newFromTemplate.size() == 4) return makeProject(options.newFromTemplate, &file) ? 0 : 1;
    if (options.exportProject.size() == 2) return exportProject(options.exportProject[0], options.exportProject[1]) ? 0 : 1;
    if (options.exportTemplate.size() == 4) {
        if (!makeProject(options.exportTemplate, &file)) return 1;
        return exportProject(file, std::filesystem::path(options.exportTemplate[3]) / L"Game") ? 0 : 1;
    }
    log::Error("書き出しの確かめ: 引数が足りません");
    return 2;
}
App::~App() = default;

int App::Run(const LaunchOptions& options) {
    if (!gfx_.Init()) {
        MessageBoxW(nullptr, Tr(L"グラフィックスを初期化できませんでした (Direct3D 11 / Direct2D)。").c_str(), L"RM Engine", MB_ICONERROR);
        return 1;
    }
    w::InitIcons(gfx_);
    settings_.Load();
    LoadLanguageCatalogs();
    ApplyOptions();
    SetUiThread();
    plugins_.Load(paths::EnginePlugins(), paths::InstalledPlugins(), paths::LegacyInstalledPlugins());
    StartEngineRefresh();  // 新しいエンジン: 入っているプラグインの新しいコピー (このスレッドの外でハッシュを取る)
    LoadLanguageCatalogs();  // (ダウンロードしたプラグインの英語も)
    LoadThemes();
    ApplyOptions();  // 選んだテーマはパックのものかもしれない
    if (!options.exportTemplate.empty() || !options.newFromTemplate.empty() || !options.exportProject.empty()) return RunExportCheck(options);
    StartCatalog();  // ダウンロードできるプラグインの一覧 (立ち上げたときだけ)
    if (!options.projectFile.empty()) {
        RequestOpenProject(options.projectFile);
    } else {
        RequestHub();
    }
    ApplyPending();

    // ウィンドウ用のログ (キューに入れるシンク) はここ、このスレッドで渡す。別のスレッドで書いた
    // ログは、ループを起こしてここに回す。
    const DWORD uiThread = GetCurrentThreadId();
    log::SetWake([uiThread] { PostThreadMessageW(uiThread, WM_NULL, 0, 0); });
    log::DeliverQueued();
    MSG msg;
    while (running_ && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        log::DeliverQueued();
        RunPostedToUi();  // 時間のかかるディスク作業 (Worker) の結果。フレームとフレームの間に
        ApplyPending();
    }
    log::SetWake({});
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
    // 核の英語 (<exe>/Lang/en.json) と各プラグインの英語 (<exe>/Plugins/<author>/<name>/lang/en.json)。
    // エンジン自身のコピーを使う。プラグインのコードはこの exe に組み込まれているので、文字もそれに合う。
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
    // ダウンロードしたプラグイン (exe に無い物と、エンジンの写しより新しい物) は、入れた写しの英語。
    for (const Plugins::Entry& e : plugins_.Entries()) {
        if (e.downloaded) files.push_back(plugins_.InstalledFolder(e.status.id) / L"lang" / L"en.json");
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

void App::StartEngineRefresh() {
    const auto candidates = plugins_.InstalledAndAvailable();
    engineWorker_.Run([this, list = paths::FileList(), available = plugins_.AvailableDir(), installed = plugins_.InstalledDir(),
                       candidates](std::stop_token, const Worker::Post& post) {
        Plugins::EngineRefreshPlan plan = Plugins::PlanEngineRefresh(list, available, installed, candidates);
        if (!plan.looked) return;
        post([this, plan = std::move(plan)] {
            engineRefresh_ = plan;
            ApplyEngineRefreshIfReady();
        });
    });
}

void App::ApplyEngineRefreshIfReady() {
    // エディターを開いていない間だけ。ワークスペースは、Reload が作り直すプラグインの登録簿を指している。
    if (!engineRefresh_ || !dynamic_cast<HubScreen*>(screen_.get())) return;
    const Plugins::EngineRefreshPlan plan = std::move(*engineRefresh_);
    engineRefresh_.reset();
    if (!plugins_.ApplyEngineRefresh(plan).empty()) {
        LoadThemes();
        ApplyOptions();
    }
}

void App::ReloadPlugins() {
    plugins_.Reload();
    LoadLanguageCatalogs();
    LoadThemes();
    ApplyOptions();
}

void App::StartCatalog() {
    std::string url = kPluginIndexUrl;
    wchar_t* over = nullptr;
    size_t overLen = 0;
    if (_wdupenv_s(&over, &overLen, kPluginIndexEnv) == 0 && over) {
        if (*over) url = WideToUtf8(over);  // (試すとき: 別の URL かローカルの index.json)
        free(over);
    }
    catalog_ = {};
    catalog_.url = url;
    catalogWorker_.Run([this, url](std::stop_token stop, const Worker::Post& post) {
        platform::FetchResult r = platform::Fetch(url, kMaxIndexBytes, stop);
        post([this, r = std::move(r)] {
            if (!r.body) {
                catalog_.state = PluginCatalog::State::Failed;
                catalog_.error = DescribeFetch(r);
                log::Warn("ダウンロードできるプラグインの一覧を読めませんでした: {} ({})", catalog_.url, WideToUtf8(catalog_.error));
                return;
            }
            json::ParseError pe;
            std::string err;
            const auto doc = json::Parse(*r.body, &pe);
            auto index = doc ? ParsePluginIndex(*doc, &err) : std::nullopt;
            if (!doc) err = TrU8F("JSON が壊れています ({} 行 {} 文字目: {})", pe.line, pe.column, pe.message);
            if (!index) {
                catalog_.state = PluginCatalog::State::Failed;
                catalog_.error = Utf8ToWide(err);
                log::Warn("ダウンロードできるプラグインの一覧を読めませんでした: {} ({})", catalog_.url, err);
                return;
            }
            for (const std::string& s : index->skipped) log::Warn("プラグインの一覧: {}", s);
            log::Info("ダウンロードできるプラグインの一覧を読みました: {} ({} 個)", catalog_.url, index->plugins.size());
            if (EngineUpdateAvailable(*index, kEngineVersion)) log::Info("新しいエンジンがあります: {} (今は {})", index->engine->version, kEngineVersion);
            catalog_.index = std::move(index);
            catalog_.state = PluginCatalog::State::Ready;
        });
    });
}

std::vector<PluginOffer> App::PluginOffersNow() const {
    if (!catalog_.index) return {};
    return PluginOffers(*catalog_.index, plugins_.InstalledManifests(), plugins_.Available(), kEngineVersion);
}

void App::DownloadPlugin(const PluginId& id) {
    if (catalog_.downloading) return;
    const std::vector<PluginOffer> offers = PluginOffersNow();
    const auto offer = std::find_if(offers.begin(), offers.end(), [&](const PluginOffer& o) { return o.plugin->id == id; });
    if (offer == offers.end()) return;
    const IndexPackage& pkg = *offer->package;
    // 前提: このエンジンにある物は一緒に入れる。無い物 (ほかにダウンロードが要る物) は先に入れてもらう。
    std::vector<PluginId> withEngine, missing;
    for (const PluginId& req : pkg.requirements) {
        if (plugins_.IsEnabled(req)) continue;
        std::vector<PluginId> unavailable;
        const auto plan = plugins_.InstallPlan(req, &unavailable);
        if (!unavailable.empty()) {
            missing.push_back(req);
            continue;
        }
        for (const PluginId& p : plan) {
            if (std::find(withEngine.begin(), withEngine.end(), p) == withEngine.end()) withEngine.push_back(p);
        }
    }
    if (!missing.empty()) {
        std::wstring names;
        for (const PluginId& m : missing) names += L"\n" + Utf8ToWide(m.Text());
        if (auto* hub = dynamic_cast<HubScreen*>(screen_.get())) hub->ShowError(Tr(L"先に次の前提プラグインを入れてください。") + names, {}, Tr(L"インストールできません"));
        return;
    }
    catalog_.downloading = id;
    const std::string url = ResolveUrl(catalog_.url, pkg.url);
    const std::filesystem::path folder = paths::InstalledPlugins() / L".downloads" / PathFromUtf8(id.author + "." + id.name);
    log::Info("プラグインを落としています: {} 版 {} ({})", id.Text(), pkg.version, url);
    downloadWorker_.Run([this, id, url, folder, size = pkg.size, sha = pkg.sha256, version = pkg.version, withEngine](std::stop_token stop,
                                                                                                              const Worker::Post& post) {
        PackageFetch r = FetchPackage(url, size, sha, folder, stop);
        post([this, id, url, folder, sha, version, withEngine, r = std::move(r)] {
            FinishedDownload done{id, version, sha, url, folder, DescribePackageFetch(r), withEngine};
            finishedDownload_ = std::move(done);
            ApplyDownloadIfReady();
        });
    });
}

void App::ApplyDownloadIfReady() {
    // エディターを開いていない間だけ (Reload がプラグインの登録簿を作り直す)。
    auto* hub = dynamic_cast<HubScreen*>(screen_.get());
    if (!finishedDownload_ || !hub) return;
    FinishedDownload d = std::move(*finishedDownload_);
    finishedDownload_.reset();
    catalog_.downloading.reset();
    if (!d.error.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(d.folder, ec);
        log::Warn("プラグインを落とせませんでした: {} ({})", d.id.Text(), WideToUtf8(d.error));
        hub->ShowError(d.error, {}, Tr(L"ダウンロードできませんでした"));
        return;
    }
    std::string err;
    bool ok = d.requirements.empty() || plugins_.Install(d.requirements, &err);
    json::Value marker = json::Value::MakeObject();
    marker.Set("source", "download");
    marker.Set("version", d.version);
    marker.Set("sha256", d.sha256);
    marker.Set("url", d.url);
    ok = ok && plugins_.InstallDownloaded(d.id, d.folder, marker, &err);
    ReloadPlugins();
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove_all(d.folder, ec);
        log::Warn("落としたプラグインを置けませんでした: {} ({})", d.id.Text(), err);
        hub->ShowError(Utf8ToWide(err), {}, Tr(L"インストールできませんでした"));
    }
}

namespace {

// ソフトを立ち上げた日の月 (1 .. 12)。最初に聞いた時のまま (季節で切り替えるテーマは、立ち上げた日付で決まる)。
int LaunchMonth() {
    static const int month = [] {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        if (localtime_s(&local, &now) != 0) return 1;
        return local.tm_mon + 1;
    }();
    return month;
}

}  // namespace

void App::LoadThemes() {
    theme::SetPackThemes({});  // 組み込みの 3 つだけ。パックを読む間
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
                // その絵: 同じプラグインのコードがその名前で出す。
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
    // ほかのテーマに従うテーマ (季節で切り替える): 立ち上げた日付の季節の、朝用のテーマ (夜用は予定)。
    std::vector<std::string> missing;
    theme::ResolveFollowing(themes, theme::SeasonOfMonth(LaunchMonth()), theme::DayPart::Morning, &missing);
    for (const std::string& id : missing) log::Warn("テーマ {} が従うテーマがありません (使いません)", id);
    for (const theme::ThemeInfo& t : themes) {
        if (t.follow) log::Info("テーマ {}: {} 月なので「{}」", t.id, LaunchMonth(), WideToUtf8(t.followed));
    }
    log::Info("テーマ: パックから {} 個", themes.size());
    theme::SetPackThemes(std::move(themes));
    // 選んだテーマがもうない (パックを消した、または入れていない): 以後は最初のテーマにして
    // hub.json に書く。パックをもう一度入れても戻らない。古い id (テーマの別名) は
    // そのテーマ本来の id にする。
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

// エディターの全ウィンドウ (Hub かエディター、そのパネルウィンドウ) をもう一度描く。
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
        // フレームの外 (メッセージボックスは自分でメッセージループを回す)。GPU ではなく Windows が描く。
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
    if (old) old->Close();  // 新しいウィンドウを出した後に閉じる。アプリのウィンドウが 0 個にならないように
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
    ApplyEngineRefreshIfReady();  // エディター (開いていれば) はもうない
    ApplyDownloadIfReady();
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
    // 設計 RME_PLATFORM 5-a: 使えないプラグインが記録されているプロジェクトは開かない。
    // そのプラグインなしで保存すると、そのプラグインのデータを失うだけだから。
    std::vector<PluginId> blocking;
    for (const PluginId& id : *used) {
        if (!plugins_.IsEnabled(id)) blocking.push_back(id);
    }
    if (!blocking.empty()) {
        log::Warn("プラグインがそろっていないため、開きません: {}", PathToUtf8(pendingFile_));
        if (HubScreen* hub = EnsureHub()) hub->AskBlockingPlugins(pendingFile_, blocking);
        return;
    }
    // 必要なものが足りない、入っているプラグイン: 開く前に 1 度だけ知らせる (2026-11-04 に決めた)。
    std::vector<PluginId> limited;
    for (const auto& e : plugins_.Entries()) {
        if (e.installed && e.status.state != PluginState::Enabled) limited.push_back(e.status.id);
    }
    if (!limited.empty() && !pendingSkipNotice_ && !settings_.LimitedNoticeHidden(limited)) {
        if (HubScreen* hub = EnsureHub()) hub->AskLimitedPlugins(pendingFile_, limited);
        return;
    }
    // 別の RM Engine で開いている: 2 重には開かない (互いの変更を上書きしてしまう)。
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
        // 何も書き込めないフォルダー (読み取り専用など): 開くが、守りはしない。
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
                // プラグインを読み直す前にエディターを閉じる。ワークスペースは登録簿を指しているから。
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
