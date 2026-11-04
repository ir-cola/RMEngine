#include "editor/shell/EditorScreen.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/plugin/Plugins.h"
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

// The "start" panel exists only while no software is installed.
constexpr char kStart[] = "core.start";

// <project>/UserSettings/Layout.json
//   { "kind": "rm_engine.layout", "formatVersion": 2,
//     "active": { "author": "rm_engine", "plugin": "game_production", "software": "game" },
//     "softwares": [ { "author": ..., "plugin": ..., "software": ..., "layout": { "main": ..., "floating": [...] } } ] }
// The workspace without a software has only "software": "core". Entries of softwares that are not
// installed now, and keys this version does not know, are written back unchanged.
constexpr char kLayoutKind[] = "rm_engine.layout";
constexpr int64_t kLayoutFormatVersion = 2;
constexpr char kCoreSoftware[] = "core";

dock::Layout CoreDefaultLayout() {
    using dock::Axis;
    namespace cp = core_panel;
    dock::Layout l;
    auto top = l.MakeSplit(Axis::Horizontal, 0.17f, l.MakeLeaf({cp::kHierarchy}),
                           l.MakeSplit(Axis::Horizontal, 0.78f, l.MakeLeaf({kStart}), l.MakeLeaf({cp::kInspector})));
    l.main = l.MakeSplit(Axis::Vertical, 0.72f, std::move(top), l.MakeLeaf({cp::kConsole, cp::kAssets}));
    return l;
}

json::Value WorkspaceKey(const SoftwareType* type) {
    json::Value k = json::Value::MakeObject();
    if (type) {
        k.Set("author", type->plugin.author);
        k.Set("plugin", type->plugin.name);
        k.Set("software", type->id);
    } else {
        k.Set("software", kCoreSoftware);
    }
    return k;
}

bool SameKey(const json::Value& entry, const json::Value& key) {
    for (const char* f : {"author", "plugin", "software"}) {
        if (entry.Get(f).AsString() != key.Get(f).AsString()) return false;
    }
    return true;
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
    BuildWorkspaces();
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
    for (auto& ws : workspaces_) {
        Workspace* w = ws.get();
        w->dock.onStructureChanged = [this, w] {
            if (w == &Active()) RequestSyncFloating();
        };
        w->dock.onRedrawAll = [this] { RedrawAll(); };
    }
    SyncFloating(false);  // panel windows saved last time come back without taking the focus
    logSink_ = log::AddSink([this](const log::Entry&) { RedrawAll(); });
    log::Info("エディターを開きました: {} ({})", project_.Name(), WideToUtf8(Active().Title()));
    UpdateTitle();
    return true;
}

void EditorScreen::Close() {
    CloseFloats();
    ScreenWindow::Close();
}

void EditorScreen::ShowError(std::wstring message) {
    modalText_ = std::move(message);
    modal_ = Modal::Error;
    Invalidate();
}

void EditorScreen::RedrawAll() {
    Invalidate();
    for (auto& f : floats_) f->Invalidate();
}

// ---------------------------------------------------------------------------
// Workspaces (one per software) and their layouts
// ---------------------------------------------------------------------------

dock::Layout EditorScreen::Workspace::DefaultLayout() const {
    return software ? software->DefaultLayout() : CoreDefaultLayout();
}

std::vector<dock::PanelType> EditorScreen::CorePanels(bool withStart) {
    auto panel = [this](const char* id, const wchar_t* title, void (EditorScreen::*draw)(ui::Context&, Rect)) {
        return dock::PanelType{id, title, [this, draw](ui::Context& c, const Rect& r) { (this->*draw)(c, r); }};
    };
    std::vector<dock::PanelType> list;
    list.push_back(panel(core_panel::kHierarchy, L"階層", &EditorScreen::DrawHierarchy));
    if (withStart) list.push_back(panel(kStart, L"スタート", &EditorScreen::DrawStart));
    list.push_back(panel(core_panel::kInspector, L"インスペクター", &EditorScreen::DrawInspector));
    list.push_back(panel(core_panel::kConsole, L"コンソール", &EditorScreen::DrawConsole));
    list.push_back(panel(core_panel::kAssets, L"アセット", &EditorScreen::DrawAssets));
    return list;
}

