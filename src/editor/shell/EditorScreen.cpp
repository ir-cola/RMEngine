#include "editor/lang/Lang.h"
#include "editor/shell/EditorScreen.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/app/Paths.h"
#include "editor/plugin/Plugins.h"
#include "editor/shell/FloatWindow.h"
#include "editor/shell/ShapedWindow.h"
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

// "start" パネルは、ソフトが 1 つも入っていない間だけある。
constexpr char kStart[] = "core.start";

// <project>/UserSettings/Layout.json
//   { "kind": "rm_engine.layout", "formatVersion": 2,
//     "active": { "author": "rm_engine", "plugin": "game_production", "software": "game" },
//     "softwares": [ { "author": ..., "plugin": ..., "software": ..., "layout": { "main": ..., "floating": [...] },
//                      "view": { ... ソフト自身のもの (Software::SaveView) },
//                      "named": [ { "name": ..., "layout": {...} } ] (名前を付けて保存したレイアウト: dock/NamedLayouts.h),
//                      "tree": { "collapsed": [...], "selected": [...] } (階層: tree::SaveState),
//                      "assets": { "thumbnail": 40 } (アセット パネル: サムネイルの大きさ。単位は DIP) } ] }
// ソフトのないワークスペースは "software": "core" だけを持つ。今は入っていないソフトの項目と、
// このバージョンが知らないキーは、そのまま書き戻す。
// (kLayoutKind / kLayoutFormatVersion: editor/project/Project.h - 新しいプロジェクトのテンプレートも同じ値を書く。)
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
// Debug ビルドだけ: 何も変えない編集。「未保存の変更」の流れを端から端まで試すためのもの。
class TestCommand final : public ICommand {
public:
    void Do() override {}
    void Undo() override {}
    std::string Label() const override { return "テスト用の変更"; }
};
#endif

}  // namespace

EditorScreen::EditorScreen(App& app, Project project) : ScreenWindow(app), project_(std::move(project)) {
    // (エディターが待機中にエクスプローラーでファイルが足された場合: 監視が起こし、フレームがもう一度調べる)
    assets_ = std::make_unique<AssetLibrary>(project_.AssetsDir(), assetsWake_.Signal());
    // ソフトがデータを読む前に ID が要る (シーンは Prefab を ID で探す)。待つのはこの最初の調べだけで、
    // 以降はすべて Worker で走る (AssetLibrary)。
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
    assets_->onChanged = [this] { RedrawAll(); };  // 調べた結果が反映された (Worker から)
    LoadLayout();  // 下のコールバックより前: まだ同期するものはない
    if (!CreateScreenWindow(d, SW_SHOWMAXIMIZED)) return false;
    for (auto& ws : workspaces_) {
        Workspace* w = ws.get();
        w->dock.onStructureChanged = [this, w] {
            if (w == &Active()) RequestSyncFloating();
        };
        w->dock.onRedrawAll = [this] { RedrawAll(); };
    }
    SyncFloating(false);  // 前回保存したパネルウィンドウを、フォーカスを奪わずに戻す
    // キューに積む: ワーカースレッドで書かれたログは、App のメッセージループ (log::DeliverQueued) から
    // この (UI) スレッドでウィンドウに届く - RedrawAll はウィンドウに触るため。
    logSink_ = log::AddSink([this](const log::Entry&) { RedrawAll(); }, log::Delivery::Queued);
    log::Info("エディターを開きました: {} ({})", project_.Name(), WideToUtf8(Active().Title()));
    UpdateTitle();
    SetTimer(Hwnd(), kAutoSaveTimer, 15000, nullptr);  // 15 秒ごとに調べ、選んだ間隔で保存する
    return true;
}

void EditorScreen::OnLookChanged() {
    ScreenWindow::OnLookChanged();
    for (auto& f : floats_) f->OnLookChanged();
    UpdateTitle();
}

