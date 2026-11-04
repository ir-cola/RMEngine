#include "editor/hub/HubScreen.h"

#include "base/FileName.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "platform/Shell.h"
#include "resource.h"

#include <algorithm>
#include <chrono>
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
        case FileNameError::Empty: return L"プロジェクト名を入力してください。";
        case FileNameError::TooLong: return L"プロジェクト名が長すぎます (64 文字まで)。";
        case FileNameError::InvalidChar: return L"使えない文字が入っています ( < > : \" / \\ | ? * )。";
        case FileNameError::ReservedName: return L"Windows が予約している名前なので使えません (CON / NUL / COM1 など)。";
        case FileNameError::LeadingSpace: return L"先頭に空白は使えません。";
        case FileNameError::TrailingDotOrSpace: return L"末尾にピリオドや空白は使えません。";
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
    d.captionRgb = 0x151618;
    if (!CreateScreenWindow(d, SW_SHOWNORMAL)) return false;
    log::Info("Hub を開きました");
    return true;
}

void HubScreen::OnCreated() {
    logo_ = platform::LoadBitmapFromMemory(app_.Gfx(), target_.Context(), platform::ResourceBytes(IDR_LOGO_HUB));
    Refresh();
}

void HubScreen::OnCloseRequested() { app_.RequestQuit(); }