void EditorScreen::BuildWorkspaces() {
    for (const SoftwareType& type : app_.GetPlugins().Registry().Softwares()) {
        auto ws = std::make_unique<Workspace>(*this);
        ws->type = &type;
        ws->software = type.create(*ws);
        if (!ws->software) {
            log::Error("ソフト「{}」を作れませんでした ({})", WideToUtf8(type.title), type.plugin.Text());
            continue;
        }
        // The software's panels after the core ones; ids must not collide (they are saved in layouts).
        std::vector<dock::PanelType> panels = CorePanels(false);
        for (auto& pt : ws->software->Panels()) {
            const bool taken = pt.id.starts_with("core.") ||
                               std::any_of(panels.begin(), panels.end(), [&](const dock::PanelType& q) { return q.id == pt.id; });
            if (taken) {
                log::Error("ソフト「{}」のパネル ID「{}」は使えません (core. で始まるか、重なっています)", WideToUtf8(type.title), pt.id);
                continue;
            }
            panels.push_back(std::move(pt));
        }
        ws->dock.SetPanelTypes(std::move(panels));
        workspaces_.push_back(std::move(ws));
    }
    if (workspaces_.empty()) {
        auto ws = std::make_unique<Workspace>(*this);
        ws->dock.SetPanelTypes(CorePanels(true));
        workspaces_.push_back(std::move(ws));
    }
    // The most important wire of the editor: an edit that reaches any history marks the project unsaved.
    for (auto& ws : workspaces_) ws->history.SetOnModified([this] { SetDirty(true); });
}

void EditorScreen::SwitchSoftware(size_t index) {
    if (index >= workspaces_.size() || index == active_) return;
    // Outside the frame: the floating windows of the old software are destroyed, the new one's created.
    Defer([this, index] {
        if (index == active_) return;
        CloseFloats();
        ctx_.ClearFocus();
        active_ = index;
        SyncFloating(false);
        log::Info("ソフトを切り替えました: {}", WideToUtf8(Active().Title()));
    });
}

fs::path EditorScreen::LayoutFile() const { return project_.Root() / L"UserSettings" / L"Layout.json"; }

void EditorScreen::LoadLayout() {
    layoutDoc_ = json::Value::MakeObject();
    const fs::path file = LayoutFile();
    std::error_code ec;
    bool broken = false;
    if (fs::exists(file, ec)) {
        std::string err;
        auto bytes = platform::ReadFileBytes(file, &err);
        auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
        if (doc && doc->IsObject() && doc->Get("kind").AsString() == kLayoutKind) {
            layoutDoc_ = std::move(*doc);
            layoutReadOnly_ = layoutDoc_.Get("formatVersion").AsInt(0) > kLayoutFormatVersion;
            if (layoutReadOnly_) log::Warn("パネルの配置は新しい版の RM Engine で保存されています。この版では上書きしません");
        } else {
            broken = true;
        }
        // Format 1 kept { "softwares": { "core": <layout> } }.
        if (const json::Value* old = layoutDoc_.Find("softwares"); old && old->IsObject()) {
            json::Value list = json::Value::MakeArray();
            if (const json::Value* core = old->Find(kCoreSoftware)) {
                json::Value e = WorkspaceKey(nullptr);
                e.Set("layout", *core);
                list.Push(std::move(e));
            }
            layoutDoc_.Set("softwares", std::move(list));
        }
    }
    const json::Value& entries = layoutDoc_.Get("softwares");
    for (auto& ws : workspaces_) {
        const json::Value key = WorkspaceKey(ws->type);
        std::optional<dock::Layout> loaded;
        for (const auto& e : entries.Items()) {
            if (!SameKey(e, key)) continue;
            loaded = dock::Layout::FromJson(e.Get("layout"));
            if (!loaded) broken = true;
        }
        dock::Layout layout = loaded ? std::move(*loaded) : ws->DefaultLayout();
        layout.Sanitize(ws->dock.PanelIds());
        ws->dock.SetLayout(std::move(layout));
    }
    if (broken) log::Warn("パネルの配置を一部読めなかったので、最初の配置で開きます: {}", PathToUtf8(file));
    const json::Value& active = layoutDoc_.Get("active");
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        if (active.IsObject() && SameKey(active, WorkspaceKey(workspaces_[i]->type))) active_ = i;
    }
}