void EditorScreen::OnTimer(UINT_PTR id) {
    if (id != kAutoSaveTimer || !dirty_) return;
    // 作業の途中では保存しない: ダイアログ、開いているメニュー、入力中、ドラッグ中 (ボタンを押している間)。
    if (modal_ != Modal::None || menuPopup_.open || ctx_.Focused() != 0) return;
    if (GetKeyState(VK_LBUTTON) < 0 || GetKeyState(VK_RBUTTON) < 0 || GetKeyState(VK_MBUTTON) < 0) return;
    const double minutes = Minutes(app_.Settings().GetOptions().autoSave);
    if (platform::NowSeconds() - dirtySince_ < minutes * 60.0) return;
    log::Info("自動保存します ({} 分ごと)", minutes);
    if (!Save()) dirtySince_ = platform::NowSeconds();  // エラーは表示済み。もう 1 間隔待ってやり直す
}

void EditorScreen::Close() {
    if (Hwnd()) KillTimer(Hwnd(), kAutoSaveTimer);
    CloseFloats();
    ScreenWindow::Close();
    if (lock_) lock_->Release();  // これで別の RM Engine がプロジェクトを開ける
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
// ワークスペース (ソフトごとに 1 つ) とそのレイアウト
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
    // 参照ができる前のデータ: プロジェクト内のパス ("Assets/Models/robot.glb")。
    std::string rel = ref.guid;
    std::replace(rel.begin(), rel.end(), '\\', '/');
    if (rel.find("..") != std::string::npos || rel.find(':') != std::string::npos) return {};  // プロジェクトの外に出ない
    std::error_code ec;
    const fs::path p = editor_.project_.Root() / PathFromUtf8(rel);
    return fs::exists(p, ec) ? p : fs::path();
}

AssetRef EditorScreen::Workspace::AssetOf(const fs::path& file) {
    AssetLibrary& lib = *editor_.assets_;
    const fs::path rel = file.lexically_normal().lexically_relative(lib.Root().lexically_normal());
    if (rel.empty() || rel.native().starts_with(L"..")) return {};  // Assets/ の中ではない
    const std::string path = PathToUtf8(rel.generic_wstring());
    // (書いたばかりのファイル - 保存した Prefab: 調べを待たずに、今すぐ ID をもらう)
    const std::string guid = lib.AdoptFile(path);
    return guid.empty() ? AssetRef{} : AssetRef{guid};
}

std::optional<std::string> EditorScreen::Workspace::AssetContent(const AssetRef& ref) {
    if (asset::IsGuid(ref.guid)) return editor_.assets_->Content(ref.guid);
    const fs::path file = AssetFile(ref);  // (参照ができる前に作られたデータのパス)
    std::string err;
    return file.empty() ? std::nullopt : platform::ReadFileBytes(file, &err);
}

void EditorScreen::Workspace::SetAssetContent(const AssetRef& ref, std::string bytes) {
    if (!asset::IsGuid(ref.guid) || !editor_.assets_->Index().ByGuid(ref.guid)) return;
    editor_.assets_->QueueContent(ref.guid, std::move(bytes));
    editor_.RedrawAll();
}

std::vector<PluginManifest> EditorScreen::Workspace::PluginManifests() const { return editor_.app_.GetPlugins().Available(); }

void EditorScreen::Workspace::PickFolder(std::wstring title, fs::path start, std::function<void(fs::path)> done) {
    EditorScreen& ed = editor_;
    ed.Defer([&ed, title = std::move(title), start = std::move(start), done = std::move(done)] {
        if (auto folder = platform::PickFolder(ed.Hwnd(), title, start.native())) {
            ed.ctx_.ClearFocus();  // 入力中のフィールドが、古い文字を新しい値の上に書き戻してはいけない
            done(fs::path(*folder));
        }
    });
}

std::unique_ptr<ToolWindow> EditorScreen::Workspace::OpenToolWindow(ToolWindowSpec spec) {
    auto w = std::make_unique<ShapedWindow>(editor_.app_, std::move(spec));
    if (!w->Open(editor_.Hwnd())) return nullptr;
    return w;
}

void EditorScreen::Workspace::Ask(std::wstring title, std::wstring text, std::vector<std::wstring> buttons, std::function<void(int)> done) {
    EditorScreen& ed = editor_;
    if (ed.modal_ != Modal::None || buttons.empty()) {
        if (done) done(-1);
        return;
    }
    ed.askTitle_ = std::move(title);
    ed.modalText_ = std::move(text);
    ed.askButtons_ = std::move(buttons);
    ed.askDone_ = std::move(done);
    ed.modal_ = Modal::Ask;
    ed.RedrawAll();
}

