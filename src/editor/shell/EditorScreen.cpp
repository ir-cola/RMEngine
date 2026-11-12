#include "editor/lang/Lang.h"
#include "editor/shell/EditorScreen.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/app/Paths.h"
#include "editor/plugin/Plugins.h"
#include "editor/shell/FloatWindow.h"
#include "editor/widgets/Theme.h"
#include "platform/Shell.h"

#include <algorithm>
#include <cmath>
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
//     "softwares": [ { "author": ..., "plugin": ..., "software": ..., "layout": { "main": ..., "floating": [...] },
//                      "view": { ... the software's own (Software::SaveView) },
//                      "named": [ { "name": ..., "layout": {...} } ] (layouts saved under a name: dock/NamedLayouts.h),
//                      "tree": { "collapsed": [...], "selected": [...] } (the 階層: tree::SaveState),
//                      "assets": { "thumbnail": 40 } (the アセット panel: the thumbnails' size in DIPs) } ] }
// The workspace without a software has only "software": "core". Entries of softwares that are not
// installed now, and keys this version does not know, are written back unchanged.
// (kLayoutKind / kLayoutFormatVersion: editor/project/Project.h - a new project's template writes it too.)
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
    assets_ = std::make_unique<AssetLibrary>(project_.AssetsDir());
    // The ids before the softwares read their data (a scene finds its prefabs by id). Only this first
    // look waits; every later one runs on a Worker (AssetLibrary).
    assets_->LookNow();
    thumbnails_ = std::make_unique<Thumbnails>(app_.Gfx(), app_.GetPlugins().Registry(), project_.AssetsDir());
    BuildWorkspaces();
    LoadSoftwareData();
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
    assets_->onChanged = [this] { RedrawAll(); };  // a look's result was put in place (from the Worker)
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
    // Queued: a log written on a worker thread reaches the windows on this (the UI) thread, from the
    // App's message loop (log::DeliverQueued) - RedrawAll touches the windows.
    logSink_ = log::AddSink([this](const log::Entry&) { RedrawAll(); }, log::Delivery::Queued);
    log::Info("エディターを開きました: {} ({})", project_.Name(), WideToUtf8(Active().Title()));
    UpdateTitle();
    SetTimer(Hwnd(), kAutoSaveTimer, 15000, nullptr);  // looks every 15 s; saves at the chosen interval
    return true;
}

void EditorScreen::OnLookChanged() {
    ScreenWindow::OnLookChanged();
    for (auto& f : floats_) f->OnLookChanged();
    UpdateTitle();
}

void EditorScreen::OnTimer(UINT_PTR id) {
    if (id != kAutoSaveTimer || !dirty_) return;
    // Never in the middle of something: a dialog, an open menu, typing, a drag (a button held).
    if (modal_ != Modal::None || menuPopup_.open || ctx_.Focused() != 0) return;
    if (GetKeyState(VK_LBUTTON) < 0 || GetKeyState(VK_RBUTTON) < 0 || GetKeyState(VK_MBUTTON) < 0) return;
    const double minutes = Minutes(app_.Settings().GetOptions().autoSave);
    if (platform::NowSeconds() - dirtySince_ < minutes * 60.0) return;
    log::Info("自動保存します ({} 分ごと)", minutes);
    if (!Save()) dirtySince_ = platform::NowSeconds();  // the error is shown; try again after another interval
}

