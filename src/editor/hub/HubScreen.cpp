#include "editor/hub/HubScreen.h"

#include "base/FileName.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/app/Integrity.h"
#include "editor/app/Paths.h"
#include "editor/hub/Updates.h"
#include "editor/lang/Lang.h"
#include "core/reflect/FieldCommand.h"
#include "editor/plugin/Plugins.h"
#include "editor/project/Project.h"
#include "editor/project/ProjectRename.h"
#include "editor/widgets/Theme.h"
#include "platform/Shell.h"
#include "resource.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cwctype>
#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

using ui::Rect;

namespace {

std::wstring FormatLocalTime(int64_t unixSeconds) {
    if (unixSeconds <= 0) return L"-";
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
    if (localtime_s(&tm, &t) != 0) return L"-";
    return std::format(L"{:04}/{:02}/{:02} {:02}:{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

int64_t FileModifiedUnix(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto st = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    return std::chrono::duration_cast<std::chrono::seconds>(st.time_since_epoch()).count();
}

std::wstring Lower(std::wstring s) {
    for (auto& ch : s) ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

std::wstring NameErrorText(FileNameError e) {
    switch (e) {
        case FileNameError::Empty: return Tr(L"プロジェクト名を入力してください。");
        case FileNameError::TooLong: return Tr(L"プロジェクト名が長すぎます (64 文字まで)。");
        case FileNameError::InvalidChar: return Tr(L"使えない文字が入っています ( < > : \" / \\ | ? * )。");
        case FileNameError::ReservedName: return Tr(L"Windows が予約している名前なので使えません (CON / NUL / COM1 など)。");
        case FileNameError::LeadingSpace: return Tr(L"先頭に空白は使えません。");
        case FileNameError::TrailingDotOrSpace: return Tr(L"末尾にピリオドや空白は使えません。");
        case FileNameError::None: break;
    }
    return {};
}

ui::TextStyle Style(float size, ui::Color color, bool bold = false, ui::HAlign h = ui::HAlign::Left) {
    ui::TextStyle s;
    s.size = size;
    s.color = color;
    s.bold = bold;
    s.h = h;
    return s;
}

}  // namespace

bool HubScreen::Open() {
    platform::WindowDesc d;
    d.title = L"RM Engine Hub";
    d.widthDip = 1280;
    d.heightDip = 760;
    d.minWidthDip = 980;
    d.minHeightDip = 600;
    if (!CreateScreenWindow(d, SW_SHOWNORMAL)) return false;
    log::Info("Hub を開きました");
    return true;
}

void HubScreen::OnCreated() {
    OnDeviceChanged();
    Refresh();
}

void HubScreen::OnDeviceChanged() {
    logo_ = platform::LoadBitmapFromMemory(app_.Gfx(), target_.Context(), platform::ResourceBytes(IDR_LOGO_HUB));
}

void HubScreen::OnCloseRequested() { app_.RequestQuit(); }

void HubScreen::OnLookChanged() {
    ScreenWindow::OnLookChanged();
    if (help_ && help_->IsOpen()) help_->OnLookChanged();
    // 読み直さない: 行の言葉は描くときに (今の言語で) 作る。
}

void HubScreen::ShowHelp() {
    // 閉じた窓は作り直す (入れたプラグインのヘルプも読み直す)。
    if (!help_ || !help_->IsOpen()) help_ = std::make_unique<HelpWindow>(app_);
    if (!help_->Show(Hwnd())) log::Error("ヘルプの窓を開けませんでした");
}

void HubScreen::ShowError(std::wstring message, fs::path removableFile, std::wstring title) {
    errorTitle_ = std::move(title);
    errorText_ = std::move(message);
    errorRemovable_ = std::move(removableFile);
    modal_ = Modal::Error;
    Invalidate();
}

// ---------------------------------------------------------------------------
// データ
// ---------------------------------------------------------------------------

HubScreen::RowData HubScreen::ReadRow(const fs::path& file) {
    // Worker のスレッドで: ファイルだけ (翻訳もログも画面も触らない)。
    RowData d;
    d.file = file;
    d.name = file.stem().native();
    std::error_code ec;
    d.missing = !fs::exists(file, ec);
    if (d.missing) return d;
    if (auto p = LoadProject(file, nullptr)) {
        d.name = Utf8ToWide(p->Name());
        if (auto used = ParsePluginIdList(p->doc.Get("plugins"), nullptr)) d.plugins = std::move(*used);
    }
    d.modifiedSort = FileModifiedUnix(file);
    return d;
}

void HubScreen::ApplyRow(const RowData& d) {
    for (Row& row : rows_) {
        if (row.file != d.file) continue;
        row.name = d.name;
        row.missing = d.missing;
        row.modifiedSort = d.modifiedSort;
        row.plugins = d.plugins;
        row.loading = false;
    }
    Invalidate();
}

void HubScreen::Refresh() {
    // その間に別の RM Engine が開いたり外したりしたもの (2026-11-18 画面で: エディターから戻ると、
    // Hub に別の方で外したプロジェクトが残り、作ったばかりのものは出なかった)。
    app_.Settings().SyncRecent();
    rows_.clear();
    std::vector<fs::path> files;
    for (const auto& r : app_.Settings().Recent()) {
        Row row;
        row.file = r.file;
        row.folder = r.file.parent_path().native();
        row.name = r.file.stem().native();  // 読み終わるまで
        rows_.push_back(std::move(row));
        files.push_back(r.file);
    }
    SortRows();
    // 遅い、または届かないドライブのプロジェクトは数秒かかる: Worker で 1 行ずつ読む。
    rowsWorker_.Run([this, files](std::stop_token stop, const Worker::Post& post) {
        for (const fs::path& file : files) {
            if (stop.stop_requested()) return;
            RowData d = ReadRow(file);
            post([this, d = std::move(d)] { ApplyRow(d); });
        }
        post([this] {
            SortRows();  // 分かった日付で
            Invalidate();
        });
    });
}

void HubScreen::SortRows() {
    auto key = [&](const Row& a, const Row& b) {
        switch (sortColumn_) {
            case 0: return Lower(a.name) < Lower(b.name);
            case 2: return Lower(a.folder) < Lower(b.folder);
            default: return a.modifiedSort < b.modifiedSort;
        }
    };
    std::stable_sort(rows_.begin(), rows_.end(), [&](const Row& a, const Row& b) { return sortAscending_ ? key(a, b) : key(b, a); });
}

std::vector<size_t> HubScreen::FilteredRows() const {
    std::vector<size_t> out;
    const std::wstring q = Lower(Utf8ToWide(search_));
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (q.empty() || Lower(rows_[i].name).find(q) != std::wstring::npos || Lower(rows_[i].folder).find(q) != std::wstring::npos) {
            out.push_back(i);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 操作
// ---------------------------------------------------------------------------

void HubScreen::OpenRow(const Row& row) {
    if (row.missing) {
        ShowError(Tr(L"プロジェクトが見つかりません。移動または削除された可能性があります。") + L"\n\n" + row.file.native(), row.file);
        return;
    }
    app_.RequestOpenProject(row.file);
}

void HubScreen::BrowseOpen() {
    Defer([this] {
        auto file = platform::PickOpenFile(Hwnd(), Tr(L"プロジェクトを開く"), Tr(L"RM Engine プロジェクト (*.rmpf)"), L"*.rmpf");
        if (file) app_.RequestOpenProject(*file);
    });
}

void HubScreen::BeginNewProject() {
    // いつもエンジンの Project フォルダ (別の場所はプロジェクトごとに 参照… で選べる)。
    const fs::path loc = paths::DefaultProjects();
    newLocation_ = PathToUtf8(loc);
    newName_ = "NewProject";
    std::error_code ec;
    for (int i = 2; fs::exists(loc / PathFromUtf8(newName_), ec) && i < 1000; ++i) newName_ = std::format("NewProject{}", i);
    newPath_ = BlankTemplate().path;
    newError_.clear();
    focusNewName_ = true;
    modal_ = Modal::NewProject;
    Invalidate();
}

void HubScreen::BrowseLocation() {
    Defer([this] {
        auto folder = platform::PickFolder(Hwnd(), Tr(L"保存先のフォルダを選択"), Utf8ToWide(newLocation_));
        if (folder) {
            ctx_.ClearFocus();  // フィールドが古い文字を新しい値の上に書き戻してはいけない
            newLocation_ = WideToUtf8(*folder);
            newError_.clear();
        }
    });
}

bool HubScreen::CheckNewProject() {
    const FileNameError ne = ValidateFileName(newName_);
    if (ne != FileNameError::None) {
        newError_ = NameErrorText(ne);
        return false;
    }
    const fs::path location = PathFromUtf8(newLocation_);
    if (newLocation_.empty()) {
        newError_ = Tr(L"保存先を選んでください。");
        return false;
    }
    if (!location.is_absolute()) {
        newError_ = Tr(L"保存先はドライブから始まるフォルダで指定してください (例: C:\\Users\\...)。");
        return false;
    }
    const fs::path file = location / PathFromUtf8(newName_) / (PathFromUtf8(newName_).native() + kProjectExtension);
    if (file.native().size() > 240) {
        newError_ = Tr(L"保存先のパスが長すぎます。もっと浅いフォルダを選んでください。");
        return false;
    }
    return true;
}

size_t HubScreen::NewNode() const { return app_.GetPlugins().Templates().Find(newPath_); }

void HubScreen::CreateNewProject() {
    if (!CheckNewProject()) return;
    const Plugins& plugins = app_.GetPlugins();
    const size_t node = NewNode();
    const ProjectTemplate* t = plugins.Templates().At(node);
    if (!t) {
        newError_ = Tr(L"テンプレートを選んでください (名前だけの枝からは作れません)。");
        return;
    }
    // テンプレートとその下にあるものが使うプラグイン (設計 09-29)。プロジェクトを開くときと同じ聞き方をする。
    std::vector<PluginId> unavailable;
    std::vector<PluginId> plan = plugins.TemplateInstallPlan(node, &unavailable);
    if (!unavailable.empty()) {
        newError_ = Tr(L"このエンジンには次のプラグインがありません。エンジンを新しい版にしてください。") + L"\n" + PluginNames(unavailable);
        return;
    }
    if (plan.empty()) {
        MakeNewProject();
        return;
    }
    Prompt pr;
    pr.kind = PromptKind::Create;
    pr.title = Tr(L"必須プラグインがインストールされていません");
    pr.body = TrF(L"テンプレート「{}」で作るには、次のプラグインが要ります (前提とつなぎを含みます)。インストールしてから作成しますか？",
                  Utf8ToWide(TrU8(t->name))) +
              L"\n" + PluginNames(plan);
    pr.toInstall = std::move(plan);
    // このフレームの後で: 作成を押した Enter が、質問への答えにもなってはいけない。
    Defer([this, pr = std::move(pr)]() mutable { ShowPrompt(std::move(pr)); });
}

void HubScreen::MakeNewProject() {
    modal_ = Modal::NewProject;
    const ProjectTemplate* t = app_.GetPlugins().Templates().At(NewNode());
    if (!t) {
        newError_ = Tr(L"テンプレートを選んでください (名前だけの枝からは作れません)。");
        return;
    }
    std::string err;
    auto project = CreateProject(PathFromUtf8(newLocation_), newName_, *t, &err);
    if (!project) {
        newError_ = Utf8ToWide(err);
        return;
    }
    modal_ = Modal::None;
    app_.RequestOpenProject(project->file);
}

void HubScreen::BeginRename(const Row& row) {
    if (row.missing) return;
    renameFile_ = row.file;
    renameOld_ = row.name;
    renameName_ = WideToUtf8(row.name);
    renameError_.clear();
    focusRename_ = true;
    modal_ = Modal::Rename;
    Invalidate();
}

void HubScreen::RunRename() {
    const FileNameError ne = ValidateFileName(renameName_);
    if (ne != FileNameError::None) {
        renameError_ = NameErrorText(ne);
        return;
    }
    // もうどこでもその名前で呼ばれている: 何もしない。
    const ProjectRenamePlan plan = PlanProjectRename(renameFile_, WideToUtf8(renameOld_), renameName_);
    if (WideToUtf8(renameOld_) == renameName_ && !plan.renameFolder && plan.file.native() == renameFile_.native()) {
        modal_ = Modal::None;
        return;
    }
    // 別の RM Engine で開いている: そのエディターは古い場所に保存し続けてしまう。
    {
        platform::FileLock probe;
        if (probe.Take(ProjectLockFile(renameFile_), nullptr) == platform::FileLock::Result::HeldByOther) {
            renameError_ = Tr(L"このプロジェクトは別の RM Engine で開いているので、名前を変えられません。そちらを閉じてからにしてください。");
            return;
        }
    }  // フォルダの名前を変える前に手放す (中で開いているファイルがあると止まる)
    std::string err;
    const auto newFile = RenameProject(renameFile_, renameName_, &err);
    if (!newFile) {
        renameError_ = Utf8ToWide(err);
        return;
    }
    app_.Settings().Replace(renameFile_, *newFile);
    app_.Settings().Save();
    if (SamePath(selected_, renameFile_)) selected_ = *newFile;
    modal_ = Modal::None;
    Refresh();
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void HubScreen::OnFrame(ui::Context& c, float width, float height) {
    auto& p = c.P();
    p.Clear(theme::Bg);
    w::DrawBackdrop(c, false);  // テーマの絵を全部の後ろに (サイドバーの上にもう一度)
    const bool overlay = modal_ != Modal::None || rowMenu_.open;
    c.SetInputEnabled(!overlay);

    if (!overlay) {
        if (c.KeyPressed(ui::Key::N, true)) BeginNewProject();
        if (c.KeyPressed(ui::Key::O, true)) BrowseOpen();
        if (c.KeyPressed(ui::Key::F5)) Refresh();
    }

    Rect full{0, 0, width, height};
    DrawSidebar(c, ui::TakeLeft(full, 232));
    if (page_ == Page::Projects) {
        DrawProjects(c, full.Inset(32, 28, 32, 28));
    } else if (page_ == Page::Plugins) {
        DrawPlugins(c, full.Inset(32, 28, 32, 28));
    } else if (page_ == Page::Updates) {
        DrawUpdates(c, full.Inset(32, 28, 32, 28));
    } else {
        DrawOptions(c, full.Inset(32, 28, 32, 28));
    }

    c.SetInputEnabled(true);
    if (rowMenu_.open) DrawRowMenu(c);
    if (modal_ == Modal::NewProject) DrawNewProject(c);
    if (modal_ == Modal::Rename) DrawRename(c);
    if (modal_ == Modal::Error) DrawError(c);
    if (modal_ == Modal::Plugin) DrawPluginPrompt(c);
    if (modal_ == Modal::Integrity) DrawIntegrity(c);
    if (modal_ == Modal::Theme) DrawThemePicker(c);
}

void HubScreen::DrawSidebar(ui::Context& c, Rect r) {
    auto& p = c.P();
    w::DrawBar(c, r, theme::Sidebar);
    p.FillRect({r.Right() - 1, r.y, 1, r.h}, theme::Border);

    float y = r.y + 34;
    if (logo_) {
        const auto sz = logo_->GetSize();
        const float lw = r.w - 64;
        const float lh = lw * sz.height / sz.width;
        // ロゴは 1 色: 暗いサイドバーでは白、明るいサイドバーでは黒 (どのテーマでも)。
        p.ImageTinted(logo_.Get(), {r.x + 32, y, lw, lh}, theme::InkOn(theme::Sidebar));
        y += lh + 40;
    } else {
        p.Text({r.x + 28, y, r.w - 56, 40}, L"RM Engine", Style(22, theme::Text, true));
        y += 72;
    }

    // ページ: 幅いっぱいの行を線で区切る (行全体がボタン)。
    constexpr float kNavH = 50.f;
    w::Separator(c, r.x, y, r.w - 1);
    auto nav = [&](std::string_view id, std::wstring_view label, Page page) {
        if (w::NavItem(c, id, {r.x, y + 1, r.w - 1, kNavH - 1}, label, page_ == page)) page_ = page;
        y += kNavH;
        w::Separator(c, r.x, y, r.w - 1);
    };
    nav("nav.projects", Tr(L"プロジェクト"), Page::Projects);
    nav("nav.plugins", Tr(L"プラグイン"), Page::Plugins);
    nav("nav.updates", Tr(L"アップデート情報"), Page::Updates);
    nav("nav.options", Tr(L"オプション"), Page::Options);
    // ヘルプはページではなく別の窓 (Hub にはドッキングが無い)。
    if (w::NavItem(c, "nav.help", {r.x, y + 1, r.w - 1, kNavH - 1}, Tr(L"ヘルプ"), false)) Defer([this] { ShowHelp(); });
    y += kNavH;
    w::Separator(c, r.x, y, r.w - 1);

    p.Text({r.x + 28, r.Bottom() - 44, r.w - 56, 20}, TrF(L"バージョン {}", std::wstring(kEngineVersionW)), Style(12, theme::TextFaint));
    // 一覧 (立ち上げたときに読んだ) のエンジンの版が今のより新しい: 知らせる。
    const PluginCatalog& catalog = app_.Catalog();
    if (catalog.index && EngineUpdateAvailable(*catalog.index, kEngineVersion)) {
        ui::TextStyle ts = Style(12, theme::Warn, true);
        ts.wrap = true;
        p.Text({r.x + 28, r.Bottom() - 84, r.w - 56, 40}, TrF(L"新しい版 {} があります", Utf8ToWide(catalog.index->engine->version)), ts);
    }
}

void HubScreen::DrawProjects(ui::Context& c, Rect area) {
    auto& p = c.P();
    const Rect header = ui::TakeTop(area, 40);
    p.Text(header, Tr(L"プロジェクト"), Style(24, theme::Text, true));
    const Rect btnNew{header.Right() - 132, header.y + 2, 132, 36};
    const Rect btnOpen{btnNew.x - 12 - 112, header.y + 2, 112, 36};
    if (w::Button(c, "hub.new", btnNew, Tr(L"新規作成"), w::ButtonKind::Primary)) BeginNewProject();
    if (w::Button(c, "hub.open", btnOpen, Tr(L"開く…"))) BrowseOpen();

    ui::TakeTop(area, 18);
    const Rect searchRow = ui::TakeTop(area, 34);
    w::TextField(c, "hub.search", {searchRow.x, searchRow.y, std::min(360.f, searchRow.w), 34}, search_, Tr(L"プロジェクトを検索"));
    ui::TakeTop(area, 16);

    // 表 (ページ自身の背景の上: 見出しの線と、ホバー / 選択中の行だけを描く)
    Rect table = area;
    const Rect head = ui::TakeTop(table, 38);
    const float menuW = 48, modW = 180;
    const float nameW = std::max(160.f, (table.w - menuW - modW) * 0.40f);
    const float pathW = std::max(80.f, table.w - menuW - modW - nameW);
    const float xName = table.x, xMod = xName + nameW, xPath = xMod + modW;

    auto headerCell = [&](std::string_view id, float x, float wdt, std::wstring_view label, int column) {
        const Rect cell{x, head.y, wdt, head.h};
        const auto b = c.ButtonBehavior(c.MakeId(id), cell);
        const ui::Color ink = b.hovered || sortColumn_ == column ? theme::Text : theme::TextDim;
        p.Text(cell.Inset(18, 0, 8, 0), label, Style(13, ink, true));
        if (sortColumn_ == column) {
            const float lw = p.TextWidth(label, 13, true);
            w::DrawIcon(c, {cell.x + 18 + lw + 4, cell.y, 16, cell.h}, sortAscending_ ? w::Icon::ChevronUp : w::Icon::ChevronDown, ink);
        }
        if (b.clicked) {
            if (sortColumn_ == column) {
                sortAscending_ = !sortAscending_;
            } else {
                sortColumn_ = column;
                sortAscending_ = column != 1;  // 日付: 新しい順
            }
            SortRows();
        }
    };
    headerCell("col.name", xName, nameW, Tr(L"名前"), 0);
    headerCell("col.mod", xMod, modW, Tr(L"更新日時"), 1);
    headerCell("col.path", xPath, pathW, Tr(L"場所"), 2);
    w::Separator(c, table.x, head.Bottom() - 1, table.w);

    const std::vector<size_t> visible = FilteredRows();
    if (visible.empty()) {
        const std::wstring msg = rows_.empty() ? Tr(L"プロジェクトがありません。\n「新規作成」で作るか、「開く…」で既存のプロジェクト (.rmpf) を追加してください。")
                                               : Tr(L"検索に一致するプロジェクトはありません。");
        ui::TextStyle ts = Style(14, theme::TextDim, false, ui::HAlign::Center);
        ts.wrap = true;
        p.Text(table.Inset(40, 0, 40, 40), msg, ts);
        return;
    }

    // 幅いっぱいの行を線で区切る。行全体がボタン。
    const float rowH = 56;
    const float content = rowH * static_cast<float>(visible.size());
    const Rect view = table;
    const float off = w::BeginScroll(c, "hub.rows", view, content);
    for (size_t vi = 0; vi < visible.size(); ++vi) {
        const Row& row = rows_[visible[vi]];
        const float top = view.y + rowH * static_cast<float>(vi) - off;
        const Rect rr{view.x, top, view.w, rowH - 1};
        if (rr.Bottom() < view.y || rr.y > view.Bottom()) continue;
        c.PushId(WideToUtf8(row.file.native()));
        const auto b = w::Selectable(c, "row", rr, SamePath(selected_, row.file), true);
        w::Separator(c, view.x, top + rowH - 1, view.w);

        p.Text({xName + 18, rr.y, nameW - 26, rr.h}, row.name, Style(16, row.missing ? theme::TextFaint : theme::Text, true));
        const std::wstring when = row.loading ? Tr(L"読み込み中…") : row.missing ? Tr(L"見つかりません") : FormatLocalTime(row.modifiedSort);
        p.Text({xMod + 18, rr.y, modW - 26, rr.h}, when, Style(14, row.missing ? theme::Danger : row.loading ? theme::TextFaint : theme::TextDim));
        p.Text({xPath + 18, rr.y, pathW - 26, rr.h}, row.folder, Style(13, theme::TextFaint));

        const Rect more{rr.Right() - 44, rr.y + 10, 34, rr.h - 20};
        if (w::IconButton(c, "more", more, w::Icon::Dots)) {
            selected_ = row.file;
            rowMenuFile_ = row.file;
            rowMenu_.OpenAt(more.x, more.Bottom() + 2);
        } else if (b.doubleClicked) {
            OpenRow(row);
        } else if (b.clicked) {
            selected_ = row.file;
        } else if (b.rightClicked) {
            selected_ = row.file;
            rowMenuFile_ = row.file;
            rowMenu_.OpenAt(c.In().mouseX, c.In().mouseY);
        }
        c.PopId();
    }
    w::EndScroll(c, "hub.rows", view, content);

    if (!selected_.empty() && c.Focused() == 0 && (c.KeyPressed(ui::Key::Enter) || c.KeyPressed(ui::Key::F2))) {
        const bool open = c.KeyPressed(ui::Key::Enter);
        for (const auto& row : rows_) {
            if (!SamePath(row.file, selected_)) continue;
            if (open) {
                OpenRow(row);
            } else {
                BeginRename(row);
            }
            break;
        }
    }
}

void HubScreen::DrawPlugins(ui::Context& c, Rect area) {
    auto& p = c.P();
    const Plugins& plugins = app_.GetPlugins();
    p.Text(ui::TakeTop(area, 40), Tr(L"プラグイン"), Style(24, theme::Text, true));
    ui::TextStyle note = Style(13, theme::TextDim);
    note.wrap = true;
    note.v = ui::VAlign::Top;
    p.Text(ui::TakeTop(area, 44),
           Tr(L"インストールしたプラグインは、すべてのプロジェクトで使えます。前提のプラグインは一緒にインストールされます。削除はここだけでできます。"),
           note);
    ui::TakeTop(area, 4);
    // ソフトで絞る (設計 1-b-2 A): すべて / テンプレートの木の各ソフト / 共通。
    const auto& groups = plugins.SoftwareGroups();
    if (!groups.empty()) {
        std::vector<std::wstring> texts{Tr(L"すべて")};
        for (const SoftwareGroup& g : groups) texts.push_back(Tr(Utf8ToWide(g.name)));
        texts.push_back(Tr(L"共通"));
        const std::vector<std::wstring_view> labels(texts.begin(), texts.end());
        if (pluginFilter_ < 0 || pluginFilter_ >= static_cast<int>(labels.size())) pluginFilter_ = 0;
        const Rect row = ui::TakeTop(area, 32);
        p.Text({row.x, row.y, 110, row.h}, Tr(L"ソフトで絞る"), Style(12, theme::TextDim));
        const float segW = std::min(row.w - 110, 104.f * static_cast<float>(labels.size()));
        const int picked = w::Segmented(c, "plg.filter", {row.x + 110, row.y, segW, row.h}, labels, pluginFilter_);
        if (picked >= 0) pluginFilter_ = picked;
        ui::TakeTop(area, 12);
    }
    if (plugins.Entries().empty()) {
        const Rect box = ui::TakeTop(area, 80);
        w::DrawSurface(c, box, w::Surface::Panel, 8.f);
        p.Text(box.Inset(24, 0, 24, 0), Tr(L"このエンジンにはプラグインが入っていません"), Style(15, theme::Text, true));
        return;
    }
    // 行はスクロールする (ウィンドウの高さよりプラグインが多いとき)。
    const Rect view{area.x - 4, area.y, area.w + 8, area.h};
    float& contentH = c.FloatState(c.MakeId("plg.contentH"), 600.f);
    const float off = w::BeginScroll(c, "plg.scroll", view, contentH);
    Rect content{area.x, area.y - off, area.w, 100000.f};
    const float top = content.y;
    auto outside = [&](const Rect& row) { return row.Bottom() < view.y || row.y > view.Bottom(); };
    DrawDownloads(c, content, view);
    bool anyShown = false;
    for (const Plugins::Entry& e : plugins.Entries()) {
        if (e.connector) continue;  // 下に離して置く: 手では選ばない
        if (!PluginShown(e.status.id)) continue;
        anyShown = true;
        const PluginStatus& st = e.status;
        const Rect row = ui::TakeTop(content, 72);
        ui::TakeTop(content, 8);
        if (outside(row)) continue;
        w::DrawSurface(c, row, w::Surface::Panel, 8.f);
        p.Text({row.x + 24, row.y + 14, row.w * 0.4f, 24}, Utf8ToWide(TrU8(st.displayName)), Style(15, theme::Text, true));
        p.Text({row.x + 24, row.y + 40, row.w * 0.4f, 20}, TrF(L"作者 {}   名前 {}", Utf8ToWide(st.id.author), Utf8ToWide(st.id.name)),
               Style(12, theme::TextFaint));

        // ボタンは右端から。
        c.PushId(st.id.author);
        c.PushId(st.id.name);
        Rect b{row.Right() - 20 - 110, row.y + 18, 110, 36};
        auto next = [&](float wdt) {
            const Rect r{b.x - 8 - wdt, b.y, wdt, b.h};
            return r;
        };
        float textRight = b.x;
        if (!e.installed) {
            const Rect install{row.Right() - 20 - 150, b.y, 150, 36};
            if (w::Button(c, "install", install, e.available ? Tr(L"インストール") : Tr(L"このエンジンに無い"), w::ButtonKind::Primary,
                          e.available)) {
                BeginInstall(st.id);
            }
            textRight = install.x;
        } else {
            if (w::Button(c, "remove", b, Tr(L"削除"))) BeginRemove(st.id);
            const Rect re = next(130);
            if (w::Button(c, "reinstall", re, Tr(L"再インストール"), w::ButtonKind::Secondary, e.available)) RunInstall({st.id});
            textRight = re.x;
            if (LacksRequirement(st.state)) {
                b = re;
                const Rect req = next(150);
                if (w::Button(c, "requirements", req, Tr(L"前提をインストール"), w::ButtonKind::Primary)) {
                    std::vector<PluginId> unavailable;
                    const auto plan = plugins.InstallPlan(st.id, &unavailable);
                    if (!unavailable.empty()) {
                        ShowError(Tr(L"このエンジンには次の前提プラグインがありません。エンジンを新しい版にしてください。") + L"\n" +
                                      PluginNames(unavailable),
                                  {}, Tr(L"インストールできません"));
                    } else {
                        RunInstall(plan);
                    }
                }
                textRight = req.x;
            }
        }
        c.PopId();
        c.PopId();

        const Rect status{row.x + row.w * 0.42f, row.y, textRight - 16 - (row.x + row.w * 0.42f), row.h};
        if (st.state == PluginState::Enabled) {
            p.Text(status, Tr(L"使用中"), Style(13, theme::TextDim, false, ui::HAlign::Right));
        } else if (st.state == PluginState::NotInstalled) {
            p.Text(status, Tr(L"インストールされていません"), Style(13, theme::TextFaint, false, ui::HAlign::Right));
        } else if (LacksRequirement(st.state)) {
            p.Text(status, TrF(L"⚠ 前提が無い: {}", Utf8ToWide(st.reason)), Style(12, theme::Warn, false, ui::HAlign::Right));
        } else {
            p.Text(status, TrF(L"⚠ 使えません: {}", Utf8ToWide(st.reason)), Style(12, theme::Danger, false, ui::HAlign::Right));
        }
    }
    if (!anyShown) p.Text(ui::TakeTop(content, 40), Tr(L"このソフトで使うプラグインはありません。"), Style(13, theme::TextDim));
    DrawConnectors(c, content, view);
    contentH = content.y - top;
    w::EndScroll(c, "plg.scroll", view, contentH);
}

// ダウンロードできるプラグイン: ポートフォリオの一覧 (立ち上げたときに読んだ) にあって、入っていない物か、
// 入っている物より新しい版がある物。押すと落として入れる (App::DownloadPlugin)。
void HubScreen::DrawDownloads(ui::Context& c, Rect& area, const Rect& view) {
    auto& p = c.P();
    const PluginCatalog& catalog = app_.Catalog();
    auto outside = [&](const Rect& row) { return row.Bottom() < view.y || row.y > view.Bottom(); };
    if (catalog.state == PluginCatalog::State::Loading) {
        p.Text(ui::TakeTop(area, 28), Tr(L"ダウンロードできるプラグインの一覧を読んでいます…"), Style(13, theme::TextFaint));
        ui::TakeTop(area, 8);
        return;
    }
    if (catalog.state == PluginCatalog::State::Failed) {
        ui::TextStyle ts = Style(12, theme::Warn);
        ts.wrap = true;
        ts.v = ui::VAlign::Top;
        p.Text(ui::TakeTop(area, 36), TrF(L"ダウンロードできるプラグインの一覧を読めませんでした: {}", catalog.error), ts);
        ui::TakeTop(area, 8);
        return;
    }
    const std::vector<PluginOffer> offers = app_.PluginOffersNow();
    if (offers.empty()) return;
    p.Text(ui::TakeTop(area, 28), Tr(L"ダウンロードできるプラグイン"), Style(16, theme::Text, true));
    ui::TakeTop(area, 4);
    for (const PluginOffer& offer : offers) {
        const Rect row = ui::TakeTop(area, 72);
        ui::TakeTop(area, 8);
        if (outside(row)) continue;
        const IndexPlugin& plugin = *offer.plugin;
        w::DrawSurface(c, row, w::Surface::Panel, 8.f);
        p.Text({row.x + 24, row.y + 14, row.w * 0.4f, 24}, Utf8ToWide(TrU8(plugin.displayName)), Style(15, theme::Text, true));
        p.Text({row.x + 24, row.y + 40, row.w * 0.4f, 20},
               TrF(L"作者 {}   名前 {}", Utf8ToWide(plugin.id.author), Utf8ToWide(plugin.id.name)), Style(12, theme::TextFaint));
        c.PushId("dl");
        c.PushId(plugin.id.author);
        c.PushId(plugin.id.name);
        const bool busy = catalog.downloading.has_value();
        const bool mine = busy && *catalog.downloading == plugin.id;
        const Rect btn{row.Right() - 20 - 190, row.y + 18, 190, 36};
        const std::wstring label = mine ? Tr(L"ダウンロード中…") : offer.kind == OfferKind::Update ? Tr(L"更新") : Tr(L"ダウンロードして入れる");
        if (w::Button(c, "get", btn, label, w::ButtonKind::Primary, !busy)) {
            Defer([this, id = plugin.id] { app_.DownloadPlugin(id); });
        }
        c.PopId();
        c.PopId();
        c.PopId();
        const Rect status{row.x + row.w * 0.42f, row.y, btn.x - 16 - (row.x + row.w * 0.42f), row.h};
        const std::wstring text = offer.kind == OfferKind::Update
                                      ? TrF(L"新しい版 {} (今は {})", Utf8ToWide(offer.package->version),
                                            offer.currentVersion.empty() ? Tr(L"版なし") : Utf8ToWide(offer.currentVersion))
                                      : TrF(L"版 {}", Utf8ToWide(offer.package->version));
        p.Text(status, text, Style(13, theme::TextDim, false, ui::HAlign::Right));
    }
    ui::TakeTop(area, 12);
}

// コネクタ (ゲーム制作 × 3D ...): インストール / 削除のボタンはない - つなぐものが全部
// 入ると 1 つ付いてきて、そのどれかを削除すると止まる。つなぐものとその状態だけを出す。
// 壊れたものはもう一度コピーできる。
void HubScreen::DrawConnectors(ui::Context& c, Rect& area, const Rect& view) {
    auto& p = c.P();
    const Plugins& plugins = app_.GetPlugins();
    const bool any = std::any_of(plugins.Entries().begin(), plugins.Entries().end(),
                                 [&](const Plugins::Entry& e) { return e.connector && PluginShown(e.status.id); });
    if (!any) return;
    ui::TakeTop(area, 10);
    p.Text(ui::TakeTop(area, 28), Tr(L"つなぎのプラグイン"), Style(16, theme::Text, true));
    ui::TextStyle note = Style(12, theme::TextDim);
    note.wrap = true;
    note.v = ui::VAlign::Top;
    p.Text(ui::TakeTop(area, 36), Tr(L"2 つのプラグインをつなぐものです。つなぐ両方が入っていると自動で入り、自分で選ぶことはありません。"), note);
    for (const Plugins::Entry& e : plugins.Entries()) {
        if (!e.connector || !PluginShown(e.status.id)) continue;
        const PluginStatus& st = e.status;
        const Rect row = ui::TakeTop(area, 44);
        ui::TakeTop(area, 6);
        if (row.Bottom() < view.y || row.y > view.Bottom()) continue;
        w::DrawSurface(c, row, w::Surface::Panel, 8.f);
        std::wstring joins;
        for (const PluginId& id : e.joins) joins += (joins.empty() ? L"" : L" + ") + Utf8ToWide(TrU8(plugins.DisplayName(id)));
        p.Text({row.x + 20, row.y, row.w * 0.3f, row.h}, Utf8ToWide(TrU8(st.displayName)), Style(13, theme::Text, true));
        p.Text({row.x + 20 + row.w * 0.3f, row.y, row.w * 0.3f, row.h}, TrF(L"つなぐもの: {}", joins), Style(12, theme::TextFaint));

        float textRight = row.Right() - 16;
        const bool broken = e.installed && st.state != PluginState::Enabled && !LacksRequirement(st.state);
        if (broken) {
            c.PushId(st.id.author);
            c.PushId(st.id.name);
            const Rect re{row.Right() - 16 - 130, row.y + 7, 130, 30};
            if (w::Button(c, "reinstall", re, Tr(L"再インストール"), w::ButtonKind::Secondary, e.available)) RunInstall({st.id});
            c.PopId();
            c.PopId();
            textRight = re.x - 12;
        }
        const Rect status{row.x + row.w * 0.6f, row.y, textRight - (row.x + row.w * 0.6f), row.h};
        if (st.state == PluginState::Enabled) {
            p.Text(status, Tr(L"使用中"), Style(12, theme::TextDim, false, ui::HAlign::Right));
        } else if (!e.installed) {
            p.Text(status, Tr(L"つなぐ両方が入ると自動で入ります"), Style(12, theme::TextFaint, false, ui::HAlign::Right));
        } else if (LacksRequirement(st.state)) {
            p.Text(status, Tr(L"つなぐものが足りないので休んでいます"), Style(12, theme::TextFaint, false, ui::HAlign::Right));
        } else {
            p.Text(status, TrF(L"⚠ 使えません: {}", Utf8ToWide(st.reason)), Style(12, theme::Danger, false, ui::HAlign::Right));
        }
    }
}

bool HubScreen::PluginShown(const PluginId& id) const {
    const auto& groups = app_.GetPlugins().SoftwareGroups();
    const int n = static_cast<int>(groups.size());
    if (pluginFilter_ <= 0 || pluginFilter_ > n + 1) return true;
    auto in = [&](const SoftwareGroup& g) { return std::find(g.plugins.begin(), g.plugins.end(), id) != g.plugins.end(); };
    if (pluginFilter_ == n + 1) return std::none_of(groups.begin(), groups.end(), in);  // 共通: どのソフトにも入っていない
    return in(groups[static_cast<size_t>(pluginFilter_ - 1)]);
}

// ---------------------------------------------------------------------------
// プラグインの質問
// ---------------------------------------------------------------------------

std::wstring HubScreen::PluginLine(const PluginId& id) const {
    const Plugins::Entry* e = app_.GetPlugins().Find(id);
    const std::wstring name = Utf8ToWide(app_.GetPlugins().DisplayName(id));
    if (!e) return L"・" + name + L" — " + Tr(L"このエンジンにはありません");
    return L"・" + name + L" — " + Utf8ToWide(e->status.reason);
}

std::wstring HubScreen::PluginNames(const std::vector<PluginId>& ids) const {
    std::wstring out;
    for (const PluginId& id : ids) {
        if (!out.empty()) out += L"\n";
        out += L"・" + Utf8ToWide(app_.GetPlugins().DisplayName(id)) + L"  (" + Utf8ToWide(id.Text()) + L")";
    }
    return out;
}

int HubScreen::RecentProjectsUsing(const PluginId& id) const {
    // Hub が知っているプロジェクトだけ (最近の一覧。Worker が読んだもの) - PC の中は探さない。
    int n = 0;
    for (const Row& row : rows_) {
        if (!row.loading && std::find(row.plugins.begin(), row.plugins.end(), id) != row.plugins.end()) ++n;
    }
    return n;
}

void HubScreen::ShowPrompt(Prompt prompt) {
    prompt_ = std::move(prompt);
    modal_ = Modal::Plugin;
    Invalidate();
}

namespace {

void AppendUnique(std::vector<PluginId>& to, const std::vector<PluginId>& from) {
    for (const PluginId& id : from) {
        if (std::find(to.begin(), to.end(), id) == to.end()) to.push_back(id);
    }
}

}  // namespace

void HubScreen::AskBlockingPlugins(const fs::path& project, const std::vector<PluginId>& blocking) {
    Prompt pr;
    pr.kind = PromptKind::Blocking;
    pr.project = project;
    pr.title = Tr(L"起動できません");
    std::vector<PluginId> unavailable;
    for (const PluginId& id : blocking) AppendUnique(pr.toInstall, app_.GetPlugins().InstallPlan(id, &unavailable));
    pr.body = Tr(L"このプロジェクトが使っているプラグインがそろっていないため、開けません。") + L"\n";
    for (const PluginId& id : blocking) pr.body += PluginLine(id) + L"\n";
    if (!unavailable.empty()) {
        pr.toInstall.clear();  // 完全にはできない: 出せるものはない
        pr.body += L"\n" + Tr(L"このエンジンには次のプラグインがありません。エンジンを新しい版にしてください。") + L"\n" + PluginNames(unavailable);
    } else if (!pr.toInstall.empty()) {
        pr.body += L"\n" + Tr(L"前提プラグインをインストールしますか？") + L"\n" + PluginNames(pr.toInstall);
    } else {
        pr.body += L"\n" + Tr(L"データを守るため、開かずに止めました。");
    }
    ShowPrompt(std::move(pr));
}

void HubScreen::AskLimitedPlugins(const fs::path& project, const std::vector<PluginId>& limited) {
    Prompt pr;
    pr.kind = PromptKind::Limited;
    pr.project = project;
    pr.limited = limited;
    pr.title = Tr(L"お知らせ");
    for (const PluginId& id : limited) AppendUnique(pr.toInstall, app_.GetPlugins().InstallPlan(id, nullptr));
    const bool onlyRequirements = std::all_of(limited.begin(), limited.end(), [&](const PluginId& id) {
        const Plugins::Entry* e = app_.GetPlugins().Find(id);
        return e && LacksRequirement(e->status.state);
    });
    pr.body = (onlyRequirements ? Tr(L"前提プラグインが存在しないプラグインがあるため、その機能が制限されます。")
                                : Tr(L"使えないプラグインがあるため、その機能が制限されます。")) +
              L"\n";
    for (const PluginId& id : limited) pr.body += PluginLine(id) + L"\n";
    if (!pr.toInstall.empty()) pr.body += L"\n" + Tr(L"「インストール」で次のプラグインを入れてから開きます。") + L"\n" + PluginNames(pr.toInstall);
    ShowPrompt(std::move(pr));
}

void HubScreen::BeginInstall(const PluginId& id) {
    std::vector<PluginId> unavailable;
    const auto plan = app_.GetPlugins().InstallPlan(id, &unavailable);
    if (!unavailable.empty()) {
        ShowError(Tr(L"このエンジンには次の前提プラグインがありません。エンジンを新しい版にしてください。") + L"\n" + PluginNames(unavailable), {},
                  Tr(L"インストールできません"));
        return;
    }
    if (plan.size() <= 1) {
        RunInstall(plan);
        return;
    }
    Prompt pr;
    pr.kind = PromptKind::Install;
    pr.title = Tr(L"プラグインのインストール");
    pr.toInstall = plan;
    pr.body = TrF(L"「{}」をインストールします。前提のプラグインも一緒に入ります。", Utf8ToWide(app_.GetPlugins().DisplayName(id))) + L"\n" +
              PluginNames(plan);
    ShowPrompt(std::move(pr));
}

void HubScreen::BeginRemove(const PluginId& id) {
    Prompt pr;
    pr.kind = PromptKind::Remove;
    pr.target = id;
    pr.title = Tr(L"プラグインの削除");
    const std::wstring name = Utf8ToWide(app_.GetPlugins().DisplayName(id));
    const auto dependents = app_.GetPlugins().Dependents(id);
    if (dependents.empty()) {
        pr.body = TrF(L"「{}」を削除しますか？", name) + L"\n";
    } else {
        pr.body = TrF(L"「{}」は、以下のプラグインの前提として使われています。削除すると、これらは使えなくなります。", name) + L"\n";
        for (const Dependent& d : dependents) {
            pr.body += L"・" + Utf8ToWide(app_.GetPlugins().DisplayName(d.id));
            if (d.via) pr.body += L" " + TrF(L"({} を通して)", Utf8ToWide(app_.GetPlugins().DisplayName(*d.via)));
            pr.body += L"\n";
        }
        pr.body += L"\n" + Tr(L"本当に削除しますか？") + L"\n";
    }
    if (const int n = RecentProjectsUsing(id); n > 0) {
        pr.body += L"\n" + TrF(L"最近のプロジェクトのうち {} 個がこのプラグインを使っています (開くときにインストールを求められます)。", n);
    }
    ShowPrompt(std::move(pr));
}

void HubScreen::RunInstall(const std::vector<PluginId>& ids) {
    if (ids.empty()) return;
    // インストールするとプラグインを読み直す (Entries() を作り直す): その一覧を描いている
    // 最中は絶対にしない - フレームの後で、メッセージループから。
    Defer([this, ids] {
        std::string err;
        if (!app_.InstallPlugins(ids, &err)) ShowError(Utf8ToWide(err), {}, Tr(L"インストールできませんでした"));
        Invalidate();
    });
}

void HubScreen::DrawPluginPrompt(ui::Context& c) {
    auto& p = c.P();
    Prompt& pr = prompt_;
    const float lines = static_cast<float>(std::count(pr.body.begin(), pr.body.end(), L'\n') + 1);
    const float extra = pr.kind == PromptKind::Limited ? 36.f : 0.f;
    const Rect cr = w::BeginModal(c, 660, std::min(640.f, 150 + 21 * lines + extra), pr.title);
    ui::TextStyle ts = Style(13, theme::Text);
    ts.wrap = true;
    ts.v = ui::VAlign::Top;
    p.Text({cr.x, cr.y, cr.w, cr.h - 52 - extra}, pr.body, ts);

    const Rect right{cr.Right() - 170, cr.Bottom() - 36, 170, 36};
    const Rect left{right.x - 12 - 150, right.y, 150, 36};
    const bool enter = c.KeyPressed(ui::Key::Enter);
    const bool esc = c.KeyPressed(ui::Key::Escape);
    auto close = [&] { modal_ = Modal::None; };

    switch (pr.kind) {
        case PromptKind::Blocking:
            if (pr.toInstall.empty()) {
                if (w::Button(c, "pp.ok", right, L"OK", w::ButtonKind::Primary) || enter || esc) close();
                break;
            }
            if (w::Button(c, "pp.cancel", left, Tr(L"やめる")) || esc) {
                close();
            } else if (w::Button(c, "pp.install", right, Tr(L"インストールして開く"), w::ButtonKind::Primary) || enter) {
                close();
                Defer([this, ids = pr.toInstall, project = pr.project] {
                    std::string err;
                    if (app_.InstallPlugins(ids, &err)) {
                        app_.RequestOpenProject(project);
                    } else {
                        ShowError(Utf8ToWide(err), {}, Tr(L"インストールできませんでした"));
                    }
                });
            }
            break;
        case PromptKind::Limited: {
            w::Checkbox(c, "pp.hide", {cr.x, right.y - 44, cr.w, 28}, pr.hideAgain, Tr(L"この状態が変わるまで表示しない"));
            if (esc) {
                close();
            } else if (w::Button(c, "pp.open", right, Tr(L"このまま開く"), w::ButtonKind::Primary) || enter) {
                close();
                if (pr.hideAgain) {
                    app_.Settings().HideLimitedNotice(pr.limited);
                    app_.Settings().Save();
                }
                app_.RequestOpenProject(pr.project, true);
            } else if (!pr.toInstall.empty() && w::Button(c, "pp.install", left, Tr(L"インストール"))) {
                close();
                Defer([this, ids = pr.toInstall, project = pr.project] {
                    std::string err;
                    if (app_.InstallPlugins(ids, &err)) {
                        app_.RequestOpenProject(project);
                    } else {
                        ShowError(Utf8ToWide(err), {}, Tr(L"インストールできませんでした"));
                    }
                });
            }
            break;
        }
        case PromptKind::Install:
            if (w::Button(c, "pp.cancel", left, Tr(L"やめる")) || esc) {
                close();
            } else if (w::Button(c, "pp.install", right, Tr(L"インストール"), w::ButtonKind::Primary) || enter) {
                close();
                RunInstall(pr.toInstall);
            }
            break;
        case PromptKind::Create:
            // どちらでも新規プロジェクトのダイアログに戻る (そこで打った内容は残す)。
            if (w::Button(c, "pp.cancel", left, Tr(L"やめる")) || esc) {
                modal_ = Modal::NewProject;
            } else if (w::Button(c, "pp.install", right, Tr(L"インストールして作成"), w::ButtonKind::Primary) || enter) {
                Defer([this, ids = pr.toInstall] {
                    std::string err;
                    if (app_.InstallPlugins(ids, &err)) {
                        MakeNewProject();
                    } else {
                        ShowError(Utf8ToWide(err), {}, Tr(L"インストールできませんでした"));
                    }
                });
            }
            break;
        case PromptKind::Remove:
            // 安全な答えが初期値: Enter も Esc も「いいえ」の意味 (2026-09-30 に決めた)。
            if (w::Button(c, "pp.no", left, Tr(L"いいえ")) || enter || esc) {
                close();
            } else if (w::Button(c, "pp.remove", right, Tr(L"削除"), w::ButtonKind::Primary)) {
                close();
                Defer([this, id = pr.target] {
                    std::string err;
                    if (!app_.UninstallPlugin(id, &err)) ShowError(Utf8ToWide(err), {}, Tr(L"削除できませんでした"));
                    Invalidate();
                });
            }
            break;
    }
}

void HubScreen::DrawOptions(ui::Context& c, Rect area) {
    auto& p = c.P();
    p.Text(ui::TakeTop(area, 40), Tr(L"オプション"), Style(24, theme::Text, true));
    ui::TakeTop(area, 22);
    float y = area.y;
    const float labelW = 230, buttonW = 190;
    auto section = [&](std::wstring_view title) {
        p.Text({area.x, y, area.w, 24}, title, Style(15, theme::Text, true));
        y += 30;
        w::Separator(c, area.x, y - 4, area.w);
        y += 6;
    };
    auto note = [&](std::wstring_view text) {
        p.Text({area.x, y, area.w, 20}, text, Style(12, theme::TextFaint));
        y += 26;
    };
    // ディスク上の場所と、あればエクスプローラーで見せるボタン。
    auto place = [&](std::string_view id, std::wstring_view label, const fs::path& path) {
        const Rect row{area.x, y, area.w, 36};
        p.Text({row.x, row.y, labelW, row.h}, label, Style(13, theme::TextDim));
        p.Text({row.x + labelW, row.y, row.w - labelW - buttonW - 16, row.h}, path.native(), Style(12, theme::Text));
        std::error_code ec;
        const bool exists = fs::exists(path, ec);
        if (w::Button(c, id, {row.Right() - buttonW, row.y + 2, buttonW, row.h - 4}, exists ? Tr(L"エクスプローラーで表示") : Tr(L"まだありません"),
                      w::ButtonKind::Secondary, exists)) {
            platform::ShowInExplorer(path);
        }
        y += 42;
    };

    // ウィンドウが低いときはページがスクロールする。
    const Rect view{area.x - 4, area.y, area.w + 8, area.h};
    float& contentH = c.FloatState(c.MakeId("opt.contentH"), 900.f);
    const float off = w::BeginScroll(c, "opt.scroll", view, contentH);
    y -= off;
    const float top = y;

    // --- オプション本体: インスペクターがその型から描く (editor/project/Options.h)。
    section(Tr(L"設定"));
    Options& opts = app_.Settings().GetOptions();
    {
        // テーマ: 専用の画面で選ぶ (たくさんできる予定)。ここには今使っているものを出す。
        const Rect row{area.x, y, area.w, 36};
        p.Text({row.x, row.y, labelW, row.h}, Tr(L"色のテーマ"), Style(13, theme::TextDim));
        // (季節で切り替える: 今従っているテーマも)
        const theme::ThemeInfo& now = theme::FindTheme(opts.theme);
        const std::wstring name = now.followed.empty() ? Tr(now.name) : TrF(L"{} (いまは {})", Tr(now.name), Tr(now.followed));
        p.Text({row.x + labelW, row.y, row.w - labelW - buttonW - 16, row.h}, name, Style(13, theme::Text));
        if (w::Button(c, "opt.theme", {row.Right() - buttonW, row.y + 2, buttonW, row.h - 4}, Tr(L"テーマを変更…"), w::ButtonKind::Secondary)) {
            modal_ = Modal::Theme;
        }
        y += 46;
    }
    reflect::Unknowns& unknowns = app_.Settings().OptionsUnknowns();
    const inspector::Block block{&reflect::TypeOf<Options>(), &opts, &unknowns, {}, {}, {}};
    // オプションの数だけの高さ (インスペクターが描きながら測る): 自前のスクロールバーはない。
    const Rect box{area.x - 12, y, area.w + 12, std::max(3 * 30.f + 20, optionsInspector_.contentHeight)};
    if (auto edit = inspector::Draw(c, optionsInspector_, box, std::span(&block, 1))) {
        // プロジェクトのデータではない: すぐ反映し、hub.json に保存し、Undo はできない。
        reflect::SetFieldCommand change(reflect::TypeOf<Options>(), std::move(edit->path), std::move(edit->before), std::move(edit->after),
                                        [&opts, &unknowns] { return reflect::EditTarget{&opts, &unknowns}; }, std::move(edit->label));
        change.Do();
        app_.Settings().Save();
        app_.ApplyOptions();
        log::Info("オプションを変えました: {}", change.Label());
    }
    y += box.h;
    note(Tr(L"自動アップデートは、エディターを配布する仕組みができたら効きます。プラグインの更新は「プラグイン」のページで行います。"));
    y += 14;

    section(Tr(L"エディターの整合性"));
    p.Text({area.x, y, area.w - buttonW - 16, 36}, Tr(L"エンジンのファイルが欠けていないか・壊れていないかを調べます。入れたプラグインが古い・壊れているときは直せます。"),
           Style(13, theme::TextDim));
    if (w::Button(c, "opt.integrity", {area.Right() - buttonW, y + 2, buttonW, 32}, Tr(L"確認する"))) RunIntegrityCheck();
    y += 56;

    section(Tr(L"新しいプロジェクト"));
    place("opt.projects", Tr(L"既定の保存先"), paths::DefaultProjects());
    note(Tr(L"「新規作成」で最初から入っている場所です。作るときに「参照…」でほかの場所にもできます。"));
    y += 14;

    section(Tr(L"エンジンのファイル"));
    place("opt.installed", Tr(L"入れたプラグイン"), paths::InstalledPlugins());
    place("opt.engine", Tr(L"このエンジンが持つプラグイン"), paths::EnginePlugins());
    place("opt.log", Tr(L"ログ (editor.log)"), paths::LogFile());
    place("opt.hub", Tr(L"Hub の設定 (hub.json)"), paths::HubSettingsFile());
    y += 14;

    section(Tr(L"お知らせ"));
    const bool hidden = app_.Settings().HasHiddenNotice();
    p.Text({area.x, y, area.w - buttonW - 16, 36},
           hidden ? Tr(L"「この状態が変わるまで表示しない」にしたプラグインのお知らせがあります。") : Tr(L"表示しないことにしたお知らせはありません。"),
           Style(13, theme::TextDim));
    if (w::Button(c, "opt.notices", {area.Right() - buttonW, y + 2, buttonW, 32}, Tr(L"また表示する"), w::ButtonKind::Secondary, hidden)) {
        app_.Settings().ShowNoticesAgain();
        app_.Settings().Save();
        log::Info("表示しないことにしたお知らせを、また表示するようにしました");
    }
    y += 56;

    section(Tr(L"このエンジンについて"));
    p.Text({area.x, y, area.w, 22}, TrF(L"RM Engine  バージョン {}", std::wstring(kEngineVersionW)), Style(13, theme::TextDim));
    y += 26;
    p.Text({area.x, y, area.w, 22}, TrF(L"エンジンのフォルダ: {}", platform::EngineDirectory().native()), Style(12, theme::TextFaint));
    y += 34;

    contentH = y - top;
    w::EndScroll(c, "opt.scroll", view, contentH);
}

void HubScreen::DrawUpdates(ui::Context& c, Rect area) {
    auto& p = c.P();
    p.Text(ui::TakeTop(area, 40), Tr(L"アップデート情報"), Style(24, theme::Text, true));
    ui::TakeTop(area, 22);
    static const json::Value doc = [] {
        const auto bytes = platform::ResourceBytes(IDR_UPDATES);
        auto v = json::Parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        return v ? std::move(*v) : json::Value();
    }();
    const std::vector<UpdateEntry> entries = ParseUpdates(doc, lang::Current() == lang::Language::English);

    const Rect view{area.x - 4, area.y, area.w + 8, area.h};
    float& contentH = c.FloatState(c.MakeId("upd.contentH"), 600.f);
    const float off = w::BeginScroll(c, "upd.scroll", view, contentH);
    float y = area.y - off;
    const float top = y;
    if (entries.empty()) {
        p.Text({area.x, y, area.w, 24}, Tr(L"アップデート情報はありません。"), Style(13, theme::TextDim));
        y += 30;
    }
    ui::TextStyle item = Style(13, theme::Text);
    item.wrap = true;
    item.v = ui::VAlign::Top;
    for (const UpdateEntry& e : entries) {
        p.Text({area.x, y, area.w, 26}, TrF(L"バージョン {}  ({})", Utf8ToWide(e.version), Utf8ToWide(e.date)), Style(16, theme::Text, true));
        y += 32;
        w::Separator(c, area.x, y - 4, area.w);
        y += 8;
        for (const std::string& line : e.items) {
            const std::wstring text = Utf8ToWide(line);
            // 長い行は折り返す: おおまかに測る (これだけの DIP の文字ごとに 1 行)。
            const float width = area.w - 28;
            const float lines = std::max(1.f, std::ceil(p.TextWidth(text, 13) / std::max(1.f, width)));
            p.FillCircle(area.x + 8.5f, y + 10.5f, 2.5f, theme::Accent);
            p.Text({area.x + 22, y, width, 22 * lines}, text, item);
            y += 22 * lines + 6;
        }
        y += 22;
    }
    contentH = y - top;
    w::EndScroll(c, "upd.scroll", view, contentH);
}

void HubScreen::RunIntegrityCheck() {
    integrityStale_.clear();
    integrityText_.clear();
    integrityRunning_ = true;
    integrityDone_ = integrityTotal_ = 0;
    modal_ = Modal::Integrity;
    Invalidate();
    // 作業に必要なものをここにコピーする (UI スレッドで)。
    std::vector<PluginId> installed;
    for (const auto& e : app_.GetPlugins().Entries()) {
        if (e.installed) installed.push_back(e.status.id);
    }
    integrityWorker_.Run([this, installed, listFile = paths::FileList(), exeDir = platform::ExeDirectory(),
                          installedDir = paths::InstalledPlugins()](std::stop_token stop, const Worker::Post& post) {
        IntegrityResult r;
        auto bytes = platform::ReadFileBytes(listFile, nullptr);
        auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
        auto list = doc ? integrity::ParseFileList(*doc, nullptr) : std::nullopt;
        if (!list) {
            post([this, r] { FinishIntegrityCheck(r); });
            return;
        }
        r.listRead = true;
        r.files = list->size();
        // 進み具合: ときどき知らせる (ファイルごとにはしない)。
        size_t done = 0;
        auto last = std::chrono::steady_clock::now();
        auto counted = [&] {
            ++done;
            if (std::chrono::steady_clock::now() - last < std::chrono::milliseconds(100)) return;
            last = std::chrono::steady_clock::now();
            post([this, done, total = r.files] {
                integrityDone_ = done;
                integrityTotal_ = total;
                Invalidate();
            });
        };
        r.problems = integrity::CheckFiles(*list, [&](const std::string& rel) -> std::optional<std::string> {
            if (stop.stop_requested()) return std::nullopt;
            counted();
            return platform::Sha256File(exeDir / PathFromUtf8(rel));
        });
        // インストール済みのプラグインを、エンジンのコピーと比べる。
        r.stale = integrity::StalePlugins(*list, installed, [&](const PluginId& id, const std::string& rel) -> std::optional<std::string> {
            if (stop.stop_requested()) return std::nullopt;
            return platform::Sha256File(installedDir / PathFromUtf8(id.author) / PathFromUtf8(id.name) / PathFromUtf8(rel));
        });
        if (stop.stop_requested()) return;
        post([this, r = std::move(r)] { FinishIntegrityCheck(r); });
    });
}

void HubScreen::FinishIntegrityCheck(const IntegrityResult& r) {
    integrityRunning_ = false;
    Invalidate();
    if (!r.listRead) {
        integrityText_ = TrF(L"ファイルの一覧 ({}) を読めないので、確かめられません。エンジンをビルドし直すか、入れ直してください。", paths::FileList().native());
        log::Warn("整合性の確認: ファイルの一覧を読めません: {}", PathToUtf8(paths::FileList()));
        return;
    }
    const auto& problems = r.problems;
    integrityStale_ = r.stale;

    std::wstring text;
    if (problems.empty()) {
        text = TrF(L"エンジンのファイル {} 個は、すべてそろっていて壊れていません。", r.files);
    } else {
        text = TrF(L"エンジンのファイルに問題があります ({} 個)。エンジンをビルドし直すか、入れ直してください。", problems.size());
        const size_t shown = std::min<size_t>(problems.size(), 8);
        for (size_t i = 0; i < shown; ++i) {
            const auto& pr = problems[i];
            text += L"\n・" + Utf8ToWide(pr.path) + L"  " + (pr.problem == integrity::Problem::Missing ? Tr(L"(見つかりません)") : Tr(L"(中身が違います)"));
        }
        if (problems.size() > shown) text += TrF(L"\n… ほか {} 個", problems.size() - shown);
    }
    if (integrityStale_.empty()) {
        text += L"\n\n" + Tr(L"入れたプラグインも、エンジンのものと同じです。");
    } else {
        text += L"\n\n" + Tr(L"入れたプラグインのうち、エンジンのものと違う (古い・壊れている) もの:");
        for (const PluginId& id : integrityStale_) text += L"\n・" + Utf8ToWide(TrU8(app_.GetPlugins().DisplayName(id)));
    }
    integrityText_ = std::move(text);
    log::Info("整合性の確認: エンジンのファイルの問題 {} 個 / 違うプラグイン {} 個", problems.size(), integrityStale_.size());
}

void HubScreen::DrawThemePicker(ui::Context& c) {
    auto& p = c.P();
    const auto themes = theme::Themes();
    constexpr int kCols = 4;
    constexpr float kGap = 16, kCardH = 156, kLineGap = 36, kNoteH = 30, kButtonsH = 52;
    // カードの置き場所: 1 行に 4 枚。各グループ (組み込みの 3 つ、次に各テーマパック) は、
    // パックの名前を書いた線の下から新しい行を始める。
    struct Cell {
        int col, row, lines;  // lines = この上にグループの線がいくつあるか
    };
    std::vector<Cell> cells;
    int col = 0, row = 0, lines = 0;
    for (size_t i = 0; i < themes.size(); ++i) {
        if (i > 0 && themes[i].group != themes[i - 1].group) {
            if (col > 0) ++row;
            col = 0;
            ++lines;
        } else if (col == kCols) {
            col = 0;
            ++row;
        }
        cells.push_back({col++, row, lines});
    }
    const int rows = themes.empty() ? 0 : row + 1;
    const float gridH = rows * kCardH + std::max(0, rows - 1 - lines) * kGap + lines * kLineGap;
    const Rect cr = w::BeginModal(c, 900, 58 + kNoteH + gridH + 8 + kButtonsH + 20, Tr(L"色のテーマ"));
    p.Text({cr.x, cr.y, cr.w, 24}, Tr(L"選ぶとすぐに変わります。"), Style(13, theme::TextDim));

    // カード (全部が入らないほどウィンドウが低いと、グリッドがスクロールする)。
    const Rect view{cr.x - 4, cr.y + kNoteH, cr.w + 8, cr.h - kNoteH - kButtonsH};
    const float off = w::BeginScroll(c, "theme.scroll", view, gridH + 8);
    const float cardW = (cr.w - kGap * (kCols - 1)) / kCols;
    // 行の上端: その上にある行と、それぞれの後ろの隙間 - グループの終わりなら線の分の場所。
    auto rowTop = [&](const Cell& cell) {
        return view.y + 4 - off + cell.row * kCardH + (cell.row - cell.lines) * kGap + cell.lines * kLineGap;
    };
    Options& opts = app_.Settings().GetOptions();
    const theme::ThemeInfo& current = theme::FindTheme(opts.theme);
    for (size_t i = 0; i < themes.size(); ++i) {
        const theme::ThemeInfo& t = themes[i];
        const Cell& cell = cells[i];
        const float top = rowTop(cell);
        if (i > 0 && cell.lines != cells[i - 1].lines) {
            // パックの名前、そして線。
            const std::wstring pack = Tr(t.pack);
            const float nameW = pack.empty() ? 0.f : p.TextWidth(pack, 12) + 10.f;
            p.Text({cr.x, top - kLineGap, nameW, kLineGap - 6}, pack, Style(12, theme::TextDim));
            w::Separator(c, cr.x + nameW, top - kLineGap * 0.5f - 3, cr.w - nameW);
        }
        const Rect card{cr.x + cell.col * (cardW + kGap), top, cardW, kCardH};
        c.PushId(static_cast<uint64_t>(i));
        // 見本: 季節で切り替える = 春 夏 秋 冬 の色を並べる (区切りの線と名前は無し: 2026-11-26 ユーザーの希望)、
        // PC の設定に合わせる = 黒｜白、ほかは 1 つ。
        std::vector<w::ThemeSlice> slices;
        if (t.seasons.size() == 4) {
            for (size_t k = 0; k < 4; ++k) slices.push_back({&t.seasons[k], {}, false});
        } else if (t.palette) {
            slices.push_back({&*t.palette, {}});
        } else {
            slices = {{&theme::kDark, {}}, {&theme::kLight, {}}};
        }
        const bool clicked = w::ThemeCard(c, "card", card, slices, Tr(t.name), &t == &current).clicked;
        c.PopId();
        if (clicked && &t != &current) {
            // プロジェクトのデータではない: すぐ反映し、hub.json に保存し、Undo はできない (ほかのオプションと同じ)。
            opts.theme = t.id;
            app_.Settings().Save();
            app_.ApplyOptions();
            log::Info("色のテーマを変えました: {}", t.id);
        }
    }
    w::EndScroll(c, "theme.scroll", view, gridH + 8);

    const Rect ok{cr.Right() - 110, cr.Bottom() - 36, 110, 36};
    if (w::Button(c, "theme.ok", ok, Tr(L"閉じる"), w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Escape) ||
        c.KeyPressed(ui::Key::Enter)) {
        modal_ = Modal::None;
    }
}

void HubScreen::DrawIntegrity(ui::Context& c) {
    auto& p = c.P();
    const Rect cr = w::BeginModal(c, 640, 420, Tr(L"エディターの整合性"));
    ui::TextStyle ts = Style(13, theme::Text);
    ts.wrap = true;
    ts.v = ui::VAlign::Top;
    const Rect ok{cr.Right() - 110, cr.Bottom() - 36, 110, 36};
    if (integrityRunning_) {
        // Worker でハッシュを取る: 進み具合と、やめる方法。
        const std::wstring how = integrityTotal_ > 0 ? TrF(L"ファイルを確かめています… {} / {} 個", integrityDone_, integrityTotal_)
                                                     : Tr(L"ファイルを確かめています…");
        p.Text({cr.x, cr.y, cr.w, 40}, how, ts);
        if (integrityTotal_ > 0) {
            const Rect bar{cr.x, cr.y + 44, cr.w, 8};
            w::ProgressBar(c, bar, static_cast<float>(integrityDone_) / static_cast<float>(integrityTotal_));
        }
        if (w::Button(c, "int.stop", ok, Tr(L"やめる")) || c.KeyPressed(ui::Key::Escape)) {
            integrityWorker_.Stop();
            integrityRunning_ = false;
            modal_ = Modal::None;
        }
        return;
    }
    p.Text({cr.x, cr.y, cr.w, cr.h - 48}, integrityText_, ts);
    if (!integrityStale_.empty() &&
        w::Button(c, "int.fix", {ok.x - 12 - 220, ok.y, 220, 36}, Tr(L"プラグインを入れ直して直す"), w::ButtonKind::Primary)) {
        const std::vector<PluginId> ids = integrityStale_;
        modal_ = Modal::None;
        Defer([this, ids] {
            std::string err;
            if (!app_.InstallPlugins(ids, &err)) {
                ShowError(Utf8ToWide(err), {}, Tr(L"直せませんでした"));
            } else {
                RunIntegrityCheck();  // 結果をもう一度見せる
            }
        });
        return;
    }
    if (w::Button(c, "int.ok", ok, Tr(L"閉じる"), integrityStale_.empty() ? w::ButtonKind::Primary : w::ButtonKind::Secondary) ||
        c.KeyPressed(ui::Key::Escape) || (integrityStale_.empty() && c.KeyPressed(ui::Key::Enter))) {
        modal_ = Modal::None;
    }
}

void HubScreen::DrawRowMenu(ui::Context& c) {
    const Row* row = nullptr;
    for (const auto& r : rows_) {
        if (SamePath(r.file, rowMenuFile_)) row = &r;
    }
    if (!row) {
        rowMenu_.open = false;
        return;
    }
    const std::vector<w::MenuItem> items = {
        {Tr(L"開く"), L"Enter"},
        {Tr(L"名前を変更…"), L"F2", !row->missing},
        {Tr(L"エクスプローラーで表示"), L"", !row->missing},
        {L"", L"", true, true},
        {Tr(L"一覧から外す (ファイルは消しません)")},
    };
    const int picked = w::MenuPopup(c, rowMenu_, items, 280);
    if (picked == 0) {
        OpenRow(*row);
    } else if (picked == 1) {
        BeginRename(*row);
    } else if (picked == 2) {
        platform::ShowInExplorer(row->file);
    } else if (picked == 4) {
        app_.Settings().Remove(row->file);
        app_.Settings().Save();
        if (SamePath(selected_, row->file)) selected_.clear();
        Refresh();
    }
}

// 新規プロジェクトのダイアログ: 左にテンプレートの木 (設計 RME_PLATFORM 1-b-2: ノードは、
// そのテンプレートとその下にある全部のプラグインを必要とする)、右に選んだものが何で
// 何を必要とするか、そして名前と場所。
void HubScreen::DrawNewProject(ui::Context& c) {
    auto& p = c.P();
    const Plugins& plugins = app_.GetPlugins();
    const TemplateTree& tree = plugins.Templates();
    const Rect cr = w::BeginModal(c, 940, 620, Tr(L"新しいプロジェクト"));
    Rect body{cr.x, cr.y, cr.w, cr.h - 52};
    const Rect left = ui::TakeLeft(body, 310);
    ui::TakeLeft(body, 28);
    const size_t chosen = NewNode();

    // --- 木 (行は深さでインデントする。選ぶとプラグインを先にインストールするなら印を付ける)
    p.Text({left.x, left.y, left.w, 20}, Tr(L"テンプレート"), Style(12, theme::TextDim, true));
    const Rect list{left.x, left.y + 26, left.w, left.h - 26 - 26};
    w::DrawSurface(c, list, w::Surface::Panel, 6.f);
    const Rect view = list.Inset(4, 4, 4, 4);
    constexpr float kRowH = 40.f;
    const float content = kRowH * static_cast<float>(tree.nodes.size());
    const float off = w::BeginScroll(c, "np.tree", view, content);
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        const TemplateNode& n = tree.nodes[i];
        const Rect row{view.x, view.y + kRowH * static_cast<float>(i) - off, view.w, kRowH - 2};
        if (row.Bottom() < view.y || row.y > view.Bottom()) continue;
        c.PushId(static_cast<uint64_t>(i));
        const auto b = w::Selectable(c, "node", row, i == chosen);
        c.PopId();
        const float indent = 10.f + 22.f * static_cast<float>(n.depth);
        const bool hasChildren = i + 1 < tree.nodes.size() && tree.nodes[i + 1].parent == i;
        if (hasChildren) w::DrawIcon(c, {row.x + indent, row.y, 14, row.h}, w::Icon::ChevronDown, theme::TextFaint);
        const ProjectTemplate* t = tree.At(i);
        const bool needsInstall = t && !plugins.TemplateInstallPlan(i).empty();
        const float markW = needsInstall ? 92.f : 0.f;
        p.Text({row.x + indent + 18, row.y, row.w - indent - 18 - markW - 8, row.h}, Tr(Utf8ToWide(tree.Label(i))),
               Style(14, t ? theme::Text : theme::TextDim, t != nullptr));
        if (needsInstall) {
            p.Text({row.Right() - markW - 8, row.y, markW, row.h}, Tr(L"要インストール"), Style(11, theme::Warn, false, ui::HAlign::Right));
        }
        if (b.clicked) {
            newPath_ = n.path;
            newError_.clear();
        }
        if (b.doubleClicked && t) CreateNewProject();
    }
    w::EndScroll(c, "np.tree", view, content);
    p.Text({left.x, list.Bottom() + 6, left.w, 18}, Tr(L"テンプレートは、プラグインが持ってきます。"), Style(11, theme::TextFaint));

    // --- 名前と場所 (下)、その上に選んだテンプレート
    constexpr float kFieldsH = 24 + 46 + 24 + 44 + 26 + 40;
    DrawTemplateDetails(c, {body.x, body.y, body.w, body.h - kFieldsH - 8}, chosen);
    float y = body.Bottom() - kFieldsH;
    auto label = [&](std::wstring_view text) {
        p.Text({body.x, y, body.w, 20}, text, Style(12, theme::TextDim, true));
        y += 24;
    };
    label(Tr(L"プロジェクト名"));
    if (focusNewName_) {
        c.FocusText(c.MakeId("np.name"), Utf8ToWide(newName_), true);
        focusNewName_ = false;
    }
    if (w::TextField(c, "np.name", {body.x, y, body.w, 34}, newName_, Tr(L"プロジェクト名"))) newError_.clear();
    y += 46;

    label(Tr(L"保存先"));
    if (w::TextField(c, "np.location", {body.x, y, body.w - 104, 34}, newLocation_, Tr(L"フォルダ"))) newError_.clear();
    if (w::Button(c, "np.browse", {body.Right() - 96, y, 96, 34}, Tr(L"参照…"))) BrowseLocation();
    y += 44;

    const std::wstring target = Utf8ToWide(newLocation_) + L"\\" + Utf8ToWide(newName_);
    p.Text({body.x, y, body.w, 20}, TrF(L"作成されるフォルダ: {}", target), Style(12, theme::TextFaint));
    y += 26;
    if (!newError_.empty()) {
        ui::TextStyle es = Style(12, theme::Danger);
        es.wrap = true;
        es.v = ui::VAlign::Top;
        p.Text({body.x, y, body.w, body.Bottom() - y}, newError_, es);
    }

    const Rect create{cr.Right() - 120, cr.Bottom() - 36, 120, 36};
    const Rect cancel{create.x - 12 - 110, create.y, 110, 36};
    if (w::Button(c, "np.cancel", cancel, Tr(L"キャンセル")) || c.KeyPressed(ui::Key::Escape)) {
        modal_ = Modal::None;
        c.ClearFocus();
        return;
    }
    const bool can = tree.At(chosen) != nullptr;
    if (w::Button(c, "np.create", create, Tr(L"作成"), w::ButtonKind::Primary, can) || (can && c.KeyPressed(ui::Key::Enter))) {
        CreateNewProject();
    }
}

// 選んだノード: その名前と説明、使うプラグイン (と今の状態)、作る前に
// インストールする数。
void HubScreen::DrawTemplateDetails(ui::Context& c, const Rect& r, size_t node) {
    auto& p = c.P();
    const Plugins& plugins = app_.GetPlugins();
    const TemplateTree& tree = plugins.Templates();
    if (node >= tree.nodes.size()) return;
    float y = r.y;
    p.Text({r.x, y, r.w, 28}, Tr(Utf8ToWide(tree.Label(node))), Style(18, theme::Text, true));
    y += 34;
    ui::TextStyle wrap = Style(13, theme::TextDim);
    wrap.wrap = true;
    wrap.v = ui::VAlign::Top;
    const ProjectTemplate* t = tree.At(node);
    if (!t) {
        p.Text({r.x, y, r.w, 40}, Tr(L"名前だけの枝です。下のテンプレートを選んでください。"), wrap);
        return;
    }
    p.Text({r.x, y, r.w, 40}, Tr(Utf8ToWide(t->description)), wrap);
    y += 46;

    const std::vector<PluginId> needed = TemplatePlugins(tree, node);
    if (needed.empty()) {
        p.Text({r.x, y, r.w, 20}, Tr(L"プラグインは使いません。"), Style(12, theme::TextFaint));
        return;
    }
    const bool below = node + 1 < tree.nodes.size() && tree.nodes[node + 1].parent == node;
    p.Text({r.x, y, r.w, 20}, below ? Tr(L"使うプラグイン (この下のテンプレートの分も入ります)") : Tr(L"使うプラグイン"),
           Style(12, theme::TextDim, true));
    y += 24;
    std::vector<PluginId> unavailable;
    const std::vector<PluginId> plan = plugins.TemplateInstallPlan(node, &unavailable);
    // 必要なプラグイン、次に付いてくるもの (前提とコネクタ)。
    std::vector<PluginId> shown = needed;
    for (const PluginId& id : plan) {
        if (std::find(shown.begin(), shown.end(), id) == shown.end()) shown.push_back(id);
    }
    const float bottom = r.Bottom() - (plan.empty() ? 0.f : 26.f);
    for (size_t i = 0; i < shown.size(); ++i) {
        if (y + 42 > bottom && i + 1 < shown.size()) {
            p.Text({r.x + 12, y, r.w - 12, 20}, TrF(L"… ほか {} 個", shown.size() - i), Style(12, theme::TextFaint));
            break;
        }
        const PluginId& id = shown[i];
        const bool extra = i >= needed.size();
        p.Text({r.x + 12, y, r.w * 0.6f, 20}, L"・" + Utf8ToWide(plugins.DisplayName(id)) + (extra ? L"  " + Tr(L"(一緒に入る)") : L""),
               Style(12, extra ? theme::TextDim : theme::Text));
        std::wstring state;
        ui::Color ink = theme::TextDim;
        if (std::find(unavailable.begin(), unavailable.end(), id) != unavailable.end()) {
            state = Tr(L"このエンジンにありません");
            ink = theme::Danger;
        } else if (std::find(plan.begin(), plan.end(), id) != plan.end()) {
            state = Tr(L"インストールされていません");
            ink = theme::Warn;
        } else {
            state = Tr(L"インストール済み");
        }
        p.Text({r.x + r.w * 0.6f, y, r.w * 0.4f, 20}, state, Style(12, ink, false, ui::HAlign::Right));
        y += 21;
    }
    if (!plan.empty()) {
        p.Text({r.x, r.Bottom() - 22, r.w, 20}, TrF(L"作成の前に {} 個のプラグインをインストールします。", plan.size()),
               Style(12, theme::Warn, true));
    }
}

void HubScreen::DrawRename(ui::Context& c) {
    auto& p = c.P();
    const Rect cr = w::BeginModal(c, 640, 330, Tr(L"プロジェクトの名前を変更"));
    float y = cr.y;
    p.Text({cr.x, y, cr.w, 20}, TrF(L"「{}」の新しい名前", renameOld_), Style(12, theme::TextDim, true));
    y += 24;
    if (focusRename_) {
        c.FocusText(c.MakeId("rn.name"), Utf8ToWide(renameName_), true);
        focusRename_ = false;
    }
    if (w::TextField(c, "rn.name", {cr.x, y, cr.w, 34}, renameName_, Tr(L"プロジェクト名"))) renameError_.clear();
    y += 46;

    // ディスク上で変わるもの: .rmpf とその中の名前、そしてプロジェクト自身のものなら
    // フォルダ (その名前を付けている)。ユーザーのフォルダは名前を変えない。
    const fs::path oldFolder = renameFile_.parent_path();
    const ProjectRenamePlan plan = PlanProjectRename(renameFile_, WideToUtf8(renameOld_), renameName_);
    ui::TextStyle faint = Style(12, theme::TextFaint);
    if (plan.renameFolder) {
        p.Text({cr.x, y, cr.w, 20}, TrF(L"フォルダ: {}  →  {}", oldFolder.filename().native(), plan.folder.filename().native()), faint);
    } else {
        p.Text({cr.x, y, cr.w, 20}, TrF(L"フォルダ: {} (変えません。プロジェクトの名前のフォルダではないため)", oldFolder.filename().native()), faint);
    }
    y += 22;
    p.Text({cr.x, y, cr.w, 20}, TrF(L"ファイル: {}  →  {}", renameFile_.filename().native(), plan.file.filename().native()), faint);
    y += 22;
    p.Text({cr.x, y, cr.w, 20}, TrF(L"場所: {}", oldFolder.parent_path().native()), faint);
    y += 30;
    if (!renameError_.empty()) {
        ui::TextStyle es = Style(12, theme::Danger);
        es.wrap = true;
        es.v = ui::VAlign::Top;
        p.Text({cr.x, y, cr.w, cr.Bottom() - 44 - y}, renameError_, es);
    }

    const Rect ok{cr.Right() - 120, cr.Bottom() - 36, 120, 36};
    const Rect cancel{ok.x - 12 - 110, ok.y, 110, 36};
    if (w::Button(c, "rn.cancel", cancel, Tr(L"キャンセル")) || c.KeyPressed(ui::Key::Escape)) {
        modal_ = Modal::None;
        c.ClearFocus();
        return;
    }
    if (w::Button(c, "rn.ok", ok, Tr(L"変更"), w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter)) RunRename();
}

void HubScreen::DrawError(ui::Context& c) {
    auto& p = c.P();
    const Rect cr = w::BeginModal(c, 600, 280, errorTitle_);
    ui::TextStyle ts = Style(13, theme::Text);
    ts.wrap = true;
    ts.v = ui::VAlign::Top;
    p.Text({cr.x, cr.y, cr.w, cr.h - 48}, errorText_, ts);

    const Rect ok{cr.Right() - 110, cr.Bottom() - 36, 110, 36};
    bool close = w::Button(c, "err.ok", ok, L"OK", w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter) ||
                 c.KeyPressed(ui::Key::Escape);
    if (!errorRemovable_.empty() && w::Button(c, "err.remove", {ok.x - 12 - 150, ok.y, 150, 36}, Tr(L"一覧から外す"))) {
        app_.Settings().Remove(errorRemovable_);
        app_.Settings().Save();
        Refresh();
        close = true;
    }
    if (close) {
        modal_ = Modal::None;
        errorRemovable_.clear();
    }
}

}  // namespace rm::editor
