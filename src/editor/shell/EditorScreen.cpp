#include "editor/shell/EditorScreen.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/shell/FloatWindow.h"
#include "editor/widgets/Theme.h"
#include "platform/Shell.h"

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

// The core panels (always there, whatever plugins are installed).
constexpr char kHierarchy[] = "core.hierarchy";
constexpr char kInspector[] = "core.inspector";
constexpr char kStart[] = "core.start";
constexpr char kConsole[] = "core.console";
constexpr char kAssets[] = "core.assets";

// Layouts are kept per software. Until a plugin adds one, the editor shows the core panels only.
constexpr char kCoreSoftware[] = "core";
constexpr char kLayoutKind[] = "rm_engine.layout";
constexpr int64_t kLayoutFormatVersion = 1;

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
    RegisterPanels();
}

EditorScreen::~EditorScreen() {
    if (logSink_) log::RemoveSink(logSink_);
    CloseFloats();
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
    LoadLayout();  // before the callbacks below: nothing to sync yet
    if (!CreateScreenWindow(d, SW_SHOWMAXIMIZED)) return false;
    dock_.onStructureChanged = [this] { RequestSyncFloating(); };
    dock_.onRedrawAll = [this] { RedrawAll(); };
    SyncFloating(false);  // panel windows saved last time come back without taking the focus
    logSink_ = log::AddSink([this](const log::Entry&) { RedrawAll(); });
    log::Info("エディターを開きました: {}", project_.Name());
    UpdateTitle();
    return true;
}

void EditorScreen::Close() {
    CloseFloats();
    ScreenWindow::Close();
}

void EditorScreen::RedrawAll() {
    Invalidate();
    for (auto& f : floats_) f->Invalidate();
}

// ---------------------------------------------------------------------------
// Panels and layout
// ---------------------------------------------------------------------------

void EditorScreen::RegisterPanels() {
    auto panel = [this](const char* id, const wchar_t* title, void (EditorScreen::*draw)(ui::Context&, Rect)) {
        return dock::PanelType{id, title, [this, draw](ui::Context& c, const Rect& r) { (this->*draw)(c, r); }};
    };
    dock_.SetPanelTypes({
        panel(kHierarchy, L"階層", &EditorScreen::DrawHierarchy),
        panel(kStart, L"スタート", &EditorScreen::DrawStart),
        panel(kInspector, L"インスペクター", &EditorScreen::DrawInspector),
        panel(kConsole, L"コンソール", &EditorScreen::DrawConsole),
        panel(kAssets, L"アセット", &EditorScreen::DrawAssets),
    });
}

dock::Layout EditorScreen::DefaultLayout() const {
    using dock::Axis;
    dock::Layout l;
    auto top = l.MakeSplit(Axis::Horizontal, 0.17f, l.MakeLeaf({kHierarchy}),
                           l.MakeSplit(Axis::Horizontal, 0.78f, l.MakeLeaf({kStart}), l.MakeLeaf({kInspector})));
    l.main = l.MakeSplit(Axis::Vertical, 0.72f, std::move(top), l.MakeLeaf({kConsole, kAssets}));
    return l;
}

fs::path EditorScreen::LayoutFile() const { return project_.Root() / L"UserSettings" / L"Layout.json"; }

void EditorScreen::LoadLayout() {
    layoutDoc_ = json::Value::MakeObject();
    std::optional<dock::Layout> loaded;
    const fs::path file = LayoutFile();
    std::error_code ec;
    if (fs::exists(file, ec)) {
        std::string err;
        auto bytes = platform::ReadFileBytes(file, &err);
        auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
        if (doc && doc->IsObject() && doc->Get("kind").AsString() == kLayoutKind) {
            layoutDoc_ = std::move(*doc);
            layoutReadOnly_ = layoutDoc_.Get("formatVersion").AsInt(0) > kLayoutFormatVersion;
            if (const json::Value* soft = layoutDoc_.Get("softwares").Find(kCoreSoftware)) loaded = dock::Layout::FromJson(*soft);
        }
        if (!loaded) log::Warn("パネルの配置を読めなかったので、最初の配置で開きます: {}", PathToUtf8(file));
        if (layoutReadOnly_) log::Warn("パネルの配置は新しい版の RM Engine で保存されています。この版では上書きしません");
    }
    dock::Layout layout = loaded ? std::move(*loaded) : DefaultLayout();
    layout.Sanitize(dock_.PanelIds());
    dock_.SetLayout(std::move(layout));
}