void EditorScreen::SaveLayout() {
    if (layoutReadOnly_) return;
    dock::Layout& current = Active().dock.GetLayout();
    for (auto& fw : floats_) {
        if (dock::Floating* f = current.FindFloating(fw->Id())) f->rect = fw->CurrentRect();
    }
    layoutDoc_.Set("kind", kLayoutKind);
    layoutDoc_.Set("formatVersion", kLayoutFormatVersion);
    layoutDoc_.Set("active", WorkspaceKey(Active().type));
    json::Value* entries = layoutDoc_.Find("softwares");
    if (!entries || !entries->IsArray()) entries = &layoutDoc_.Set("softwares", json::Value::MakeArray());
    for (auto& ws : workspaces_) {
        const json::Value key = WorkspaceKey(ws->type);
        json::Value* entry = nullptr;
        for (auto& e : entries->Items()) {
            if (SameKey(e, key)) entry = &e;
        }
        if (!entry) {
            entries->Push(key);
            entry = &entries->Items().back();
        }
        entry->Set("layout", ws->dock.GetLayout().ToJson());
    }

    const fs::path file = LayoutFile();
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::string err;
    if (!platform::WriteFileAtomic(file, json::Write(layoutDoc_), &err)) log::Warn("パネルの配置を保存できませんでした: {}", err);
}

