#include "editor/shell/EditorScreen.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/widgets/Theme.h"

#include <algorithm>
#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

using ui::Rect;

namespace {

ui::TextStyle Style(float size, ui::Color color, bool bold = false, ui::HAlign h = ui::HAlign::Left) {
    ui::TextStyle s;
    s.size = size;
    s.color = color;
    s.bold = bold;
    s.h = h;
    return s;
}

#ifdef _DEBUG
// Debug builds only: an edit that changes nothing, to exercise "unsaved changes" end to end.
class TestCommand final : public ICommand {
public:
    void Do() override {}
    void Undo() override {}
    std::string Label() const override { return "テスト用の変更"; }
};
#endif

}  // namespace

EditorScreen::EditorScreen(App& app, Project project) : ScreenWindow(app), project_(std::move(project)) {
    // The most important wire of the editor: an edit that reaches the history marks the project unsaved.
    history_.SetOnModified([this] { SetDirty(true); });
}

EditorScreen::~EditorScreen() {
    if (logSink_) log::RemoveSink(logSink_);
}

bool EditorScreen::Open() {
    platform::WindowDesc d;
    d.title = Utf8ToWide(project_.Name()) + L" - RM Engine";
    d.widthDip = 1600;
    d.heightDip = 900;
    d.minWidthDip = 960;
    d.minHeightDip = 560;
    d.captionRgb = 0x151618;
    RefreshAssets();
    if (!CreateScreenWindow(d, SW_SHOWMAXIMIZED)) return false;
    logSink_ = log::AddSink([this](const log::Entry&) { Invalidate(); });
    log::Info("エディターを開きました: {}", project_.Name());
    UpdateTitle();
    return true;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

void EditorScreen::SetDirty(bool dirty) {
    if (dirty_ == dirty) return;
    dirty_ = dirty;
    UpdateTitle();
    Invalidate();
}

void EditorScreen::UpdateTitle() {
    SetTitle((dirty_ ? L"* " : L"") + Utf8ToWide(project_.Name()) + L" - RM Engine");
}

bool EditorScreen::Save() {
    std::string err;
    if (!SaveProject(project_, &err)) {
        log::Error("{}", err);
        modalText_ = Utf8ToWide(err);
        modal_ = Modal::Error;
        return false;
    }
    SetDirty(false);
    RefreshAssets();
    return true;
}

void EditorScreen::RefreshAssets() {
    assets_.clear();
    std::error_code ec;
    for (fs::directory_iterator it(project_.AssetsDir(), ec), end; !ec && it != end; it.increment(ec)) {
        assets_.push_back({it->path().filename().native(), it->is_directory(ec)});
    }
    std::sort(assets_.begin(), assets_.end(), [](const AssetEntry& a, const AssetEntry& b) {
        if (a.folder != b.folder) return a.folder;
        return a.name < b.name;
    });
}

void EditorScreen::OnCloseRequested() { RequestClose(CloseAction::ToHub); }

void EditorScreen::RequestClose(CloseAction action) {
    if (dirty_) {
        pendingClose_ = action;
        modal_ = Modal::UnsavedChanges;
        Invalidate();
        return;
    }
    FinishClose(action);
}

void EditorScreen::FinishClose(CloseAction action) {
    log::Info("エディターを閉じます: {}", project_.Name());
    if (action == CloseAction::Quit) {
        app_.RequestQuit();
    } else {
        app_.RequestHub();
    }
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

std::vector<w::MenuItem> EditorScreen::MenuItems(Menu m) const {
    switch (m) {
        case Menu::File:
            return {{L"保存", L"Ctrl+S"}, {L"", L"", true, true}, {L"Hub に戻る"}, {L"終了"}};
        case Menu::Edit:
            return {{L"元に戻す", L"Ctrl+Z", history_.CanUndo()}, {L"やり直し", L"Ctrl+Y", history_.CanRedo()}};
        case Menu::Plugins:
            return {{L"インストール済みのプラグイン: なし", L"", false}, {L"プラグインを追加… (準備中)", L"", false}};
        case Menu::Help:
            return {{L"RM Engine について"}};
        case Menu::Debug:
            return {{L"未保存の変更を作る (テスト用)"}};
        default:
            return {};
    }
}

void EditorScreen::RunMenu(Menu m, int index) {
    if (m == Menu::File) {
        if (index == 0) Save();
        if (index == 2) RequestClose(CloseAction::ToHub);
        if (index == 3) RequestClose(CloseAction::Quit);
    } else if (m == Menu::Edit) {
        if (index == 0) history_.Undo();
        if (index == 1) history_.Redo();
    } else if (m == Menu::Help) {
        modal_ = Modal::About;
    } else if (m == Menu::Debug) {
#ifdef _DEBUG
        history_.Push(std::make_unique<TestCommand>());
        log::Info("テスト用の変更を履歴に積みました");
#endif
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void EditorScreen::OnFrame(ui::Context& c, float width, float height) {
    auto& p = c.P();
    p.Clear(theme::Bg);
    const bool overlay = modal_ != Modal::None || menuPopup_.open;
    c.SetInputEnabled(!overlay);

    if (!overlay && c.Focused() == 0) {
        if (c.KeyPressed(ui::Key::S, true)) Save();
        if (c.KeyPressed(ui::Key::Z, true)) history_.Undo();
        if (c.KeyPressed(ui::Key::Y, true) || c.KeyPressed(ui::Key::Z, true, true)) history_.Redo();
    }

    Rect full{0, 0, width, height};
    const Rect menuBar = ui::TakeTop(full, 30);
    const Rect tabs = ui::TakeTop(full, 36);
    const Rect status = ui::TakeBottom(full, 24);
    DrawWorkspace(c, full);
    DrawSoftTabs(c, tabs);
    DrawStatusBar(c, status);
    DrawMenuBar(c, menuBar);

    c.SetInputEnabled(true);
    if (menuPopup_.open) DrawOpenMenu(c);
    DrawModal(c);
}

void EditorScreen::DrawMenuBar(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.FillRect(r, theme::Sidebar);
    static const wchar_t* kTitles[] = {L"ファイル", L"編集", L"プラグイン", L"ヘルプ", L"デバッグ"};
#ifdef _DEBUG
    const int count = static_cast<int>(Menu::Count);
#else
    const int count = static_cast<int>(Menu::Debug);
#endif
    float x = r.x + 6;
    for (int i = 0; i < count; ++i) {
        const float wdt = p.TextWidth(kTitles[i], 13) + 24;
        const Rect item{x, r.y + 3, wdt, r.h - 6};
        menuTitleRects_[i] = item;
        c.PushId(static_cast<uint64_t>(i));
        const auto b = c.ButtonBehavior(c.MakeId("menubar"), item);
        c.PopId();
        const bool open = menuPopup_.open && static_cast<int>(openMenu_) == i;
        if (open || b.hovered) p.FillRoundRect(item, 4, open ? theme::Hover : theme::Pressed);
        p.Text(item, kTitles[i], Style(13, theme::Text, false, ui::HAlign::Center));
        if (b.clicked) {
            openMenu_ = static_cast<Menu>(i);
            menuPopup_.OpenAt(item.x, r.Bottom());
        }
        x += wdt + 2;
    }
    for (int i = count; i < static_cast<int>(Menu::Count); ++i) menuTitleRects_[i] = {};
    p.FillRect({r.x, r.Bottom() - 1, r.w, 1}, theme::Border);
}

void EditorScreen::DrawOpenMenu(ui::Context& c) {
    // While a menu is open, hovering another menu title switches to it (like any desktop app).
    const auto& in = c.In();
    for (int i = 0; i < static_cast<int>(Menu::Count); ++i) {
        const Rect& t = menuTitleRects_[i];
        if (t.w > 0 && t.Contains(in.mouseX, in.mouseY) && static_cast<int>(openMenu_) != i) {
            openMenu_ = static_cast<Menu>(i);
            menuPopup_.OpenAt(t.x, t.Bottom() + 3);
            c.RequestRedraw();
        }
    }
    // A click on the title of the open menu closes it (MenuPopup treats it as "outside").
    const Menu m = openMenu_;
    const int picked = w::MenuPopup(c, menuPopup_, MenuItems(m), 260);
    if (picked >= 0) RunMenu(m, picked);
}

void EditorScreen::DrawSoftTabs(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.FillRect(r, theme::Bg);
    p.FillRect({r.x, r.Bottom() - 1, r.w, 1}, theme::Border);
    // No plugin provides a "software" yet, so there are no tabs to show.
    p.Text(r.Inset(16, 0, 16, 0), L"ソフトはまだありません — プラグインを追加すると、ここにソフト (ゲーム・イラスト・作曲 …) のタブが並びます",
           Style(12, theme::TextFaint));
}

void EditorScreen::DrawWorkspace(ui::Context& c, Rect r) {
    auto& p = c.P();
    r = r.Inset(4);
    const Rect bottom = ui::TakeBottom(r, std::min(240.f, r.h * 0.35f));
    const Rect left = ui::TakeLeft(r, 260);
    const Rect right = ui::TakeRight(r, 320);
    const Rect center = r;

    const Rect hierarchy = w::Panel(c, left.Inset(2), L"階層");
    ui::TextStyle hint = Style(12, theme::TextFaint);
    hint.wrap = true;
    hint.v = ui::VAlign::Top;
    p.Text(hierarchy.Inset(12, 10, 12, 10), L"ソフトを選ぶと、ここに中身 (ゲームならエンティティの親子) が並びます。", hint);

    const Rect inspector = w::Panel(c, right.Inset(2), L"インスペクター");
    p.Text(inspector.Inset(12, 10, 12, 10), L"何も選ばれていません。", hint);

    const Rect main = center.Inset(2);
    p.FillRoundRect(main, 6, theme::Panel);
    const float cy = main.y + main.h * 0.5f;
    p.Text({main.x, cy - 46, main.w, 30}, L"ソフトがまだありません", Style(20, theme::Text, true, ui::HAlign::Center));
    ui::TextStyle body = Style(13, theme::TextDim, false, ui::HAlign::Center);
    body.wrap = true;
    body.v = ui::VAlign::Top;
    p.Text({main.x + 40, cy - 6, main.w - 80, 80},
           L"ゲーム・イラスト・作曲・3D モデルなどの機能は、プラグインとして追加します。\n"
           L"プラグインを入れると上のタブにソフトが並び、ここにそのソフトのパネルが表示されます。",
           body);

    DrawBottomPanel(c, bottom.Inset(2));
}

void EditorScreen::DrawBottomPanel(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.FillRoundRect(r, 6, theme::Panel);
    Rect body = r;
    const Rect header = ui::TakeTop(body, 30);
    const wchar_t* names[] = {L"コンソール", L"アセット"};
    float x = header.x + 6;
    for (int i = 0; i < 2; ++i) {
        const float wdt = p.TextWidth(names[i], 12, true) + 28;
        const Rect tab{x, header.y + 3, wdt, header.h - 3};
        c.PushId(static_cast<uint64_t>(i));
        const auto b = c.ButtonBehavior(c.MakeId("bottomtab"), tab);
        c.PopId();
        const bool sel = bottomTab_ == i;
        if (sel) {
            p.FillRoundRect(tab, 4, theme::PanelHeader);
            p.FillRect({tab.x + 8, tab.Bottom() - 2, tab.w - 16, 2}, theme::Accent);
        } else if (b.hovered) {
            p.FillRoundRect(tab, 4, theme::Pressed);
        }
        p.Text(tab, names[i], Style(12, sel ? theme::Text : theme::TextDim, true, ui::HAlign::Center));
        if (b.clicked) bottomTab_ = i;
        x += wdt + 2;
    }
    p.FillRect({r.x, header.Bottom() - 1, r.w, 1}, theme::Border);
    if (bottomTab_ == 0) {
        DrawConsole(c, body);
    } else {
        DrawAssets(c, body);
    }
}

void EditorScreen::DrawConsole(ui::Context& c, Rect r) {
    auto& p = c.P();
    const std::vector<log::Entry> entries = log::Snapshot();
    const float lineH = 20;
    const float content = lineH * static_cast<float>(entries.size()) + 8;
    const uint64_t last = entries.empty() ? 0 : entries.back().seq;
    if (last != consoleSeenSeq_) {
        w::ScrollToEnd(c, "console");  // follow new output
        consoleSeenSeq_ = last;
    }
    const Rect view = r.Inset(0, 2, 0, 2);
    const float off = w::BeginScroll(c, "console", view, content);
    const size_t first = static_cast<size_t>(std::max(0.f, (off - 4) / lineH));
    for (size_t i = first; i < entries.size(); ++i) {
        const float y = view.y + 4 + lineH * static_cast<float>(i) - off;
        if (y > view.Bottom()) break;
        const auto& e = entries[i];
        const ui::Color col = e.level == log::Level::Error ? theme::Danger : e.level == log::Level::Warn ? theme::Warn : theme::Text;
        p.Text({view.x + 12, y, 70, lineH}, std::format(L"{:8.2f}", e.time), Style(12, theme::TextFaint));
        p.Text({view.x + 86, y, view.w - 100, lineH}, Utf8ToWide(e.text), Style(12, col));
    }
    w::EndScroll(c, "console", view, content);
}

void EditorScreen::DrawAssets(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.Text({r.x + 12, r.y + 4, r.w - 24, 22}, project_.AssetsDir().native(), Style(12, theme::TextFaint));
    const Rect view = r.Inset(0, 28, 0, 2);
    if (assets_.empty()) {
        p.Text(view.Inset(12, 4, 12, 4), L"Assets フォルダは空です。", Style(12, theme::TextDim));
        return;
    }
    const float lineH = 24;
    const float content = lineH * static_cast<float>(assets_.size()) + 4;
    const float off = w::BeginScroll(c, "assets", view, content);
    for (size_t i = 0; i < assets_.size(); ++i) {
        const float y = view.y + lineH * static_cast<float>(i) - off;
        if (y + lineH < view.y || y > view.Bottom()) continue;
        const auto& a = assets_[i];
        p.FillRoundRect({view.x + 14, y + 6, 14, 12}, 2, a.folder ? theme::Folder : theme::TextFaint);
        p.Text({view.x + 36, y, view.w - 50, lineH}, a.name, Style(12, theme::Text));
    }
    w::EndScroll(c, "assets", view, content);
}

void EditorScreen::DrawStatusBar(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.FillRect(r, theme::Sidebar);
    p.FillRect({r.x, r.y, r.w, 1}, theme::Border);
    p.Text(r.Inset(12, 0, 260, 0), project_.file.native(), Style(12, theme::TextFaint));
    if (dirty_) {
        p.Text(r.Inset(12, 0, 12, 0), L"● 未保存の変更があります", Style(12, theme::Warn, false, ui::HAlign::Right));
    } else {
        p.Text(r.Inset(12, 0, 12, 0), L"保存済み", Style(12, theme::TextDim, false, ui::HAlign::Right));
    }
}

void EditorScreen::DrawModal(ui::Context& c) {
    if (modal_ == Modal::None) return;
    auto& p = c.P();
    ui::TextStyle body = Style(13, theme::Text);
    body.wrap = true;
    body.v = ui::VAlign::Top;

    if (modal_ == Modal::UnsavedChanges) {
        const Rect cr = w::BeginModal(c, 600, 220, L"保存されていない変更があります");
        p.Text({cr.x, cr.y, cr.w, cr.h - 48},
               std::format(L"「{}」の変更を保存しますか？\n保存しないで閉じると、変更は失われます。", Utf8ToWide(project_.Name())), body);
        const Rect save{cr.Right() - 150, cr.Bottom() - 36, 150, 36};
        const Rect discard{save.x - 12 - 150, save.y, 150, 36};
        const Rect cancel{discard.x - 12 - 110, save.y, 110, 36};
        if (w::Button(c, "uc.cancel", cancel, L"キャンセル") || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
            pendingClose_ = CloseAction::None;
        } else if (w::Button(c, "uc.discard", discard, L"保存せずに閉じる")) {
            modal_ = Modal::None;
            log::Warn("保存せずに閉じました: {}", project_.Name());
            FinishClose(pendingClose_);
        } else if (w::Button(c, "uc.save", save, L"保存して閉じる", w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter)) {
            modal_ = Modal::None;
            if (Save()) FinishClose(pendingClose_);  // on failure Save() shows the error and the editor stays open
        }
        return;
    }

    if (modal_ == Modal::Error) {
        const Rect cr = w::BeginModal(c, 600, 260, L"エラー");
        p.Text({cr.x, cr.y, cr.w, cr.h - 48}, modalText_, body);
        if (w::Button(c, "err.ok", {cr.Right() - 110, cr.Bottom() - 36, 110, 36}, L"OK", w::ButtonKind::Primary) ||
            c.KeyPressed(ui::Key::Enter) || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
        }
        return;
    }

    if (modal_ == Modal::About) {
        const Rect cr = w::BeginModal(c, 520, 230, L"RM Engine について");
        p.Text({cr.x, cr.y, cr.w, cr.h - 48},
               std::format(L"RM Engine  バージョン {}\n\nゲーム・イラスト・作曲・3D モデルなどを、プラグインを足して 1 つのソフトで作るための制作環境。",
                           kEngineVersionW),
               body);
        if (w::Button(c, "about.ok", {cr.Right() - 110, cr.Bottom() - 36, 110, 36}, L"閉じる", w::ButtonKind::Primary) ||
            c.KeyPressed(ui::Key::Enter) || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
        }
    }
}

}  // namespace rm::editor