void EditorScreen::SaveLayout() {
    if (layoutReadOnly_) return;
    dock::Layout& layout = dock_.GetLayout();
    for (auto& fw : floats_) {
        if (dock::Floating* f = layout.FindFloating(fw->Id())) f->rect = fw->CurrentRect();
    }
    layoutDoc_.Set("kind", kLayoutKind);
    layoutDoc_.Set("formatVersion", kLayoutFormatVersion);
    json::Value* softwares = layoutDoc_.Find("softwares");
    if (!softwares || !softwares->IsObject()) softwares = &layoutDoc_.Set("softwares", json::Value::MakeObject());
    softwares->Set(kCoreSoftware, layout.ToJson());

    const fs::path file = LayoutFile();
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::string err;
    if (!platform::WriteFileAtomic(file, json::Write(layoutDoc_), &err)) log::Warn("パネルの配置を保存できませんでした: {}", err);
}

void EditorScreen::ResetLayout() {
    dock::Layout layout = DefaultLayout();
    layout.Sanitize(dock_.PanelIds());
    dock_.SetLayout(std::move(layout));
    log::Info("パネルの配置を最初の状態に戻しました");
}

void EditorScreen::RequestSyncFloating() {
    // Windows are created and destroyed from the message loop, never inside a frame or a
    // window procedure of the window being destroyed.
    if (syncQueued_) return;
    syncQueued_ = true;
    Defer([this] { SyncFloating(true); });
}

void EditorScreen::SyncFloating(bool activate) {
    syncQueued_ = false;
    dock::Layout& layout = dock_.GetLayout();
    for (auto it = floats_.begin(); it != floats_.end();) {
        if (layout.FindFloating((*it)->Id())) {
            ++it;
            continue;
        }
        dock_.ForgetSite((*it)->Id());
        (*it)->Close();
        it = floats_.erase(it);
    }
    std::vector<uint32_t> failed;
    for (auto& f : layout.floating) {
        const std::wstring title = dock_.FloatingTitle(f.id);
        auto it = std::find_if(floats_.begin(), floats_.end(), [&](const auto& w) { return w->Id() == f.id; });
        if (it == floats_.end()) {
            auto fw = std::make_unique<FloatWindow>(app_, *this, f.id);
            if (!fw->Open(f.rect, Hwnd(), title, activate)) {
                failed.push_back(f.id);
                continue;
            }
            f.rect = fw->CurrentRect();  // may differ: a saved position on a monitor that is gone
            floats_.push_back(std::move(fw));
            continue;
        }
        FloatWindow& fw = **it;
        const dock::ScreenRect& a = fw.Applied();
        if (a.x != f.rect.x || a.y != f.rect.y || a.w != f.rect.w || a.h != f.rect.h) fw.MoveTo(f.rect);
        fw.SetTitle(title);
    }
    for (const uint32_t id : failed) {
        log::Error("パネルのウィンドウを作れませんでした。中のパネルは閉じます");
        layout.CloseFloating(id);
    }
    RedrawAll();
}

void EditorScreen::CloseFloats() {
    for (auto& f : floats_) {
        dock_.ForgetSite(f->Id());
        f->Close();
    }
    floats_.clear();
}

void EditorScreen::DrawFloating(ui::Context& c, float width, float height, FloatWindow& window) {
    c.P().Clear(theme::Bg);
    // While the editor shows a dialog, the panel windows only show.
    const bool blocked = modal_ != Modal::None;
    c.SetInputEnabled(!blocked);
    if (!blocked) HandleShortcuts(c);
    dock_.Draw(c, Rect{0, 0, width, height}.Inset(4), window.Id(), window.Hwnd(), window.Scale());
    dock_.DrawOverlay(c, window.Id());
    c.SetInputEnabled(true);
}

void EditorScreen::CloseFloatingWindow(uint32_t floatingId) { dock_.CloseFloating(floatingId); }

void EditorScreen::FloatingMoving(uint32_t floatingId, HWND hwnd) { dock_.WindowMoving(floatingId, hwnd); }