void HubScreen::ShowError(std::wstring message, fs::path removableFile) {
    errorText_ = std::move(message);
    errorRemovable_ = std::move(removableFile);
    modal_ = Modal::Error;
    Invalidate();
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

void HubScreen::Refresh() {
    rows_.clear();
    for (const auto& r : app_.Settings().Recent()) {
        Row row;
        row.file = r.file;
        row.folder = r.file.parent_path().native();
        std::error_code ec;
        row.missing = !fs::exists(r.file, ec);
        if (row.missing) {
            row.name = r.file.stem().native();
            row.modifiedSort = 0;
            row.modifiedText = L"見つかりません";
        } else {
            std::string err;
            auto p = LoadProject(r.file, &err);
            row.name = p ? Utf8ToWide(p->Name()) : r.file.stem().native();
            row.modifiedSort = FileModifiedUnix(r.file);
            row.modifiedText = FormatLocalTime(row.modifiedSort);
        }
        rows_.push_back(std::move(row));
    }
    SortRows();
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
// Actions
// ---------------------------------------------------------------------------

void HubScreen::OpenRow(const Row& row) {
    if (row.missing) {
        ShowError(L"プロジェクトが見つかりません。移動または削除された可能性があります。\n\n" + row.file.native(), row.file);
        return;
    }
    app_.RequestOpenProject(row.file);
}

void HubScreen::BrowseOpen() {
    Defer([this] {
        auto file = platform::PickOpenFile(Hwnd(), L"プロジェクトを開く", L"RM Engine プロジェクト (*.rmpf)", L"*.rmpf");
        if (file) app_.RequestOpenProject(*file);
    });
}

void HubScreen::BeginNewProject() {
    fs::path loc = app_.Settings().LastLocation();
    if (loc.empty()) loc = platform::GetKnownFolder(platform::KnownFolder::Documents) / L"RM_Engine Projects";
    newLocation_ = PathToUtf8(loc);
    newName_ = "NewProject";
    std::error_code ec;
    for (int i = 2; fs::exists(loc / PathFromUtf8(newName_), ec) && i < 1000; ++i) newName_ = std::format("NewProject{}", i);
    newTemplate_ = 0;
    newError_.clear();
    focusNewName_ = true;
    modal_ = Modal::NewProject;
    Invalidate();
}

void HubScreen::BrowseLocation() {
    Defer([this] {
        auto folder = platform::PickFolder(Hwnd(), L"保存先のフォルダを選択", Utf8ToWide(newLocation_));
        if (folder) {
            ctx_.ClearFocus();  // the field must not write its old text back over the new value
            newLocation_ = WideToUtf8(*folder);
            newError_.clear();
        }
    });
}

void HubScreen::CreateNewProject() {
    const FileNameError ne = ValidateFileName(newName_);
    if (ne != FileNameError::None) {
        newError_ = NameErrorText(ne);
        return;
    }
    const fs::path location = PathFromUtf8(newLocation_);
    if (newLocation_.empty()) {
        newError_ = L"保存先を選んでください。";
        return;
    }
    if (!location.is_absolute()) {
        newError_ = L"保存先はドライブから始まるフォルダで指定してください (例: C:\\Users\\...)。";
        return;
    }
    const fs::path file = location / PathFromUtf8(newName_) / (PathFromUtf8(newName_).native() + kProjectExtension);
    if (file.native().size() > 240) {
        newError_ = L"保存先のパスが長すぎます。もっと浅いフォルダを選んでください。";
        return;
    }
    std::string err;
    auto project = CreateProject(location, newName_, ProjectTemplates()[newTemplate_], &err);
    if (!project) {
        newError_ = Utf8ToWide(err);
        return;
    }
    app_.Settings().SetLastLocation(location);
    app_.Settings().Save();
    modal_ = Modal::None;
    app_.RequestOpenProject(project->file);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void HubScreen::OnFrame(ui::Context& c, float width, float height) {
    auto& p = c.P();
    p.Clear(theme::Bg);
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
    } else {
        DrawPlugins(c, full.Inset(32, 28, 32, 28));
    }

    c.SetInputEnabled(true);
    if (rowMenu_.open) DrawRowMenu(c);
    if (modal_ == Modal::NewProject) DrawNewProject(c);
    if (modal_ == Modal::Error) DrawError(c);
}

void HubScreen::DrawSidebar(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.FillRect(r, theme::Sidebar);
    p.FillRect({r.Right() - 1, r.y, 1, r.h}, theme::Border);

    float y = r.y + 34;
    if (logo_) {
        const auto sz = logo_->GetSize();
        const float lw = r.w - 64;
        const float lh = lw * sz.height / sz.width;
        p.Image(logo_.Get(), {r.x + 32, y, lw, lh});
        y += lh + 40;
    } else {
        p.Text({r.x + 28, y, r.w - 56, 40}, L"RM Engine", Style(22, theme::Text, true));
        y += 72;
    }

    auto nav = [&](std::string_view id, std::wstring_view label, Page page) {
        const Rect item{r.x + 14, y, r.w - 28, 40};
        const auto b = c.ButtonBehavior(c.MakeId(id), item);
        const bool sel = page_ == page;
        if (sel || b.hovered) p.FillRoundRect(item, 6, sel ? theme::Hover : theme::Pressed);
        if (sel) p.FillRoundRect({item.x, item.y + 10, 3, item.h - 20}, 1.5f, theme::Accent);
        p.Text(item.Inset(18, 0, 10, 0), label, Style(14, sel ? theme::Text : theme::TextDim, sel));
        if (b.clicked) page_ = page;
        y += 44;
    };
    nav("nav.projects", L"プロジェクト", Page::Projects);
    nav("nav.plugins", L"プラグイン", Page::Plugins);

    p.Text({r.x + 28, r.Bottom() - 44, r.w - 56, 20}, std::format(L"バージョン {}", kEngineVersionW), Style(12, theme::TextFaint));
}

void HubScreen::DrawProjects(ui::Context& c, Rect area) {
    auto& p = c.P();
    const Rect header = ui::TakeTop(area, 40);
    p.Text(header, L"プロジェクト", Style(24, theme::Text, true));
    const Rect btnNew{header.Right() - 132, header.y + 2, 132, 36};
    const Rect btnOpen{btnNew.x - 12 - 112, header.y + 2, 112, 36};
    if (w::Button(c, "hub.new", btnNew, L"新規作成", w::ButtonKind::Primary)) BeginNewProject();
    if (w::Button(c, "hub.open", btnOpen, L"開く…")) BrowseOpen();

    ui::TakeTop(area, 18);
    const Rect searchRow = ui::TakeTop(area, 34);
    w::TextField(c, "hub.search", {searchRow.x, searchRow.y, std::min(360.f, searchRow.w), 34}, search_, L"プロジェクトを検索");
    ui::TakeTop(area, 16);

    // Table
    p.FillRoundRect(area, 8, theme::Panel);
    Rect table = area;
    const Rect head = ui::TakeTop(table, 38);
    const float menuW = 48, modW = 180;
    const float nameW = std::max(160.f, (table.w - menuW - modW) * 0.40f);
    const float pathW = std::max(80.f, table.w - menuW - modW - nameW);
    const float xName = table.x, xMod = xName + nameW, xPath = xMod + modW;

    auto headerCell = [&](std::string_view id, float x, float wdt, std::wstring_view label, int column) {
        const Rect cell{x, head.y, wdt, head.h};
        const auto b = c.ButtonBehavior(c.MakeId(id), cell);
        std::wstring text(label);
        if (sortColumn_ == column) text += sortAscending_ ? L"  ▲" : L"  ▼";
        p.Text(cell.Inset(18, 0, 8, 0), text, Style(12, b.hovered || sortColumn_ == column ? theme::Text : theme::TextDim, true));
        if (b.clicked) {
            if (sortColumn_ == column) {
                sortAscending_ = !sortAscending_;
            } else {
                sortColumn_ = column;
                sortAscending_ = column != 1;  // dates: newest first
            }
            SortRows();
        }
    };
    headerCell("col.name", xName, nameW, L"名前", 0);
    headerCell("col.mod", xMod, modW, L"更新日時", 1);
    headerCell("col.path", xPath, pathW, L"場所", 2);
    p.FillRect({table.x, head.Bottom() - 1, table.w, 1}, theme::Border);

    const std::vector<size_t> visible = FilteredRows();
    if (visible.empty()) {
        const std::wstring msg = rows_.empty() ? L"プロジェクトがありません。\n「新規作成」で作るか、「開く…」で既存のプロジェクト (.rmpf) を追加してください。"
                                               : L"検索に一致するプロジェクトはありません。";
        ui::TextStyle ts = Style(14, theme::TextDim, false, ui::HAlign::Center);
        ts.wrap = true;
        p.Text(table.Inset(40, 0, 40, 40), msg, ts);
        return;
    }

    const float rowH = 46;
    const float content = rowH * static_cast<float>(visible.size()) + 8;
    const Rect view = table.Inset(0, 4, 0, 4);
    const float off = w::BeginScroll(c, "hub.rows", view, content);
    for (size_t vi = 0; vi < visible.size(); ++vi) {
        const Row& row = rows_[visible[vi]];
        const Rect rr{view.x + 6, view.y + 4 + rowH * static_cast<float>(vi) - off, view.w - 12, rowH - 2};
        if (rr.Bottom() < view.y || rr.y > view.Bottom()) continue;
        c.PushId(WideToUtf8(row.file.native()));
        const auto b = c.ButtonBehavior(c.MakeId("row"), rr);
        const bool sel = SamePath(selected_, row.file);
        if (sel || b.hovered) p.FillRoundRect(rr, 6, sel ? theme::Selected : theme::Hover);

        p.Text({xName + 18, rr.y, nameW - 26, rr.h}, row.name, Style(14, row.missing ? theme::TextFaint : theme::Text, true));
        p.Text({xMod + 18, rr.y, modW - 26, rr.h}, row.modifiedText, Style(13, row.missing ? theme::Danger : theme::TextDim));
        p.Text({xPath + 18, rr.y, pathW - 26, rr.h}, row.folder, Style(12, theme::TextFaint));

        const Rect more{rr.Right() - 40, rr.y + 7, 32, rr.h - 14};
        if (w::Button(c, "more", more, L"…", w::ButtonKind::Ghost)) {
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

    if (!selected_.empty() && c.KeyPressed(ui::Key::Enter) && c.Focused() == 0) {
        for (const auto& row : rows_) {
            if (SamePath(row.file, selected_)) {
                OpenRow(row);
                break;
            }
        }
    }
}

void HubScreen::DrawPlugins(ui::Context& c, Rect area) {
    auto& p = c.P();
    p.Text(ui::TakeTop(area, 40), L"プラグイン", Style(24, theme::Text, true));
    ui::TakeTop(area, 18);
    const Rect box = ui::TakeTop(area, 150);
    p.FillRoundRect(box, 8, theme::Panel);
    p.Text({box.x + 24, box.y + 20, box.w - 48, 26}, L"インストールされているプラグインはありません", Style(15, theme::Text, true));
    ui::TextStyle ts = Style(13, theme::TextDim);
    ts.wrap = true;
    ts.v = ui::VAlign::Top;
    p.Text({box.x + 24, box.y + 56, box.w - 48, box.h - 70},
           L"ゲーム・イラスト・作曲・3D モデルなどの機能は、プラグインとして追加します。\n"
           L"ここでプラグインのインストール・削除・再インストールを行えるようにする予定です。",
           ts);
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
        {L"開く", L"Enter"},
        {L"エクスプローラーで表示", L"", !row->missing},
        {L"", L"", true, true},
        {L"一覧から外す (ファイルは消しません)"},
    };
    const int picked = w::MenuPopup(c, rowMenu_, items, 280);
    if (picked == 0) {
        OpenRow(*row);
    } else if (picked == 1) {
        platform::ShowInExplorer(row->file);
    } else if (picked == 3) {
        app_.Settings().Remove(row->file);
        app_.Settings().Save();
        if (SamePath(selected_, row->file)) selected_.clear();
        Refresh();
    }
}

void HubScreen::DrawNewProject(ui::Context& c) {
    auto& p = c.P();
    const Rect cr = w::BeginModal(c, 640, 520, L"新しいプロジェクト");
    float y = cr.y;
    auto label = [&](std::wstring_view text) {
        p.Text({cr.x, y, cr.w, 20}, text, Style(12, theme::TextDim, true));
        y += 24;
    };

    label(L"テンプレート");
    const auto& templates = ProjectTemplates();
    for (size_t i = 0; i < templates.size(); ++i) {
        const Rect card{cr.x, y, cr.w, 60};
        c.PushId(static_cast<uint64_t>(i));
        const auto b = c.ButtonBehavior(c.MakeId("tmpl"), card);
        c.PopId();
        const bool sel = newTemplate_ == i;
        p.FillRoundRect(card, 6, sel ? theme::Selected : b.hovered ? theme::Hover : theme::Field);
        p.StrokeRoundRect(card, 6, sel ? theme::Accent : theme::Border);
        p.Text({card.x + 16, card.y + 8, card.w - 32, 22}, templates[i].name, Style(14, theme::Text, true));
        p.Text({card.x + 16, card.y + 31, card.w - 32, 20}, templates[i].description, Style(12, theme::TextDim));
        if (b.clicked) newTemplate_ = i;
        y += 66;
    }
    p.Text({cr.x, y, cr.w, 18}, L"ゲーム・イラストなどのテンプレートは、プラグインを追加すると増えます。", Style(11, theme::TextFaint));
    y += 30;

    label(L"プロジェクト名");
    if (focusNewName_) {
        c.FocusText(c.MakeId("np.name"), Utf8ToWide(newName_), true);
        focusNewName_ = false;
    }
    if (w::TextField(c, "np.name", {cr.x, y, cr.w, 34}, newName_, L"プロジェクト名")) newError_.clear();
    y += 46;

    label(L"保存先");
    if (w::TextField(c, "np.location", {cr.x, y, cr.w - 104, 34}, newLocation_, L"フォルダ")) newError_.clear();
    if (w::Button(c, "np.browse", {cr.Right() - 96, y, 96, 34}, L"参照…")) BrowseLocation();
    y += 44;

    const std::wstring target = Utf8ToWide(newLocation_) + L"\\" + Utf8ToWide(newName_);
    p.Text({cr.x, y, cr.w, 20}, L"作成されるフォルダ: " + target, Style(12, theme::TextFaint));
    y += 26;
    if (!newError_.empty()) {
        ui::TextStyle es = Style(12, theme::Danger);
        es.wrap = true;
        es.v = ui::VAlign::Top;
        p.Text({cr.x, y, cr.w, cr.Bottom() - 44 - y}, newError_, es);
    }

    const Rect create{cr.Right() - 120, cr.Bottom() - 36, 120, 36};
    const Rect cancel{create.x - 12 - 110, create.y, 110, 36};
    if (w::Button(c, "np.cancel", cancel, L"キャンセル") || c.KeyPressed(ui::Key::Escape)) {
        modal_ = Modal::None;
        c.ClearFocus();
        return;
    }
    if (w::Button(c, "np.create", create, L"作成", w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter)) {
        CreateNewProject();
    }
}

void HubScreen::DrawError(ui::Context& c) {
    auto& p = c.P();
    const Rect cr = w::BeginModal(c, 600, 280, L"開けませんでした");
    ui::TextStyle ts = Style(13, theme::Text);
    ts.wrap = true;
    ts.v = ui::VAlign::Top;
    p.Text({cr.x, cr.y, cr.w, cr.h - 48}, errorText_, ts);

    const Rect ok{cr.Right() - 110, cr.Bottom() - 36, 110, 36};
    bool close = w::Button(c, "err.ok", ok, L"OK", w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter) ||
                 c.KeyPressed(ui::Key::Escape);
    if (!errorRemovable_.empty() && w::Button(c, "err.remove", {ok.x - 12 - 150, ok.y, 150, 36}, L"一覧から外す")) {
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