void EditorScreen::ResetLayout() {
    dock::Layout layout = Active().DefaultLayout();
    layout.Sanitize(Active().dock.PanelIds());
    Active().dock.SetLayout(std::move(layout));
    log::Info("パネルの配置を最初の状態に戻しました ({})", WideToUtf8(Active().Title()));
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
    dock::Layout& layout = Active().dock.GetLayout();
    for (auto it = floats_.begin(); it != floats_.end();) {
        if (layout.FindFloating((*it)->Id())) {
            ++it;
            continue;
        }
        Active().dock.ForgetSite((*it)->Id());
        (*it)->Close();
        it = floats_.erase(it);
    }
    std::vector<uint32_t> failed;
    for (auto& f : layout.floating) {
        const std::wstring title = Active().dock.FloatingTitle(f.id);
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
        Active().dock.ForgetSite(f->Id());
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
    Active().dock.Draw(c, Rect{0, 0, width, height}.Inset(4), window.Id(), window.Hwnd(), window.Scale());
    Active().dock.DrawOverlay(c, window.Id());
    c.SetInputEnabled(true);
}

void EditorScreen::CloseFloatingWindow(uint32_t floatingId) { Active().dock.CloseFloating(floatingId); }

void EditorScreen::FloatingMoving(uint32_t floatingId, HWND hwnd) { Active().dock.WindowMoving(floatingId, hwnd); }

void EditorScreen::FloatingMoveEnded(uint32_t floatingId) {
    Active().dock.WindowMoveEnded(floatingId);
    // Still floating (not docked back): remember where the user put it.
    dock::Floating* f = Active().dock.GetLayout().FindFloating(floatingId);
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
            return {{L"元に戻す", L"Ctrl+Z", Active().history.CanUndo()}, {L"やり直し", L"Ctrl+Y", Active().history.CanRedo()}};
        case Menu::Window: {
            std::vector<w::MenuItem> items;
            for (const auto& t : Active().dock.PanelTypes()) {
                w::MenuItem it;
                it.label = t.title;
                it.checked = Active().dock.GetLayout().Contains(t.id);
                items.push_back(std::move(it));
            }
            items.push_back({L"", L"", true, true});
            items.push_back({L"パネルの配置を元に戻す"});
            return items;
        }
        case Menu::Plugins: {
            // One row per plugin, in the order of Plugins::Entries() (RunMenu relies on it).
            std::vector<w::MenuItem> items;
            for (const Plugins::Entry& e : app_.GetPlugins().Entries()) {
                w::MenuItem it;
                const std::wstring name = Utf8ToWide(e.status.displayName);
                if (e.installed) {
                    it.label = name + L"  (" + Utf8ToWide(e.status.id.Text()) + L")";
                    it.shortcut = e.status.state == PluginState::Enabled ? L"使用中" : L"⚠ 使えません";
                    it.enabled = false;  // information only
                } else {
                    it.label = L"「" + name + L"」をインストール…";
                    it.enabled = e.available;
                }
                items.push_back(std::move(it));
            }
            if (items.empty()) items.push_back({L"このエンジンにはプラグインがありません", L"", false});
            items.push_back({L"", L"", true, true});
            items.push_back({L"削除・再インストールは Hub の「プラグイン」で行います", L"", false});
            return items;
        }
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
        if (index == 0) Active().history.Undo();
        if (index == 1) Active().history.Redo();
    } else if (m == Menu::Window) {
        const auto& types = Active().dock.PanelTypes();
        if (index >= 0 && index < static_cast<int>(types.size())) {
            Active().dock.TogglePanel(types[static_cast<size_t>(index)].id);
        } else if (index == static_cast<int>(types.size()) + 1) {
            ResetLayout();
        }
    } else if (m == Menu::Plugins) {
        const auto& entries = app_.GetPlugins().Entries();
        if (index >= 0 && index < static_cast<int>(entries.size())) BeginInstallPlugin(entries[static_cast<size_t>(index)].status.id);
    } else if (m == Menu::Help) {
        modal_ = Modal::About;
    } else if (m == Menu::Debug) {
#ifdef _DEBUG
        Active().history.Push(std::make_unique<TestCommand>());
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
    Active().dock.Draw(c, full.Inset(4), 0, Hwnd(), Scale());
    DrawSoftTabs(c, tabs);
    DrawStatusBar(c, status);
    DrawMenuBar(c, menuBar);

    c.SetInputEnabled(!overlay);
    Active().dock.DrawOverlay(c, 0);
    c.SetInputEnabled(true);
    if (menuPopup_.open) DrawOpenMenu(c);
    DrawModal(c);
}

void EditorScreen::BeginInstallPlugin(const PluginId& id) {
    std::vector<PluginId> unavailable;
    installPlan_ = app_.GetPlugins().InstallPlan(id, &unavailable);
    installName_ = Utf8ToWide(app_.GetPlugins().DisplayName(id));
    if (!unavailable.empty()) {
        std::wstring names;
        for (const PluginId& u : unavailable) names += L"\n・" + Utf8ToWide(app_.GetPlugins().DisplayName(u));
        ShowError(L"「" + installName_ + L"」の前提プラグインがこのエンジンにありません。エンジンを新しい版にしてください。" + names);
        return;
    }
    if (installPlan_.empty()) return;
    modal_ = Modal::InstallPlugin;
}

void EditorScreen::HandleShortcuts(ui::Context& c) {
    // Shared by the editor window and the panel windows: Ctrl+S works wherever the focus is.
    if (c.Focused() != 0) return;
    if (c.KeyPressed(ui::Key::S, true)) Save();
    if (c.KeyPressed(ui::Key::Z, true)) Active().history.Undo();
    if (c.KeyPressed(ui::Key::Y, true) || c.KeyPressed(ui::Key::Z, true, true)) Active().history.Redo();
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
    const std::vector<w::MenuItem> items = MenuItems(m);
    // As wide as the longest row (label + shortcut), at least 260.
    float width = 260;
    for (const auto& it : items) {
        const float need = c.P().TextWidth(it.label, theme::FontSize) + c.P().TextWidth(it.shortcut, theme::FontSize) + 84;
        width = std::max(width, need);
    }
    const int picked = w::MenuPopup(c, menuPopup_, items, width);
    if (picked >= 0) RunMenu(m, picked);
}

void EditorScreen::DrawSoftTabs(ui::Context& c, Rect r) {
    auto& p = c.P();
    p.FillRect(r, theme::Bg);
    p.FillRect({r.x, r.Bottom() - 1, r.w, 1}, theme::Border);
    if (!workspaces_[0]->type) {
        // No plugin provides a "software", so there are no tabs to show.
        p.Text(r.Inset(16, 0, 16, 0), L"ソフトはまだありません — プラグインを追加すると、ここにソフト (ゲーム・イラスト・作曲 …) のタブが並びます",
               Style(12, theme::TextFaint));
        return;
    }
    float x = r.x + 8;
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        const std::wstring title = workspaces_[i]->Title();
        const float wdt = p.TextWidth(title, 13, true) + 36;
        const Rect tab{x, r.y + 4, wdt, r.h - 5};
        c.PushId(static_cast<uint64_t>(i));
        const auto b = c.ButtonBehavior(c.MakeId("software"), tab);
        c.PopId();
        const bool sel = i == active_;
        if (sel) {
            p.FillRoundRect(tab, 5, theme::Panel);
            p.FillRect({tab.x + 10, tab.Bottom() - 2, tab.w - 20, 2}, theme::Accent);
        } else if (b.hovered) {
            p.FillRoundRect(tab, 5, theme::Pressed);
        }
        p.Text(tab, title, Style(13, sel ? theme::Text : theme::TextDim, true, ui::HAlign::Center));
        if (b.clicked) SwitchSoftware(i);
        x += wdt + 4;
    }
}