std::wstring EditorScreen::Workspace::RenameAsset(const AssetRef& ref, const std::string& newNameUtf8) {
    return editor_.assets_->Rename(ref.guid, newNameUtf8);
}

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
    list.push_back(panel(core_panel::kHierarchy, TrKey(L"ヒエラルキー"), &EditorScreen::DrawHierarchy));
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
        // ソフトのパネルはコアのパネルの後ろ。ID はぶつからないこと (レイアウトに保存されるため)。
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
    // エディターの一番大事な配線: どれかの履歴に届いた編集は、プロジェクトを未保存にする。
    for (auto& ws : workspaces_) ws->history.SetOnModified([this] { SetDirty(true); });
}

// .rmpf: "softwareData": [ { "author": ..., "plugin": ..., "software": ..., "data": {...} } ]
// 今は入っていないソフトの項目はそのまま残す。
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
    for (auto& ws : workspaces_) {
        if (!ws->software) continue;
        // ソフトごとに探し直す: 下のドキュメントのメンバーはその後に増えているかもしれない (新しい
        // キーの Set で動く) - 前のポインターは解放済みのメモリを指してしまう。
        json::Value* entries = project_.doc.Find("softwareData");
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
        // プロジェクトはこのプラグインのデータを持つようになった: 記録しておき、プロジェクトがそれ無しで
        // 開かれる (そしてそのデータ抜きで保存される) ことがないようにする。記録は自動で書き、手では書かない。
        json::Value* used = project_.doc.Find("plugins");
        if (!used || !used->IsArray()) used = &project_.doc.Set("plugins", json::Value::MakeArray());
        std::vector<PluginId> ids{ws->type->plugin};
        for (const PluginId& p : ws->software->UsedPlugins()) ids.push_back(p);  // (その部品のプラグイン ...)
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
    // フレームの外で: 古いソフトのフローティングウィンドウを壊し、新しいほうのを作る。
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
        // フォーマット 1 は { "softwares": { "core": <layout> } } だった。
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

// %APPDATA%\RM_Engine\Layouts.json - どのプロジェクトでも使える、名前付きレイアウト (paths::SharedLayoutsFile):
//   { "kind": "rm_engine.shared_layouts", "formatVersion": 1,
//     "softwares": [ { "author": ..., "plugin": ..., "software": ..., "named": [ { "name", "layout" } ] } ] }
// エディターを開くときに読み、一覧が変わったらすぐ書く。今は入っていないソフトと
// 知らないキーはそのまま書き戻す (Layout.json と同じ)。
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
            // 上書きしない: 読めないファイルは、ユーザーが直すものかもしれない。
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
    // ウィンドウはメッセージループから作って壊す。フレームの中や、壊す最中のウィンドウの
    // ウィンドウプロシージャの中では決してしない。
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
            f.rect = fw->CurrentRect();  // 違うことがある: 無くなったモニターにあった保存位置
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
    w::DrawBackdrop(c, true);  // テーマの絵。すべての後ろに
    // エディターがダイアログ (や 部品を足す パネル: OnFrame) を出している間、パネルウィンドウは表示だけする。
    const bool blocked = modal_ != Modal::None || Active().inspectorState.addPopup.open;
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
    // ドラッグしているウィンドウだけが、ボタンを押している (マウスをキャプチャーしている) か、離したのを見る。
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
    if (dragTarget_ && dragTarget_ != now) InvalidateRect(dragTarget_, nullptr, FALSE);  // その枠線を消す
    dragTarget_ = in.released[0] ? nullptr : now;
    w::SetDragElsewhere(target ? &target->UiContext() : nullptr, x, y, in.released[0]);
    if (target) {
        target->Invalidate();
        c.RequestRedraw();  // マウスに付いていく
    }
}

void EditorScreen::CloseFloatingWindow(uint32_t floatingId) { Active().dock.CloseFloating(floatingId); }