void EditorScreen::Close() {
    if (Hwnd()) KillTimer(Hwnd(), kAutoSaveTimer);
    CloseFloats();
    ScreenWindow::Close();
    if (lock_) lock_->Release();  // another RM Engine may open the project now
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

const PluginRegistry& EditorScreen::Workspace::Registry() const { return editor_.app_.GetPlugins().Registry(); }

platform::Gfx& EditorScreen::Workspace::Graphics() { return editor_.app_.Gfx(); }

bool EditorScreen::Workspace::SetClipboard(const std::string& textUtf8) {
    return platform::SetClipboardText(editor_.Hwnd(), Utf8ToWide(textUtf8));
}

std::string EditorScreen::Workspace::Clipboard() { return WideToUtf8(platform::GetClipboardText(editor_.Hwnd())); }

fs::path EditorScreen::Workspace::AssetFile(const AssetRef& ref) const {
    const AssetLibrary& lib = *editor_.assets_;
    if (ref.Empty()) return {};
    if (asset::IsGuid(ref.guid)) {
        const asset::Entry* e = lib.Index().ByGuid(ref.guid);
        return e ? lib.FullPath(*e) : fs::path();
    }
    // Data from before references: a path inside the project ("Assets/Models/robot.glb").
    std::string rel = ref.guid;
    std::replace(rel.begin(), rel.end(), '\\', '/');
    if (rel.find("..") != std::string::npos || rel.find(':') != std::string::npos) return {};  // stays inside the project
    std::error_code ec;
    const fs::path p = editor_.project_.Root() / PathFromUtf8(rel);
    return fs::exists(p, ec) ? p : fs::path();
}

AssetRef EditorScreen::Workspace::AssetOf(const fs::path& file) {
    AssetLibrary& lib = *editor_.assets_;
    const fs::path rel = file.lexically_normal().lexically_relative(lib.Root().lexically_normal());
    if (rel.empty() || rel.native().starts_with(L"..")) return {};  // not inside Assets/
    const std::string path = PathToUtf8(rel.generic_wstring());
    // (a file just written - a prefab saved: it gets its id now, without waiting for a look)
    const std::string guid = lib.AdoptFile(path);
    return guid.empty() ? AssetRef{} : AssetRef{guid};
}

std::vector<PluginManifest> EditorScreen::Workspace::PluginManifests() const { return editor_.app_.GetPlugins().Available(); }

bool EditorScreen::ExportForCheck(const fs::path& outDir, std::string* error) {
    for (auto& ws : workspaces_) {
        if (ws->software && ws->software->CanExport()) return ws->software->ExportNow(outDir, error);
    }
    if (error) *error = TrU8("書き出せるソフトがありません (ゲーム制作のプラグインが要ります)");
    return false;
}

dock::Layout EditorScreen::Workspace::DefaultLayout() const {
    return software ? software->DefaultLayout() : CoreDefaultLayout();
}

std::vector<dock::PanelType> EditorScreen::CorePanels(bool withStart) {
    auto panel = [this](const char* id, const wchar_t* title, void (EditorScreen::*draw)(ui::Context&, Rect)) {
        return dock::PanelType{id, title, [this, draw](ui::Context& c, const Rect& r) { (this->*draw)(c, r); }};
    };
    std::vector<dock::PanelType> list;
    list.push_back(panel(core_panel::kHierarchy, TrKey(L"階層"), &EditorScreen::DrawHierarchy));
    if (withStart) list.push_back(panel(kStart, TrKey(L"スタート"), &EditorScreen::DrawStart));
    list.push_back(panel(core_panel::kInspector, TrKey(L"インスペクター"), &EditorScreen::DrawInspector));
    list.push_back(panel(core_panel::kConsole, TrKey(L"コンソール"), &EditorScreen::DrawConsole));
    list.push_back(panel(core_panel::kAssets, TrKey(L"アセット"), &EditorScreen::DrawAssets));
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

// .rmpf: "softwareData": [ { "author": ..., "plugin": ..., "software": ..., "data": {...} } ]
// Entries of softwares that are not installed now are kept as they are.
void EditorScreen::LoadSoftwareData() {
    const json::Value& entries = project_.doc.Get("softwareData");
    for (auto& ws : workspaces_) {
        if (!ws->software) continue;
        const json::Value key = WorkspaceKey(ws->type);
        const json::Value* data = nullptr;
        for (const auto& e : entries.Items()) {
            if (SameKey(e, key)) data = &e.Get("data");
        }
        static const json::Value kNone;
        ws->software->LoadData(data ? *data : kNone);
    }
}

void EditorScreen::StoreSoftwareData(std::vector<ProjectFileWrite>& files) {
    json::Value* entries = project_.doc.Find("softwareData");
    for (auto& ws : workspaces_) {
        if (!ws->software) continue;
        const json::Value key = WorkspaceKey(ws->type);
        json::Value* entry = nullptr;
        if (entries) {
            for (auto& e : entries->Items()) {
                if (SameKey(e, key)) entry = &e;
            }
        }
        json::Value data = entry ? entry->Get("data") : json::Value::MakeObject();
        if (!ws->software->SaveData(data, files)) continue;
        if (!entries || !entries->IsArray()) entries = &project_.doc.Set("softwareData", json::Value::MakeArray());
        if (!entry) {
            entries->Push(key);
            entry = &entries->Items().back();
        }
        entry->Set("data", std::move(data));
        // The project now has this plugin's data: record it, so the project is never opened without
        // it (and saved without its data). The record is written automatically, never by hand.
        json::Value* used = project_.doc.Find("plugins");
        if (!used || !used->IsArray()) used = &project_.doc.Set("plugins", json::Value::MakeArray());
        std::vector<PluginId> ids{ws->type->plugin};
        for (const PluginId& p : ws->software->UsedPlugins()) ids.push_back(p);  // (its components' plugins ...)
        for (const PluginId& id : ids) {
            bool known = false;
            for (const auto& u : used->Items()) known |= u.Get("author").AsString() == id.author && u.Get("name").AsString() == id.name;
            if (known) continue;
            json::Value rec = json::Value::MakeObject();
            rec.Set("author", id.author);
            rec.Set("name", id.name);
            used->Push(std::move(rec));
            log::Info("このプロジェクトが使うプラグインとして記録しました: {}", id.Text());
        }
    }
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

fs::path EditorScreen::LayoutFile() const { return project_.LayoutFile(); }

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
        const json::Value* view = nullptr;
        for (const auto& e : entries.Items()) {
            if (!SameKey(e, key)) continue;
            loaded = dock::Layout::FromJson(e.Get("layout"));
            if (!loaded) broken = true;
            view = e.Find("view");
            reflect::Warnings warnings;
            ws->named = dock::NamedLayouts::FromJson(e.Get("named"), &warnings);
            for (const auto& wmsg : warnings) log::Warn("名前を付けた配置: {}", wmsg);
            if (const json::Value* t = e.Find("tree")) tree::LoadState(ws->treeState, *t);
            ws->assetThumbnail = std::clamp(static_cast<float>(e.Get("assets").Get("thumbnail").AsDouble(ws->assetThumbnail)),
                                            AssetPanel::kMinThumbnail, AssetPanel::kMaxThumbnail);
        }
        if (ws->software) ws->software->LoadView(view ? *view : json::Value());
        dock::Layout layout = loaded ? std::move(*loaded) : ws->DefaultLayout();
        layout.Sanitize(ws->dock.PanelIds());
        ws->dock.SetLayout(std::move(layout));
    }
    if (broken) log::Warn("パネルの配置を一部読めなかったので、最初の配置で開きます: {}", PathToUtf8(file));
    LoadSharedLayouts();
    const json::Value& active = layoutDoc_.Get("active");
    for (size_t i = 0; i < workspaces_.size(); ++i) {
        if (active.IsObject() && SameKey(active, WorkspaceKey(workspaces_[i]->type))) active_ = i;
    }
}

// %APPDATA%\RM_Engine\Layouts.json - named layouts every project can use (paths::SharedLayoutsFile):
//   { "kind": "rm_engine.shared_layouts", "formatVersion": 1,
//     "softwares": [ { "author": ..., "plugin": ..., "software": ..., "named": [ { "name", "layout" } ] } ] }
// Read when the editor opens, written as soon as the list changes. Softwares not installed now and
// unknown keys are written back unchanged (like Layout.json).
constexpr char kSharedLayoutsKind[] = "rm_engine.shared_layouts";
constexpr int64_t kSharedLayoutsFormatVersion = 1;

void EditorScreen::LoadSharedLayouts() {
    sharedDoc_ = json::Value::MakeObject();
    sharedReadOnly_ = false;
    const fs::path file = paths::SharedLayoutsFile();
    std::error_code ec;
    if (fs::exists(file, ec)) {
        std::string err;
        auto bytes = platform::ReadFileBytes(file, &err);
        auto doc = bytes ? json::Parse(*bytes) : std::nullopt;
        if (doc && doc->IsObject() && doc->Get("kind").AsString() == kSharedLayoutsKind) {
            sharedDoc_ = std::move(*doc);
            sharedReadOnly_ = sharedDoc_.Get("formatVersion").AsInt(0) > kSharedLayoutsFormatVersion;
            if (sharedReadOnly_) log::Warn("共通の配置は新しい版の RM Engine で保存されています。この版では上書きしません");
        } else {
            // Never written over: a file we cannot read may be the user's to fix.
            sharedReadOnly_ = true;
            log::Warn("共通の配置を読めませんでした (上書きしません): {}", PathToUtf8(file));
        }
    }
    for (auto& ws : workspaces_) {
        ws->shared = {};
        const json::Value key = WorkspaceKey(ws->type);
        for (const auto& e : sharedDoc_.Get("softwares").Items()) {
            if (!SameKey(e, key)) continue;
            reflect::Warnings warnings;
            ws->shared = dock::NamedLayouts::FromJson(e.Get("named"), &warnings);
            for (const auto& wmsg : warnings) log::Warn("共通の配置: {}", wmsg);
        }
    }
}

void EditorScreen::SaveSharedLayouts() {
    if (sharedReadOnly_) {
        log::Warn("共通の配置はファイルに書きません (新しい版のファイルか、読めなかったファイルのため)");
        return;
    }
    sharedDoc_.Set("kind", kSharedLayoutsKind);
    sharedDoc_.Set("formatVersion", kSharedLayoutsFormatVersion);
    json::Value* entries = sharedDoc_.Find("softwares");
    if (!entries || !entries->IsArray()) entries = &sharedDoc_.Set("softwares", json::Value::MakeArray());
    for (auto& ws : workspaces_) {
        const json::Value key = WorkspaceKey(ws->type);
        json::Value* entry = nullptr;
        for (auto& e : entries->Items()) {
            if (SameKey(e, key)) entry = &e;
        }
        if (!entry) {
            if (ws->shared.Empty()) continue;
            entries->Push(key);
            entry = &entries->Items().back();
        }
        entry->Set("named", ws->shared.ToJson());
    }
    const fs::path file = paths::SharedLayoutsFile();
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::string err;
    if (!platform::WriteFileAtomic(file, json::Write(sharedDoc_), &err)) log::Warn("共通の配置を保存できませんでした: {}", err);
}

void EditorScreen::CaptureFloatingRects() {
    dock::Layout& current = Active().dock.GetLayout();
    for (auto& fw : floats_) {
        if (dock::Floating* f = current.FindFloating(fw->Id())) f->rect = fw->CurrentRect();
    }
}

void EditorScreen::SaveLayout() {
    if (layoutReadOnly_) return;
    CaptureFloatingRects();
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
        if (!ws->named.Empty() || entry->Find("named")) entry->Set("named", ws->named.ToJson());
        json::Value* assets = entry->Find("assets");
        if (!assets || !assets->IsObject()) assets = &entry->Set("assets", json::Value::MakeObject());
        assets->Set("thumbnail", static_cast<double>(std::round(ws->assetThumbnail)));
        if (ws->software) {
            if (json::Value view = ws->software->SaveView(); !view.IsNull()) entry->Set("view", std::move(view));
            if (ws->software->Hierarchy()) entry->Set("tree", tree::SaveState(ws->treeState));
        }
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
    Active().namedShown.clear();
    Active().namedShownShared = false;
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
    w::DrawBackdrop(c, true);  // the theme's picture, behind everything
    // While the editor shows a dialog, the panel windows only show.
    const bool blocked = modal_ != Modal::None;
    c.SetInputEnabled(!blocked);
    if (!blocked) HandleShortcuts(c);
    Active().dock.Draw(c, Rect{0, 0, width, height}.Inset(4), window.Id(), window.Hwnd(), window.Scale());
    Active().dock.DrawOverlay(c, window.Id());
    c.SetInputEnabled(true);
    ForwardDrag(c, window.Hwnd(), width, height);
    FlushAssetEdits(c);
    w::EndDragFrame(c);
}

void EditorScreen::ForwardDrag(ui::Context& c, HWND self, float width, float height) {
    const ui::Input& in = c.In();
    // Only the window the drag is in has the button down (it has the mouse captured) or sees it let go.
    if (!w::DraggedThing() || (!in.down[0] && !in.released[0])) return;
    ScreenWindow* target = nullptr;
    POINT pt{};
    const bool outside = in.mouseX < 0 || in.mouseY < 0 || in.mouseX >= width || in.mouseY >= height;
    if (outside && GetCursorPos(&pt)) {
        const HWND under = platform::TopLevelWindowAt(pt, self);
        if (under == Hwnd()) target = this;
        for (auto& f : floats_) {
            if (under && f->Hwnd() == under) target = f.get();
        }
    }
    float x = 0, y = 0;
    if (target) {
        ScreenToClient(target->Hwnd(), &pt);
        x = static_cast<float>(pt.x) / target->Scale();
        y = static_cast<float>(pt.y) / target->Scale();
    }
    const HWND now = target ? target->Hwnd() : nullptr;
    if (dragTarget_ && dragTarget_ != now) InvalidateRect(dragTarget_, nullptr, FALSE);  // its outline goes away
    dragTarget_ = in.released[0] ? nullptr : now;
    w::SetDragElsewhere(target ? &target->UiContext() : nullptr, x, y, in.released[0]);
    if (target) {
        target->Invalidate();
        c.RequestRedraw();  // keep following the mouse
    }
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
    if (dirty) dirtySince_ = platform::NowSeconds();  // the auto-save interval counts from the first unsaved change
    dirty_ = dirty;
    UpdateTitle();
    Invalidate();
}

void EditorScreen::UpdateTitle() {
    SetTitle((dirty_ ? L"* " : L"") + Utf8ToWide(project_.Name()) + L" - RM Engine");
}

void EditorScreen::FlushAssetEdits(const ui::Context& c) {
    // An import setting is written once the edit is finished: no button held (a drag of a number),
    // no field of this window being typed into.
    if (!assets_->HasQueuedImports() || c.Focused() != 0) return;
    if (GetKeyState(VK_LBUTTON) < 0 || GetKeyState(VK_RBUTTON) < 0 || GetKeyState(VK_MBUTTON) < 0) return;
    assets_->FlushImports();
}

bool EditorScreen::Save() {
    assets_->FlushImports();
    std::vector<ProjectFileWrite> files;
    StoreSoftwareData(files);
    auto fail = [&](const std::string& err) {
        log::Error("{}", err);
        modalText_ = Utf8ToWide(err);
        modal_ = Modal::Error;
        return false;
    };
    // The softwares' own files (a game's scenes) first, each atomically: the .rmpf written after them
    // never names a file that is not there. A failure stops the save before the .rmpf.
    for (const ProjectFileWrite& f : files) {
        const bool inside = !f.path.empty() && f.path.find("..") == std::string::npos && f.path.find(':') == std::string::npos &&
                            f.path.front() != '/' && f.path.front() != '\\';
        if (!inside) return fail(TrU8F("プロジェクトの外のファイルには書けません: {}", f.path));
        const fs::path file = project_.Root() / PathFromUtf8(f.path);
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        std::string err;
        if (!platform::WriteFileAtomic(file, f.bytes, &err)) return fail(TrU8F("保存できませんでした: {}\n{}", PathToUtf8(file), err));
        log::Info("保存しました: {}", PathToUtf8(file));
    }
    std::string err;
    if (!SaveProject(project_, &err)) return fail(err);
    for (auto& ws : workspaces_) {
        if (ws->software) ws->software->Saved();
    }
    SetDirty(false);
    SaveLayout();
    return true;
}

inspector::Refs EditorScreen::MakeRefs() {
    inspector::Refs refs;
    AssetLibrary* lib = assets_.get();
    refs.asset = [lib](std::string_view guid) { return AssetChoice(*lib, guid); };
    refs.assets = [lib] { return AssetChoices(*lib); };
    if (Thumbnails* thumbs = thumbnails_.get()) {
        refs.thumbnail = [lib, thumbs](ui::Context& c, const std::string& guid, float size) { return AssetThumbnail(c, *lib, *thumbs, guid, size); };
    }
    // An entity reference = an item of this software's 階層 (game: an entity). Its parent is "where".
    tree::Source* src = Active().software ? Active().software->Hierarchy() : nullptr;
    if (!src) return refs;
    // Listed once per drawing of the インスペクター, and only when a field asks (every entity-reference
    // field of the drawing shares the list, and finds its item by the id's index).
    struct Items {
        bool built = false;
        std::vector<inspector::RefChoice> list;
        std::unordered_map<uint64_t, size_t> at;  // id -> its place in list
    };
    auto cache = std::make_shared<Items>();
    auto build = [src, cache]() -> const Items& {
        if (cache->built) return *cache;
        cache->built = true;
        std::vector<tree::Item> items;
        src->Items(items);
        const tree::ItemIndex index(items);  // (the tree's own id index)
        cache->list.reserve(items.size());
        for (const tree::Item& it : items) {
            inspector::RefChoice r;
            r.value = std::to_string(it.id);
            r.name = Utf8ToWide(it.name);
            const size_t p = it.parent != 0 ? index.Find(it.parent) : static_cast<size_t>(-1);
            if (p != static_cast<size_t>(-1)) r.detail = Utf8ToWide(items[p].name);
            r.icon = tree::PictureOf(it.icon);
            cache->at.try_emplace(it.id, cache->list.size());
            cache->list.push_back(std::move(r));
        }
        return *cache;
    };
    refs.items = [build] { return build().list; };
    refs.item = [build](uint64_t id) -> std::optional<inspector::RefChoice> {
        const Items& c = build();
        const auto it = c.at.find(id);
        if (it == c.at.end()) return std::nullopt;
        return c.list[it->second];
    };
    return refs;
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

bool EditorScreen::OnQueryEndSession() {
    if (!dirty_) {
        SaveLayout();
        return true;
    }
    // Asked like closing: save / close without saving / cancel. Windows waits on its shutdown page
    // with this reason; "保存して閉じる" then quits, and the shutdown goes on (or the user starts it again).
    if (!shutdownBlocked_) {
        ShutdownBlockReasonCreate(Hwnd(), Tr(L"RM Engine: 保存されていない変更があります").c_str());
        shutdownBlocked_ = true;
    }
    pendingClose_ = CloseAction::Quit;
    modal_ = Modal::UnsavedChanges;
    log::Warn("Windows の終了を待ってもらいました (保存されていない変更があるため): {}", project_.Name());
    Invalidate();
    return false;
}

void EditorScreen::OnEndSession() {
    // Shutting down anyway ("強制的にシャットダウン"): the layout is kept; unsaved changes cannot be asked about.
    if (dirty_) log::Warn("保存しないまま Windows が終了しました: {}", project_.Name());
    SaveLayout();
}

void EditorScreen::EndShutdownBlock() {
    if (!shutdownBlocked_) return;
    ShutdownBlockReasonDestroy(Hwnd());
    shutdownBlocked_ = false;
}

void EditorScreen::FinishClose(CloseAction action) {
    assets_->FlushImports();
    EndShutdownBlock();
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
            return {{Tr(L"保存"), L"Ctrl+S"}, {L"", L"", true, true}, {Tr(L"Hub に戻る")}, {Tr(L"終了")}};
        case Menu::Edit:
            return {{Tr(L"元に戻す"), L"Ctrl+Z", Active().history.CanUndo()}, {Tr(L"やり直し"), L"Ctrl+Y", Active().history.CanRedo()}};
        case Menu::Window: {
            std::vector<w::MenuItem> items;
            for (const auto& t : Active().dock.PanelTypes()) {
                w::MenuItem it;
                it.label = Tr(t.title);
                it.checked = Active().dock.GetLayout().Contains(t.id);
                items.push_back(std::move(it));
            }
            // Order read by RunMenu: panels | named layouts (this project's, then the shared ones;
            // when there are some) | the commands.
            items.push_back({L"", L"", true, true});
            for (const bool sharedList : {false, true}) {
                for (const auto& e : Active().Named(sharedList).Entries()) {
                    w::MenuItem it;
                    it.label = Utf8ToWide(e.info.name);
                    it.shortcut = sharedList ? Tr(L"共通の配置") : Tr(L"このプロジェクトの配置");
                    it.checked = e.info.name == Active().namedShown && sharedList == Active().namedShownShared;
                    items.push_back(std::move(it));
                }
            }
            if (!Active().named.Empty() || !Active().shared.Empty()) items.push_back({L"", L"", true, true});
            items.push_back({Tr(L"今の配置に名前を付けて保存…")});
            items.push_back({Tr(L"配置の管理…")});
            items.push_back({Tr(L"最初の配置に戻す")});
            return items;
        }
        case Menu::Plugins: {
            // One row per plugin, in the order of Plugins::Entries() (RunMenu relies on it).
            std::vector<w::MenuItem> items;
            for (const Plugins::Entry& e : app_.GetPlugins().Entries()) {
                w::MenuItem it;
                const std::wstring name = Utf8ToWide(TrU8(e.status.displayName));
                if (e.connector && !e.installed) {
                    // A connector is never chosen: it comes along with what it joins.
                    it.label = name + L"  (" + Utf8ToWide(e.status.id.Text()) + L")";
                    it.shortcut = Tr(L"つなぐ両方が入ると自動で入ります");
                    it.enabled = false;
                } else if (e.installed) {
                    it.label = name + L"  (" + Utf8ToWide(e.status.id.Text()) + L")";
                    it.shortcut = e.status.state == PluginState::Enabled ? Tr(L"使用中") : Tr(L"⚠ 使えません");
                    it.enabled = false;  // information only
                } else {
                    it.label = TrF(L"「{}」をインストール…", name);
                    it.enabled = e.available;
                }
                items.push_back(std::move(it));
            }
            if (items.empty()) items.push_back({Tr(L"このエンジンにはプラグインがありません"), L"", false});
            items.push_back({L"", L"", true, true});
            items.push_back({Tr(L"削除・再インストールは Hub の「プラグイン」で行います"), L"", false});
            return items;
        }
        case Menu::Help:
            return {{Tr(L"RM Engine について")}};
        case Menu::Debug:
            return {{Tr(L"未保存の変更を作る (テスト用)")}};
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
        if (index == 0) UndoRedo(false);
        if (index == 1) UndoRedo(true);
    } else if (m == Menu::Window) {
        const auto& types = Active().dock.PanelTypes();
        const auto& named = Active().named.Entries();
        const auto& shared = Active().shared.Entries();
        const int panels = static_cast<int>(types.size());
        const int firstNamed = panels + 1;
        const int firstShared = firstNamed + static_cast<int>(named.size());
        const int namedEnd = firstShared + static_cast<int>(shared.size());
        const int commands = namedEnd + (namedEnd == firstNamed ? 0 : 1);
        if (index >= 0 && index < panels) {
            Active().dock.TogglePanel(types[static_cast<size_t>(index)].id);
        } else if (index >= firstNamed && index < firstShared) {
            ApplyNamedLayout(named[static_cast<size_t>(index - firstNamed)].info.name, false);
        } else if (index >= firstShared && index < namedEnd) {
            ApplyNamedLayout(shared[static_cast<size_t>(index - firstShared)].info.name, true);
        } else if (index == commands) {
            BeginSaveLayoutAs();
        } else if (index == commands + 1) {
            renamingLayout_.clear();
            confirmDelete_.clear();
            modal_ = Modal::Layouts;
        } else if (index == commands + 2) {
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
    // Files added / moved / renamed outside (Explorer): the ids follow, the names shown too.
    if (assets_->Poll()) RedrawAll();
    FlushAssetEdits(c);
    p.Clear(theme::Bg);
    w::DrawBackdrop(c, false);  // the theme's picture, behind everything (again on each panel and bar)
    const bool overlay = modal_ != Modal::None || menuPopup_.open;
    c.SetInputEnabled(!overlay);

    if (!overlay) HandleShortcuts(c);

    Rect full{0, 0, width, height};
    const Rect menuBar = ui::TakeTop(full, 30);
    const Rect tabs = ui::TakeTop(full, 36);
    const Rect status = ui::TakeBottom(full, 24);
    if (Active().software) Active().software->Tick(c);  // a game that plays moves on before it is drawn
    Active().dock.Draw(c, full.Inset(4), 0, Hwnd(), Scale());
    DrawSoftTabs(c, tabs);
    DrawStatusBar(c, status);
    DrawMenuBar(c, menuBar);

    c.SetInputEnabled(!overlay);
    Active().dock.DrawOverlay(c, 0);
    c.SetInputEnabled(true);
    if (menuPopup_.open) DrawOpenMenu(c);
    DrawModal(c);
    ForwardDrag(c, Hwnd(), width, height);
    w::EndDragFrame(c);
}

void EditorScreen::BeginInstallPlugin(const PluginId& id) {
    std::vector<PluginId> unavailable;
    installPlan_ = app_.GetPlugins().InstallPlan(id, &unavailable);
    installName_ = Utf8ToWide(app_.GetPlugins().DisplayName(id));
    if (!unavailable.empty()) {
        std::wstring names;
        for (const PluginId& u : unavailable) names += L"\n・" + Utf8ToWide(app_.GetPlugins().DisplayName(u));
        ShowError(TrF(L"「{}」の前提プラグインがこのエンジンにありません。エンジンを新しい版にしてください。", installName_) + names);
        return;
    }
    if (installPlan_.empty()) return;
    modal_ = Modal::InstallPlugin;
}

void EditorScreen::HandleShortcuts(ui::Context& c) {
    // Shared by the editor window and the panel windows: Ctrl+S works wherever the focus is.
    if (c.Focused() != 0) return;
    if (c.KeyPressed(ui::Key::S, true)) Save();
    if (c.KeyPressed(ui::Key::Z, true)) UndoRedo(false);
    if (c.KeyPressed(ui::Key::Y, true) || c.KeyPressed(ui::Key::Z, true, true)) UndoRedo(true);
}

void EditorScreen::UndoRedo(bool redo) {
    // While a software runs (a game in Play) its data stays as it was: undo would change what the
    // run goes back to, under it.
    if (Active().software && Active().software->Running()) {
        log::Info("Play 中は元に戻す / やり直しはできません (止めてから)");
        return;
    }
    if (redo) {
        Active().history.Redo();
    } else {
        Active().history.Undo();
    }
}

void EditorScreen::DrawMenuBar(ui::Context& c, Rect r) {
    auto& p = c.P();
    w::DrawBar(c, r, theme::Sidebar);
    static const wchar_t* kTitles[] = {TrKey(L"ファイル"), TrKey(L"編集"), TrKey(L"ウィンドウ"), TrKey(L"プラグイン"), TrKey(L"ヘルプ"), TrKey(L"デバッグ")};
#ifdef _DEBUG
    const int count = static_cast<int>(Menu::Count);
#else
    const int count = static_cast<int>(Menu::Debug);
#endif
    float x = r.x + 6;
    for (int i = 0; i < count; ++i) {
        const std::wstring menuTitle = Tr(kTitles[i]);
        const float wdt = p.TextWidth(menuTitle, 13) + 24;
        const Rect item{x, r.y + 3, wdt, r.h - 6};
        menuTitleRects_[i] = item;
        c.PushId(static_cast<uint64_t>(i));
        const auto b = c.ButtonBehavior(c.MakeId("menubar"), item);
        c.PopId();
        const bool open = menuPopup_.open && static_cast<int>(openMenu_) == i;
        if (open || b.hovered) w::DrawSurface(c, item, open ? w::Surface::Pressed : w::Surface::Hovered);
        p.Text(item, menuTitle, Style(13, theme::Text, false, ui::HAlign::Center));
        if (b.clicked) {
            openMenu_ = static_cast<Menu>(i);
            menuPopup_.OpenAt(item.x, r.Bottom());
        }
        x += wdt + 2;
    }
    for (int i = count; i < static_cast<int>(Menu::Count); ++i) menuTitleRects_[i] = {};
    w::Separator(c, r.x, r.Bottom() - 1, r.w);
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
    // A software that runs (a game in Play) tints the row: it can be seen from anywhere in the window.
    const bool running = Active().software && Active().software->Running();
    w::DrawBar(c, r, running ? theme::RunningBar() : theme::Bg);
    w::Separator(c, r.x, r.Bottom() - 1, r.w);
    if (!workspaces_[0]->type) {
        // No plugin provides a "software", so there are no tabs to show.
        p.Text(r.Inset(16, 0, 16, 0), Tr(L"ソフトはまだありません — プラグインを追加すると、ここにソフト (ゲーム・イラスト・作曲 …) のタブが並びます"),
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
        w::DrawTab(c, tab, sel, b.hovered);
        p.Text(tab, title, Style(13, sel ? theme::Text : theme::TextDim, true, ui::HAlign::Center));
        if (b.clicked) SwitchSoftware(i);
        x += wdt + 4;
    }
    // The software's own controls (a game's ▶ Play ■), in the room right of the tabs.
    if (Active().software && r.Right() - x > 40) {
        c.PushId("toolbar");
        Active().software->DrawToolbar(c, {x + 8, r.y, r.Right() - x - 16, r.h});
        c.PopId();
    }
}

void EditorScreen::DrawHierarchy(ui::Context& c, Rect r) {
    Workspace& ws = Active();
    if (tree::Source* src = ws.software ? ws.software->Hierarchy() : nullptr) {
        tree::Draw(c, ws.treeState, *src, r);
        return;
    }
    ui::TextStyle hint = Style(12, theme::TextFaint);
    hint.wrap = true;
    hint.v = ui::VAlign::Top;
    c.P().Text(r.Inset(12, 10, 12, 10),
               ws.software ? Tr(L"このソフトは階層を使いません。") : Tr(L"ソフトを入れると、ここに中身 (ゲームならエンティティの親子) が並びます。"), hint);
}

void EditorScreen::DrawInspector(ui::Context& c, Rect r) {
    Workspace& ws = Active();
    const Inspectable& target = ws.inspected;
    // The blocks as they are now (a component may have come back or gone with an undo).
    const std::vector<InspectBlock> blocks = target.blocks ? target.blocks() : std::vector<InspectBlock>{};
    std::vector<inspector::Block> shown;
    std::vector<size_t> source;  // shown[i] is blocks[source[i]]
    for (size_t i = 0; i < blocks.size(); ++i) {
        const InspectBlock& b = blocks[i];
        const reflect::EditTarget obj = b.type && b.resolve ? b.resolve() : reflect::EditTarget{};
        if (b.type && !obj.object) continue;  // gone
        std::vector<const void*> others;
        for (const auto& resolveOther : b.also) {
            if (const reflect::EditTarget o = resolveOther ? resolveOther() : reflect::EditTarget{}; o.object) others.push_back(o.object);
        }
        shown.push_back({b.type, obj.object, obj.unknowns, b.title, b.note, b.menu, b.modes, b.mode, b.setMode, std::move(others), b.readOnly});
        source.push_back(i);
    }
    const bool gone = target.blocks && shown.empty();
    if (shown.empty()) {
        ui::TextStyle hint = Style(12, theme::TextFaint);
        hint.wrap = true;
        hint.v = ui::VAlign::Top;
        c.P().Text(r.Inset(12, 10, 12, 10), !gone && !target.title.empty() ? target.title : Tr(L"何も選ばれていません。"), hint);
        return;
    }
    const Rect title = ui::TakeTop(r, 34);
    c.P().Text(title.Inset(12, 0, 12, 0), target.title, Style(14, theme::Text, true));
    w::Separator(c, title.x + 8, title.Bottom() - 1, title.w - 16);
    // (a choice made in a drop-down arrives a frame later: the list may have changed since)
    const inspector::Refs refs = MakeRefs();
    if (auto edit = inspector::Draw(c, ws.inspectorState, r, shown, target.addLabel, target.addMenu, &refs); edit && edit->block < source.size()) {
        // Edit = undo entry = unsaved (rule 7): the change is applied by the command, never directly.
        const InspectBlock& b = blocks[source[edit->block]];
        if (b.readOnly) {
            // shown only (a drop-down chosen just before Play started): nothing changes
        } else if (b.onEdit) {
            b.onEdit({std::move(edit->path), std::move(edit->before), std::move(edit->after), std::move(edit->label), edit->merge});
        } else if (!b.also.empty()) {
            // The same change to every selected one, as one entry. Every target gets a command (even one
            // that has the value already), so the typing of one field joins command by command.
            auto group = std::make_unique<CommandGroup>(edit->label, edit->merge);
            std::vector<std::function<reflect::EditTarget()>> targets{b.resolve};
            targets.insert(targets.end(), b.also.begin(), b.also.end());
            for (const auto& resolveOne : targets) {
                const reflect::EditTarget t = resolveOne ? resolveOne() : reflect::EditTarget{};
                if (!t.object) continue;
                const reflect::PathTarget pt = reflect::Resolve(*b.type, t.object, t.unknowns, edit->path);
                if (!pt.field) continue;
                json::Value before = reflect::SaveField(*pt.field, pt.owner, pt.unknowns);
                json::Value after = edit->after;
                if (edit->component >= 0 && before.IsArray() && after.IsArray()) {
                    // A vector: only the number typed; the others keep their own.
                    after = before;
                    const size_t i = static_cast<size_t>(edit->component);
                    if (i < after.Items().size() && i < edit->after.Items().size()) after.At(i) = edit->after.At(i);
                }
                group->Add(std::make_unique<reflect::SetFieldCommand>(*b.type, edit->path, std::move(before), std::move(after), resolveOne,
                                                                      edit->label, edit->merge));
            }
            if (!group->Empty()) ws.history.Push(std::move(group), false);
        } else {
            ws.history.Push(std::make_unique<reflect::SetFieldCommand>(*b.type, std::move(edit->path), std::move(edit->before),
                                                                       std::move(edit->after), b.resolve, std::move(edit->label), edit->merge),
                            false);
        }
    }
}

void EditorScreen::DrawStart(ui::Context& c, Rect r) {
    auto& p = c.P();
    const float cy = r.y + r.h * 0.5f;
    p.Text({r.x, cy - 46, r.w, 30}, Tr(L"ソフトがまだありません"), Style(20, theme::Text, true, ui::HAlign::Center));
    ui::TextStyle body = Style(13, theme::TextDim, false, ui::HAlign::Center);
    body.wrap = true;
    body.v = ui::VAlign::Top;
    p.Text({r.x + 40, cy - 6, r.w - 80, 80},
           Tr(L"ゲーム・イラスト・作曲・3D モデルなどの機能は、プラグインとして追加します。\nプラグインを入れると上のタブにソフトが並び、ここにそのソフトのパネルが表示されます。"),
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
    Workspace& ws = Active();
    if (!ws.assetPanel) {
        ws.assetPanel = std::make_unique<AssetPanel>(*assets_, thumbnails_.get(), ws.assetThumbnail,
                                                     [this](std::wstring problem) { ShowError(std::move(problem)); });
        Workspace* wp = &ws;
        ws.assetPanel->SetInspector([wp](Inspectable i) { wp->Inspect(std::move(i)); },
                                    [wp](std::unique_ptr<ICommand> cmd) { wp->history.Push(std::move(cmd), false); });
    }
    ws.assetPanel->Draw(c, r);
}

void EditorScreen::DrawStatusBar(ui::Context& c, Rect r) {
    auto& p = c.P();
    w::DrawBar(c, r, theme::Sidebar);
    w::Separator(c, r.x, r.y, r.w);
    p.Text(r.Inset(12, 0, 260, 0), project_.file.native(), Style(12, theme::TextFaint));
    if (dirty_) {
        p.Text(r.Inset(12, 0, 12, 0), Tr(L"● 未保存の変更があります"), Style(12, theme::Warn, false, ui::HAlign::Right));
    } else {
        p.Text(r.Inset(12, 0, 12, 0), Tr(L"保存済み"), Style(12, theme::TextDim, false, ui::HAlign::Right));
    }
}

void EditorScreen::DrawModal(ui::Context& c) {
    if (modal_ == Modal::None) return;
    auto& p = c.P();
    ui::TextStyle body = Style(13, theme::Text);
    body.wrap = true;
    body.v = ui::VAlign::Top;

    if (modal_ == Modal::UnsavedChanges) {
        const Rect cr = w::BeginModal(c, 600, 220, Tr(L"保存されていない変更があります"));
        p.Text({cr.x, cr.y, cr.w, cr.h - 48},
               TrF(L"「{}」の変更を保存しますか？\n保存しないで閉じると、変更は失われます。", Utf8ToWide(project_.Name())), body);
        const Rect save{cr.Right() - 150, cr.Bottom() - 36, 150, 36};
        const Rect discard{save.x - 12 - 150, save.y, 150, 36};
        const Rect cancel{discard.x - 12 - 110, save.y, 110, 36};
        if (w::Button(c, "uc.cancel", cancel, Tr(L"キャンセル")) || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
            pendingClose_ = CloseAction::None;
            EndShutdownBlock();  // Windows' shutdown (if it was one) is cancelled
        } else if (w::Button(c, "uc.discard", discard, Tr(L"保存せずに閉じる"))) {
            modal_ = Modal::None;
            log::Warn("保存せずに閉じました: {}", project_.Name());
            FinishClose(pendingClose_);
        } else if (w::Button(c, "uc.save", save, Tr(L"保存して閉じる"), w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter)) {
            modal_ = Modal::None;
            if (Save()) FinishClose(pendingClose_);  // on failure Save() shows the error and the editor stays open
        }
        return;
    }

    if (modal_ == Modal::SaveLayoutAs) {
        DrawSaveLayoutAs(c);
        return;
    }
    if (modal_ == Modal::Layouts) {
        DrawLayouts(c);
        return;
    }

    if (modal_ == Modal::InstallPlugin) {
        std::wstring list;
        if (installPlan_.size() > 1) {
            list = L"\n" + Tr(L"前提のプラグインも一緒に入ります:");
            for (const PluginId& id : installPlan_) list += L"\n・" + Utf8ToWide(app_.GetPlugins().DisplayName(id));
        }
        const Rect cr = w::BeginModal(c, 620, 250 + 20 * static_cast<float>(installPlan_.size()), Tr(L"プラグインのインストール"));
        p.Text({cr.x, cr.y, cr.w, cr.h - 48},
               TrF(L"「{}」をインストールします。", installName_) + list + L"\n\n" +
                   Tr(L"プロジェクトを保存してから、このプロジェクトを開き直します。\n元に戻す (Undo) の履歴は消えます。"),
               body);
        const Rect ok{cr.Right() - 180, cr.Bottom() - 36, 180, 36};
        if (w::Button(c, "ip.cancel", {ok.x - 12 - 110, ok.y, 110, 36}, Tr(L"やめる")) || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
        } else if (w::Button(c, "ip.ok", ok, Tr(L"保存して開き直す"), w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter)) {
            modal_ = Modal::None;
            // A failed save shows its error and the editor stays as it is (nothing is lost).
            if (Save()) app_.RequestInstallAndReopen(installPlan_, project_.file);
        }
        return;
    }

    if (modal_ == Modal::Error) {
        const Rect cr = w::BeginModal(c, 600, 260, Tr(L"エラー"));
        p.Text({cr.x, cr.y, cr.w, cr.h - 48}, modalText_, body);
        if (w::Button(c, "err.ok", {cr.Right() - 110, cr.Bottom() - 36, 110, 36}, L"OK", w::ButtonKind::Primary) ||
            c.KeyPressed(ui::Key::Enter) || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
        }
        return;
    }

    if (modal_ == Modal::About) {
        const Rect cr = w::BeginModal(c, 520, 230, Tr(L"RM Engine について"));
        p.Text({cr.x, cr.y, cr.w, cr.h - 48},
               TrF(L"RM Engine  バージョン {}\n\nゲーム・イラスト・作曲・3D モデルなどを、プラグインを足して 1 つのソフトで作るための制作環境。",
                           kEngineVersionW),
               body);
        if (w::Button(c, "about.ok", {cr.Right() - 110, cr.Bottom() - 36, 110, 36}, Tr(L"閉じる"), w::ButtonKind::Primary) ||
            c.KeyPressed(ui::Key::Enter) || c.KeyPressed(ui::Key::Escape)) {
            modal_ = Modal::None;
        }
    }
}

}  // namespace rm::editor