void EditorScreen::DrawHierarchy(ui::Context& c, Rect r) {
    ui::TextStyle hint = Style(12, theme::TextFaint);
    hint.wrap = true;
    hint.v = ui::VAlign::Top;
    c.P().Text(r.Inset(12, 10, 12, 10), L"ここに中身 (ゲームならエンティティの親子) が並びます。", hint);
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

    if (modal_ == Modal::InstallPlugin) {
        std::wstring list;
        if (installPlan_.size() > 1) {
            list = L"\n前提のプラグインも一緒に入ります:";
            for (const PluginId& id : installPlan_) list += L"\n・" + Utf8ToWide(app_.GetPlugins().DisplayName(id));
        }
        const Rect cr = w::BeginModal(c, 620, 250 + 20 * static_cast<float>(installPlan_.size()), L"プラグインのインストール");
        p.Text({cr.x, cr.y, cr.w, cr.h - 48},
               L"「" + installName_ + L"」をインストールします。" + list +
                   L"\n\nプロジェクトを保存してから、このプロジェクトを開き直します。\n元に戻す (Undo) の履歴は消えます。",
               body);
        const Rect ok{cr.Right() - 180, cr.Bottom() - 36, 180, 36};
        if (w::Button(c, "ip.cancel", {ok.x - 12 - 110, ok.y, 110, 36}, L"やめる") || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
        } else if (w::Button(c, "ip.ok", ok, L"保存して開き直す", w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter)) {
            modal_ = Modal::None;
            // A failed save shows its error and the editor stays as it is (nothing is lost).
            if (Save()) app_.RequestInstallAndReopen(installPlan_, project_.file);
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