void EditorScreen::FloatingMoving(uint32_t floatingId, HWND hwnd) { Active().dock.WindowMoving(floatingId, hwnd); }

void EditorScreen::FloatingMoveEnded(uint32_t floatingId) {
    Active().dock.WindowMoveEnded(floatingId);
    // まだフローティング (ドックに戻していない): ユーザーが置いた場所を覚えておく。
    dock::Floating* f = Active().dock.GetLayout().FindFloating(floatingId);
    for (auto& fw : floats_) {
        if (f && fw->Id() == floatingId) f->rect = fw->CurrentRect();
    }
}

// ---------------------------------------------------------------------------
// 状態
// ---------------------------------------------------------------------------

void EditorScreen::SetDirty(bool dirty) {
    if (dirty_ == dirty) return;
    if (dirty) dirtySince_ = platform::NowSeconds();  // 自動保存の間隔は、最初の未保存の変更から数える
    dirty_ = dirty;
    UpdateTitle();
    Invalidate();
}

void EditorScreen::UpdateTitle() {
    SetTitle((dirty_ ? L"* " : L"") + Utf8ToWide(project_.Name()) + L" - RM Engine");
}

void EditorScreen::FlushAssetEdits(const ui::Context& c) {
    // 取り込みの設定は編集が終わってから書く: ボタンを押していない (数のドラッグ)、
    // このウィンドウのフィールドに入力していない。
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
    // ソフト自身のファイル (ゲームのシーン) を先に、それぞれ原子的に書く: そのあとに書く .rmpf が、
    // 無いファイルの名前を持つことはない。失敗したら .rmpf の前で保存を止める。
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
    // エンティティ参照 = このソフトの 階層 の項目 (ゲームではエンティティ)。その親が「場所」。
    tree::Source* src = Active().software ? Active().software->Hierarchy() : nullptr;
    if (!src) return refs;
    // インスペクターを 1 回描くごとに 1 度だけ一覧にし、フィールドが求めたときだけ作る (その描画の
    // エンティティ参照フィールドはみな一覧を共有し、ID の添字で項目を探す)。
    struct Items {
        bool built = false;
        std::vector<inspector::RefChoice> list;
        std::unordered_map<uint64_t, size_t> at;  // id -> list の中の位置
    };
    auto cache = std::make_shared<Items>();
    auto build = [src, cache]() -> const Items& {
        if (cache->built) return *cache;
        cache->built = true;
        std::vector<tree::Item> items;
        src->Items(items);
        const tree::ItemIndex index(items);  // (ツリー自身の ID 索引)
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
    // 閉じるときと同じように尋ねる: 保存 / 保存せず閉じる / キャンセル。Windows は、この理由を
    // 付けてシャットダウンのページで待つ。"保存して閉じる" で終了し、シャットダウンは続く (またはユーザーがやり直す)。
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
    // とにかくシャットダウンする ("強制的にシャットダウン"): レイアウトは残す。未保存の変更は尋ねられない。
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
// メニュー
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
            // RunMenu が読む順: パネル | 名前付きレイアウト (このプロジェクトのもの、次に共有のもの。
            // あるとき) | コマンド。
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
            // プラグイン 1 つにつき 1 行。Plugins::Entries() の順 (RunMenu はこれに頼る)。
            std::vector<w::MenuItem> items;
            for (const Plugins::Entry& e : app_.GetPlugins().Entries()) {
                w::MenuItem it;
                const std::wstring name = Utf8ToWide(TrU8(e.status.displayName));
                if (e.connector && !e.installed) {
                    // コネクターは選ばれない: つなぐものと一緒に付いてくる。
                    it.label = name + L"  (" + Utf8ToWide(e.status.id.Text()) + L")";
                    it.shortcut = Tr(L"つなぐ両方が入ると自動で入ります");
                    it.enabled = false;
                } else if (e.installed) {
                    it.label = name + L"  (" + Utf8ToWide(e.status.id.Text()) + L")";
                    it.shortcut = e.status.state == PluginState::Enabled ? Tr(L"使用中") : Tr(L"⚠ 使えません");
                    it.enabled = false;  // 情報だけ
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
        case Menu::Debug: {
            // 表示中のソフトの行が先 (RunMenu が数える)、そのあとにエディター自身のテスト。
            std::vector<w::MenuItem> items;
            if (Active().software) {
                for (const Software::DebugMenuItem& d : Active().software->DebugMenu()) {
                    w::MenuItem it;
                    it.label = d.label;
                    it.checked = d.checked;
                    items.push_back(std::move(it));
                }
            }
#ifdef _DEBUG
            if (!items.empty()) items.push_back({L"", L"", true, true});
            items.push_back({Tr(L"未保存の変更を作る (テスト用)")});
#endif
            if (items.empty()) items.push_back({Tr(L"このソフトにはデバッグの項目がありません"), L"", false});
            return items;
        }
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
        std::vector<Software::DebugMenuItem> rows;
        if (Active().software) rows = Active().software->DebugMenu();
        if (index >= 0 && index < static_cast<int>(rows.size())) {
            if (rows[static_cast<size_t>(index)].run) rows[static_cast<size_t>(index)].run();
            return;
        }
#ifdef _DEBUG
        if (index == static_cast<int>(rows.size()) + (rows.empty() ? 0 : 1)) {
            Active().history.Push(std::make_unique<TestCommand>());
            log::Info("テスト用の変更を履歴に積みました");
        }
#endif
    }
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void EditorScreen::OnFrame(ui::Context& c, float width, float height) {
    auto& p = c.P();
    // 外 (エクスプローラー) でファイルが足された / 動かされた / 名前を変えられた: ID が付いてくる。
    // 表示する名前も。
    if (assets_->Poll()) RedrawAll();
    if (assets_->Settling()) c.RequestAnimation();  // (しばらく静かになるまでフレームを回す)
    FlushAssetEdits(c);
    p.Clear(theme::Bg);
    w::DrawBackdrop(c, false);  // テーマの絵。すべての後ろに (パネルやバーごとにもう一度)
    // メニュー、ダイアログ - またはインスペクターの 部品を足す パネル (パネルより広いとき): その下にあるものは
    // 入力を受けない (2026-11-17 画面で: シーンビューの上にあるパネルのフォルダー行をクリックすると
    // シーンビューにも届いて、何も選ばなかった - インスペクターが作り直されてパネルが閉じた)。
    // 階層 / アセット の右クリックメニュー、インスペクターの ⋮ メニュー、ドロップダウンの一覧も同じ
    // (2026-11-18 画面で: シーンビューの上で 追加 ▸ 3D オブジェクト ▸ 球 を選んだら、それも
    // シーンビューに届き、新しい 球 でなくマウスの下にあるものが選ばれた)。
    const Workspace& view = Active();
    const bool overlay = modal_ != Modal::None || menuPopup_.open || view.inspectorState.addPopup.open || view.inspectorState.menu.open ||
                         view.inspectorState.popup.open || view.treeState.menu.open || (view.assetPanel && view.assetPanel->MenuOpen());
    c.SetInputEnabled(!overlay);

    if (!overlay) HandleShortcuts(c);

    Rect full{0, 0, width, height};
    const Rect menuBar = ui::TakeTop(full, 30);
    const Rect tabs = ui::TakeTop(full, 36);
    const Rect status = ui::TakeBottom(full, 24);
    if (Active().software) Active().software->Tick(c);  // Play 中のゲームは、描く前に先へ進む
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
    // 右ボタンを押している間 (3D のシーンビューが飛ぶ: Ctrl が下がり、S が戻る) はショートカットを動かさない -
    // そこでの Ctrl + S は飛ぶことで、保存ではない (2026-11-19)。
    if (c.In().down[static_cast<int>(ui::Mouse::Right)]) return;
    // エディターのウィンドウとパネルウィンドウで共有: Ctrl+S はフォーカスがどこにあっても効く -
    // テキストフィールドの中でも (そこで打った文字はもうデータに入っている: フィールドは 1 キーごとに
    // 反映する)。前は下の判定が先にあり、フォーカス中のフィールドが Ctrl+S の保存を妨げた
    // (2026-11-17 画面で: アセットの検索で入力したあと Ctrl+S しても、タイトルの "*" が残った)。
    if (c.KeyPressed(ui::Key::S, true)) Save();
    // フィールドの中の Ctrl+Z / Ctrl+Y はそのフィールド自身のもの (その文字)。履歴のものではない。
    if (c.Focused() != 0) return;
    if (c.KeyPressed(ui::Key::Z, true)) UndoRedo(false);
    if (c.KeyPressed(ui::Key::Y, true) || c.KeyPressed(ui::Key::Z, true, true)) UndoRedo(true);
}

void EditorScreen::UndoRedo(bool redo) {
    // ソフトが動いている間 (Play 中のゲーム) は、そのデータはそのまま。Undo すると、実行が戻る先を
    // 実行中に変えてしまう。
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
    const int count = static_cast<int>(Menu::Count);  // (デバッグも: ゲームの ゲームデバッグ がある)
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
    // メニューが開いている間、別のメニューのタイトルにマウスを重ねるとそちらに切り替わる (どのデスクトップアプリとも同じ)。
    const auto& in = c.In();
    for (int i = 0; i < static_cast<int>(Menu::Count); ++i) {
        const Rect& t = menuTitleRects_[i];
        if (t.w > 0 && t.Contains(in.mouseX, in.mouseY) && static_cast<int>(openMenu_) != i) {
            openMenu_ = static_cast<Menu>(i);
            menuPopup_.OpenAt(t.x, t.Bottom() + 3);
            c.RequestRedraw();
        }
    }
    // 開いているメニューのタイトルをクリックすると閉じる (MenuPopup は「外」として扱う)。
    const Menu m = openMenu_;
    const std::vector<w::MenuItem> items = MenuItems(m);
    // 一番長い行 (ラベル + ショートカット) と同じ幅。ただし 260 以上。
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
    // 動いているソフト (Play 中のゲーム) は行に色を付ける: ウィンドウのどこからでも分かる。
    const bool running = Active().software && Active().software->Running();
    w::DrawBar(c, r, running ? theme::RunningBar() : theme::Bg);
    w::Separator(c, r.x, r.Bottom() - 1, r.w);
    if (!workspaces_[0]->type) {
        // "software" を出すプラグインがないので、表示するタブがない。
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
    // ソフト自身の操作 (ゲームの ▶ Play ■) は、タブの右の空きに出す。
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
    // 今のブロック (Undo で部品が戻ったり無くなったりしているかもしれない)。
    const std::vector<InspectBlock> blocks = target.blocks ? target.blocks() : std::vector<InspectBlock>{};
    std::vector<inspector::Block> shown;
    std::vector<size_t> source;  // shown[i] は blocks[source[i]]
    for (size_t i = 0; i < blocks.size(); ++i) {
        const InspectBlock& b = blocks[i];
        const reflect::EditTarget obj = b.type && b.resolve ? b.resolve() : reflect::EditTarget{};
        if (b.type && !obj.object) continue;  // 無くなった
        std::vector<const void*> others;
        for (const auto& resolveOther : b.also) {
            if (const reflect::EditTarget o = resolveOther ? resolveOther() : reflect::EditTarget{}; o.object) others.push_back(o.object);
        }
        shown.push_back({b.type, obj.object, obj.unknowns, b.title, b.note, b.menu, b.modes, b.mode, b.setMode, std::move(others), b.readOnly, b.fieldMenu});
        shown.back().presetLabel = b.presetLabel;
        shown.back().presets = b.presets;
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
    // (ドロップダウンで選んだものは 1 フレーム遅れて届く: その間に一覧が変わったかもしれない)
    const inspector::Refs refs = MakeRefs();
    if (auto edit = inspector::Draw(c, ws.inspectorState, r, shown, target.addLabel, target.addMenu, &refs); edit && edit->block < source.size()) {
        // 編集 = Undo の項目 = 未保存 (憲法 7): 変更はコマンドが適用し、直接は変えない -
        // ただしその場で編集するブロック (`direct`: Play 中のゲームのコピー) は別で、そのコマンドはすぐ実行し
        // どこにも残さない。
        const InspectBlock& b = blocks[source[edit->block]];
        auto commit = [&](std::unique_ptr<ICommand> cmd) {
            if (b.direct) {
                cmd->Do();
            } else {
                ws.history.Push(std::move(cmd), false);
            }
        };
        if (b.readOnly) {
            // 表示だけ (Play が始まる直前に選んだドロップダウン): 何も変わらない
        } else if (b.onEdit) {
            b.onEdit({std::move(edit->path), std::move(edit->before), std::move(edit->after), std::move(edit->label), edit->merge});
        } else if (!b.also.empty()) {
            // 選択中のすべてに同じ変更を、1 つの項目として。どの対象にもコマンドを作る (すでに同じ値の
            // ものにも)。こうして 1 つのフィールドの入力が、コマンドごとにつながる。
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
                    // ベクター: 打った数だけ。ほかの成分はそれぞれの値のまま。
                    after = before;
                    const size_t i = static_cast<size_t>(edit->component);
                    if (i < after.Items().size() && i < edit->after.Items().size()) after.At(i) = edit->after.At(i);
                }
                group->Add(std::make_unique<reflect::SetFieldCommand>(*b.type, edit->path, std::move(before), std::move(after), resolveOne,
                                                                      edit->label, edit->merge));
            }
            if (!group->Empty()) commit(std::move(group));
        } else {
            commit(std::make_unique<reflect::SetFieldCommand>(*b.type, std::move(edit->path), std::move(edit->before), std::move(edit->after),
                                                              b.resolve, std::move(edit->label), edit->merge));
        }
        // 変更が、このフレームのフィールド (とその前のパネル) を描いた後に来た: それを反映してもう 1 回
        // (2026-11-18 画面で: シーン「Main」でドロップダウンの選択が、マウスを動かすまで表示されなかった)。
        c.RequestRedraw();
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
        w::ScrollToEnd(c, "console");  // 新しい出力を追う (新しいウィンドウでは最後から始める)
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
        ws.assetPanel->SetPreviews(&app_.GetPlugins().Registry(), [this] { RedrawAll(); });
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
            EndShutdownBlock();  // Windows のシャットダウン (そうだったなら) は取り消された
        } else if (w::Button(c, "uc.discard", discard, Tr(L"保存せずに閉じる"))) {
            modal_ = Modal::None;
            log::Warn("保存せずに閉じました: {}", project_.Name());
            FinishClose(pendingClose_);
        } else if (w::Button(c, "uc.save", save, Tr(L"保存して閉じる"), w::ButtonKind::Primary) || c.KeyPressed(ui::Key::Enter)) {
            modal_ = Modal::None;
            if (Save()) FinishClose(pendingClose_);  // 失敗したら Save() がエラーを出し、エディターは開いたまま
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
            // 保存に失敗するとエラーを出し、エディターはそのまま (何も失われない)。
            if (Save()) app_.RequestInstallAndReopen(installPlan_, project_.file);
        }
        return;
    }

    if (modal_ == Modal::Ask) {
        // ソフトからの質問 (SoftwareHost::Ask): 答えはダイアログが消えた後に返す。
        const Rect cr = w::BeginModal(c, 600, 220, askTitle_);
        p.Text({cr.x, cr.y, cr.w, cr.h - 48}, modalText_, body);
        int answer = -2;
        float x = cr.Right();
        for (size_t i = 0; i < askButtons_.size(); ++i) {
            const float width = std::max(110.f, 24.f + 14.f * static_cast<float>(askButtons_[i].size()));
            x -= width;
            if (w::Button(c, "ask." + std::to_string(i), {x, cr.Bottom() - 36, width, 36}, askButtons_[i],
                          i == 0 ? w::ButtonKind::Primary : w::ButtonKind::Secondary)) {
                answer = static_cast<int>(i);
            }
            x -= 12;
        }
        if (answer == -2 && c.KeyPressed(ui::Key::Escape)) answer = -1;
        if (answer != -2) {
            modal_ = Modal::None;
            auto done = std::move(askDone_);
            askDone_ = nullptr;
            askButtons_.clear();
            if (done) done(answer);
            Invalidate();
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