void EditorScreen::FloatingMoveEnded(uint32_t floatingId) {
    dock_.WindowMoveEnded(floatingId);
    // Still floating (not docked back): remember where the user put it.
    dock::Floating* f = dock_.GetLayout().FindFloating(floatingId);
    for (auto& fw : floats_) {
        if (f && fw->Id() == floatingId) f->rect = fw->CurrentRect();
    }
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
    SaveLayout();
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
    SaveLayout();
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
        case Menu::Window: {
            std::vector<w::MenuItem> items;
            for (const auto& t : dock_.PanelTypes()) {
                w::MenuItem it;
                it.label = t.title;
                it.checked = dock_.GetLayout().Contains(t.id);
                items.push_back(std::move(it));
            }
            items.push_back({L"", L"", true, true});
            items.push_back({L"パネルの配置を元に戻す"});
            return items;
        }
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
    } else if (m == Menu::Window) {
        const auto& types = dock_.PanelTypes();
        if (index >= 0 && index < static_cast<int>(types.size())) {
            dock_.TogglePanel(types[static_cast<size_t>(index)].id);
        } else if (index == static_cast<int>(types.size()) + 1) {
            ResetLayout();
        }
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

    if (!overlay) HandleShortcuts(c);

    Rect full{0, 0, width, height};
    const Rect menuBar = ui::TakeTop(full, 30);
    const Rect tabs = ui::TakeTop(full, 36);
    const Rect status = ui::TakeBottom(full, 24);
    dock_.Draw(c, full.Inset(4), 0, Hwnd(), Scale());
    DrawSoftTabs(c, tabs);
    DrawStatusBar(c, status);
    DrawMenuBar(c, menuBar);

    c.SetInputEnabled(!overlay);
    dock_.DrawOverlay(c, 0);
    c.SetInputEnabled(true);
    if (menuPopup_.open) DrawOpenMenu(c);
    DrawModal(c);
}

void EditorScreen::HandleShortcuts(ui::Context& c) {
    // Shared by the editor window and the panel windows: Ctrl+S works wherever the focus is.
    if (c.Focused() != 0) return;
    if (c.KeyPressed(ui::Key::S, true)) Save();
    if (c.KeyPressed(ui::Key::Z, true)) history_.Undo();
    if (c.KeyPressed(ui::Key::Y, true) || c.KeyPressed(ui::Key::Z, true, true)) history_.Redo();
}

void EditorScreen::DrawMenuBar(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.FillRect(r, theme::Sidebar);
    static const wchar_t* kTitles[] = {L"ファイル", L"編集", L"ウィンドウ", L"プラグイン", L"ヘルプ", L"デバッグ"};
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

void EditorScreen::DrawHierarchy(ui::Context& c, Rect r) {
    ui::TextStyle hint = Style(12, theme::TextFaint);
    hint.wrap = true;
    hint.v = ui::VAlign::Top;
    c.P().Text(r.Inset(12, 10, 12, 10), L"ソフトを選ぶと、ここに中身 (ゲームならエンティティの親子) が並びます。", hint);
}

void EditorScreen::DrawInspector(ui::Context& c, Rect r) {
    ui::TextStyle hint = Style(12, theme::TextFaint);
    hint.wrap = true;
    hint.v = ui::VAlign::Top;
    c.P().Text(r.Inset(12, 10, 12, 10), L"何も選ばれていません。", hint);
}

void EditorScreen::DrawStart(ui::Context& c, Rect r) {
    auto& p = c.P();
    const float cy = r.y + r.h * 0.5f;
    p.Text({r.x, cy - 46, r.w, 30}, L"ソフトがまだありません", Style(20, theme::Text, true, ui::HAlign::Center));
    ui::TextStyle body = Style(13, theme::TextDim, false, ui::HAlign::Center);
    body.wrap = true;
    body.v = ui::VAlign::Top;
    p.Text({r.x + 40, cy - 6, r.w - 80, 80},
           L"ゲーム・イラスト・作曲・3D モデルなどの機能は、プラグインとして追加します。\n"
           L"プラグインを入れると上のタブにソフトが並び、ここにそのソフトのパネルが表示されます。",
           body);
}

void EditorScreen::DrawConsole(ui::Context& c, Rect r) {
    auto& p = c.P();
    const std::vector<log::Entry> entries = log::Snapshot();
    const float lineH = 20;
    const float content = lineH * static_cast<float>(entries.size()) + 8;
    const uint64_t last = entries.empty() ? 0 : entries.back().seq;
    auto seen = consoleSeenSeq_.try_emplace(&c, ~0ull).first;
    if (last != seen->second) {
        w::ScrollToEnd(c, "console");  // follow new output (and start at the end in a new window)
        seen->second = last;
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
        p.Text({view.x + 12, view.y + 4, view.w - 24, 22}, L"Assets フォルダは空です。", Style(12, theme::TextDim));
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
