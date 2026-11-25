#include "rm_engine/game_production/editor/GameSoftware.h"

#include "base/FileName.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/reflect/Data.h"
#include "editor/app/Worker.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "editor/export/ExportPlan.h"
#include "editor/inspector/Inspector.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/DebugPanel.h"
#include "rm_engine/game_production/editor/SceneSettingLook.h"
#include "rm_engine/game_production/editor/GameScripts.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Id.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_production/runtime/SceneRecord.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "core/asset/AssetFiles.h"
#include "core/asset/ImportSettings.h"
#include "rm_engine/game_production/runtime/GameDraw.h"
#include "rm_engine/game_production/runtime/PlayServices.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <optional>
#include <system_error>
#include <unordered_map>

namespace rm::game_production {

namespace {

namespace fs = std::filesystem;
namespace theme = editor::theme;
namespace tree = editor::tree;
using editor::Tr;
using editor::TrF;
using editor::TrKey;
using editor::TrU8;
using ui::Rect;

constexpr char kSceneView[] = "scene_view";
constexpr char kGameView[] = "game_view";
constexpr char kScenesPanel[] = "scenes";
constexpr size_t kMaxPrefabRows = 20;  // "Prefab を置く" の行数 (階層のメニュー)
constexpr float kSpaceStripH = 34.f;   // ゲームビューの上の帯 (止まっています ...)

// 1 つの空間のビュー (SceneViewInfo): シーンビューと、Play 中にゲームを映すゲームビュー
// (同じ種類のビュー)。
struct View {
    const SceneViewInfo* info = nullptr;
    std::unique_ptr<SceneView> scene;
    std::unique_ptr<SceneView> game;
};

void Placeholder(ui::Context& c, const Rect& r, const std::wstring& title, const std::wstring& body) {
    auto& p = c.P();
    const float cy = r.y + r.h * 0.5f;
    ui::TextStyle ts;
    ts.size = 18;
    ts.bold = true;
    ts.color = theme::Text;
    ts.h = ui::HAlign::Center;
    p.Text({r.x, cy - 40, r.w, 28}, title, ts);
    ts.size = 13;
    ts.bold = false;
    ts.color = theme::TextDim;
    ts.wrap = true;
    ts.v = ui::VAlign::Top;
    p.Text({r.x + 32, cy - 4, r.w - 64, 80}, body, ts);
}

// シーンの形の変更 (作る・消す・複製・動かす): 変更の前と後のシーン。名前・スイッチなどの
// フィールドの編集は、ほかと同じく reflect::SetFieldCommand。
class SceneCommand final : public ICommand {
public:
    SceneCommand(Scene& scene, json::Value before, json::Value after, std::string label)
        : scene_(scene), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {
        bytes_ = json::Write(before_, false).size() + json::Write(after_, false).size();  // 一度だけ (履歴の上限のため)
    }
    size_t Bytes() const override { return bytes_; }
    // 次の id は取っておく: 一度配った id は二度と配らない (Undo の後に作ったエンティティが、
    // Undo で消えたエンティティの id をもらわないように。参照や保存したビューがそれを指してしまう)。
    void Do() override { Load(after_); }
    void Undo() override { Load(before_); }
    std::string Label() const override { return label_; }

private:
    void Load(const json::Value& v) {
        const int next = scene_.NextId();
        scene_.Load(v);
        scene_.KeepNextId(next);
    }

    Scene& scene_;
    json::Value before_, after_;
    std::string label_;
    size_t bytes_ = 0;
};

// 編集が書いたファイル (Prefab - Prefab に反映): Undo は元にあった内容を書き戻し、Redo は新しい内容を書く。
// シーンの変更と同じ 1 項目に入れるので、Undo でファイルとシーンがずれることはない。
class FileCommand final : public ICommand {
public:
    FileCommand(std::function<void(const std::string&)> write, std::string before, std::string after, std::string label)
        : write_(std::move(write)), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {}
    void Do() override { write_(after_); }
    void Undo() override { write_(before_); }
    std::string Label() const override { return label_; }
    size_t Bytes() const override { return before_.size() + after_.size(); }

private:
    std::function<void(const std::string&)> write_;
    std::string before_, after_, label_;
};

// シーンの形の変更は、触れたエンティティの前と後 (SceneRecord) として持つ。シーン全体を 2 つ
// 持つのではない。Undo / Redo はそのエンティティだけを戻す。
class RecordCommand final : public ICommand {
public:
    RecordCommand(Scene& scene, SceneRecord record, std::string label) : scene_(scene), record_(std::move(record)), label_(std::move(label)) {}
    void Do() override { record_.Redo(scene_); }
    void Undo() override { record_.Undo(scene_); }
    std::string Label() const override { return label_; }
    size_t Bytes() const override { return record_.Bytes(); }

private:
    Scene& scene_;
    SceneRecord record_;
    std::string label_;
};

// ゲームのシーンの一覧 (シーンの一覧: 足す・消す・動かす・最初の 1 つ) の編集。
class SceneListCommand final : public ICommand {
public:
    SceneListCommand(SceneList& list, SceneList before, SceneList after, std::string label)
        : list_(list), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {}
    void Do() override { list_ = after_; }
    void Undo() override { list_ = before_; }
    std::string Label() const override { return label_; }

private:
    SceneList& list_;
    SceneList before_, after_;
    std::string label_;
};

#ifdef _DEBUG
// 次の id を除いて、シーン全体。(次の id は Undo でも戻さない: id は使い回さない。)
json::Value ShapeOf(const Scene& scene) {
    json::Value v = scene.Save();
    v.Erase("nextId");
    return v;
}
#endif

std::vector<int> ToEntityIds(std::span<const tree::ItemId> ids) {
    std::vector<int> out;
    for (const tree::ItemId id : ids) out.push_back(static_cast<int>(id));
    return out;
}

// Play: シーンをコピーし、そのコピーを動かす。停止するとコピーを捨てる (シーンも Undo の履歴も
// 未保存の印も、コピーには関わらない)。
enum class PlayState { Stopped, Playing, Paused };

class GameSoftware final : public editor::Software, public tree::Source {
public:
    explicit GameSoftware(editor::SoftwareHost& host) : host_(host), scripts_(host) {
        // 部品の型はほかのプラグインから来る (コネクター: 「ゲーム制作 × 3D」が Transform を出す)。
        std::vector<const reflect::TypeDesc*> types, multiple;
        for (const auto& c : host.Registry().Contributions<ComponentInfo>(kComponentPoint)) {
            const reflect::TypeDesc* t = &c.item->type();
            if (std::find(types.begin(), types.end(), t) != types.end()) continue;
            types.push_back(t);
            if (c.item->onNewEntities) newEntityInfos_.push_back(c.item);
            if (c.item->multiple) multiple.push_back(t);
            if (c.item->placement) placementTypes_.push_back(t->name);
            if (c.item->folder) componentFolders_[t->name] = c.item->folder;
        }
        baseTypes_ = types;
        baseMultiple_ = multiple;
        scene_.SetComponentTypes(std::move(types), std::move(multiple));
        // シーン自身の値 (重力, 空 ...): シーンを選ぶとインスペクターが見せる。
        scene_.SetSettingTypes(SettingTypes(host.Registry()));
        for (const auto& s : host.Registry().Contributions<SceneSettingInfo>(kSceneSettingPoint)) {
            if (s.item->space) settingSpaces_[s.item->type().name] = s.item->space;
        }
        // エディターでの見せ方 (階層の絵、ローカル / ワールド) は、そのプラグインのエディター部分が持つ。
        for (const auto& l : host.Registry().Contributions<ComponentLook>(kComponentLookPoint)) componentLooks_.push_back(l.item);
        for (const auto& l : host.Registry().Contributions<SceneSettingLook>(kSceneSettingLookPoint)) settingLooks_.push_back(l.item);
        // ... Play 中にそれを動かすシステムも同じ。
        runner_.SetSystems(RegisteredSystems(host.Registry()));
        // システムはプロジェクトのファイルからアセットを読む (メッシュコライダー: シーンビューと同じ読み方のモデル)。
        runner_.SetAssets({nullptr, [this](const AssetRef& ref) { return host_.AssetFile(ref); }});
        cameraTypes_ = CameraTypes(host.Registry());
    }

    // スクリプトのオブジェクトは、その DLL より先に消す (scripts_ はシーンより先に壊れる)。
    ~GameSoftware() override {
        playScene_.reset();
        scene_.Load(json::Value());
    }

    // --- ソフト ---------------------------------------------------------------------------
    std::vector<editor::dock::PanelType> Panels() override {
        std::vector<editor::dock::PanelType> panels{
            {kSceneView, TrKey(L"シーンビュー"),
             [this](ui::Context& c, const Rect& r) {
                 // (デバッグ パネル用に測る: どのくらいの頻度で、どのくらい時間がかかるか)
                 const double t0 = platform::NowSeconds();
                 DrawSceneView(c, r);
                 sceneMeter_.Frame(t0, (platform::NowSeconds() - t0) * 1000.0);
                 sceneGraph_.Push(t0, (platform::NowSeconds() - t0) * 1000.0);
                 sceneMouse_.reset();
                 if (c.IsHovered(r)) sceneMouse_ = Vec2{c.In().mouseX - r.x, c.In().mouseY - r.y};
             }},
            {kGameView, TrKey(L"ゲームビュー"),
             [this](ui::Context& c, const Rect& r) {
                 const double t0 = platform::NowSeconds();
                 DrawGameView(c, r);
                 gameMeter_.Frame(t0, (platform::NowSeconds() - t0) * 1000.0);
             }},
            {kScenesPanel, TrKey(L"シーン"), [this](ui::Context& c, const Rect& r) { DrawScenesPanel(c, r); }},
        };
        // プラグインが足すパネル (ゲーム UI: UI ビュー)。空間のものではない。
        for (const auto& p : host_.Registry().Contributions<ViewPanelInfo>(kViewPanelPoint)) {
            const ViewPanelInfo* info = p.item;
            if (!info->create || !info->id || !*info->id) continue;
            panels.push_back({info->id, info->title ? info->title : L"", [this, info](ui::Context& c, const Rect& r) { DrawViewPanel(c, r, *info); }});
        }
        return panels;
    }

    // デバッグ: 専用のウィンドウ (エディターに固定しない): Windows の枠はなく、5 つのタブを持つ
    // ファイルフォルダの形 (DebugPanel.h)。タイマーで描く。毎フレームは描かない (描くと、パネルが
    // 浮いている間エディター本体のウィンドウが描かれなくなった)。
    void ToggleDebugWindow() {
        if (debugWindow_ && debugWindow_->IsOpen()) {
            debugWindow_->Close();
            return;
        }
        editor::ToolWindowSpec spec;
        spec.title = Tr(L"デバッグ");
        spec.widthDip = 620;
        spec.heightDip = 720;
        spec.minWidthDip = 420;
        spec.minHeightDip = 260;
        spec.refreshSeconds = 1.0 / 30.0;
        spec.shape = [](float w, float h) { return FolderWindowShape(w, h); };
        spec.hit = [](float x, float y, float w, float h) { return HitFolderWindow(LayoutFolderWindow(w, h), x, y); };
        spec.draw = [this](ui::Context& c, float w, float h) {
            debugPanel_.DrawWindow(c, w, h, MakeDebugView(), [this] {
                if (debugWindow_) debugWindow_->Close();
            });
        };
        debugWindow_ = host_.OpenToolWindow(std::move(spec));
        if (!debugWindow_) log::Warn("デバッグの窓を作れませんでした");
    }

    // 今デバッグ ウィンドウが見せるもの。
    DebugView MakeDebugView() {
        const double now = platform::NowSeconds();
        DebugView v;
        v.registry = &host_.Registry();
        v.gfx = &host_.Graphics();
        v.scene = &Shown();
        v.settings = &settings_;
        v.playing = Running();
        v.paused = play_ == PlayState::Paused;
        if (v.playing) {
            v.world = &scripts_.World();
            v.runner = &runner_;
        }
        v.sceneView = sceneMeter_.Over(now);
        v.gameView = gameMeter_.Over(now);
        v.update = updateMeter_.Over(now);
        v.sceneMouse = sceneMouse_;
        v.gameMouse = pointer_;
        v.scriptTypes = scripts_.Module().Types().size();
        v.scriptBuild = scripts_.LastBuild();
        v.scriptsBuilding = scripts_.Building();
        v.rebuildScripts = [this] { scripts_.Build(true); };
        v.reloadScripts = [this] { scripts_.Reload(); };
        if (v.playing) {
            v.playTimes = playTimes_;
            v.scriptTimes = scriptTimes_;
            v.systemTimes = systemTimes_;
            v.fixedSteps = runner_.LastSteps();
        }
        v.playGraph = playGraph_;
        v.sceneGraph = sceneGraph_;
        debugShownAt_ = now;
        return v;
    }

    // プラグインが足したパネル: そのビューは、最初に描くときに作る (状態はシーンビューと同じく
    // "viewPanels": {id: 状態} に持つ)。
    void DrawViewPanel(ui::Context& c, const Rect& r, const ViewPanelInfo& info) {
        auto it = viewPanels_.find(info.id);
        if (it == viewPanels_.end()) {
            std::unique_ptr<SceneView> made = info.create();
            if (!made) return;
            made->LoadState(view_.Get("viewPanels").Get(info.id));
            it = viewPanels_.emplace(info.id, std::move(made)).first;
        }
        SceneViewHost h = ViewHost(false);
        it->second->Draw(c, r, h);
        if (play_ == PlayState::Playing) c.RequestAnimation();
    }

    editor::dock::Layout DefaultLayout() const override {
        using editor::dock::Axis;
        namespace cp = editor::core_panel;
        editor::dock::Layout l;
        // プラグインのパネル (UI ビュー) は、シーンとゲームの隣のタブになる。
        std::vector<std::string> views{kSceneView, kGameView};
        for (const auto& p : host_.Registry().Contributions<ViewPanelInfo>(kViewPanelPoint)) {
            if (p.item->create && p.item->id && *p.item->id) views.push_back(p.item->id);
        }
        auto top = l.MakeSplit(Axis::Horizontal, 0.17f, l.MakeLeaf({cp::kHierarchy, kScenesPanel}),
                               l.MakeSplit(Axis::Horizontal, 0.78f, l.MakeLeaf(views), l.MakeLeaf({cp::kInspector})));
        l.main = l.MakeSplit(Axis::Vertical, 0.72f, std::move(top), l.MakeLeaf({cp::kConsole, cp::kAssets}));
        return l;
    }

    tree::Source* Hierarchy() override { return this; }

    // ビューは読んだままに持つ。だから今回開かなかったシーンビューもカメラを保ち (新しい版の
    // キーも残る)。
    // "inspectorModes": [[エンティティ id, モード], ...] - 各エンティティの Transform の見せ方 (ユーザーが切り替える)。
    void LoadView(const json::Value& view) override {
        view_ = view.IsObject() ? view : json::Value::MakeObject();
        for (View& v : views_) v.scene->LoadState(ViewState(v));
        modes_.clear();
        for (const auto& pair : view_.Get("inspectorModes").Items()) {
            if (pair.Items().size() == 2) modes_[static_cast<int>(pair.Items()[0].AsInt())] = pair.Items()[1].AsInt() != 0 ? 1 : 0;
        }
    }
    json::Value SaveView() const override {
        json::Value v = view_.IsObject() ? view_ : json::Value::MakeObject();
        // "sceneViews": {空間: そのビューの状態} - プラグインが今ない空間は、その状態をそのまま持つ。
        if (!views_.empty()) {
            json::Value states = v.Get("sceneViews").IsObject() ? v.Get("sceneViews") : json::Value::MakeObject();
            for (const View& view : views_) {
                if (json::Value s = view.scene->SaveState(); !s.IsNull()) states.Set(view.info->space, std::move(s));
            }
            v.Set("sceneViews", std::move(states));
            v.Erase("sceneView");  // 空間ができる前の 1 つだけのビュー: 今は "sceneViews" にある
        }
        if (!viewPanels_.empty()) {
            json::Value states = v.Get("viewPanels").IsObject() ? v.Get("viewPanels") : json::Value::MakeObject();
            for (const auto& [id, panel] : viewPanels_) {
                if (json::Value s = panel->SaveState(); !s.IsNull()) states.Set(id, std::move(s));
            }
            v.Set("viewPanels", std::move(states));
        }
        json::Value modes = json::Value::MakeArray();
        std::vector<std::pair<int, int>> sorted(modes_.begin(), modes_.end());
        std::sort(sorted.begin(), sorted.end());  // 同じ選び方なら同じファイル
        for (const auto& [id, mode] : sorted) {
            if (!scene_.Find(id)) continue;  // その後に消えた
            json::Value pair = json::Value::MakeArray();
            pair.Push(id);
            pair.Push(mode);
            modes.Push(std::move(pair));
        }
        v.Set("inspectorModes", std::move(modes));
        return v;
    }

    // .rmpf のゲームのデータ: 設定とシーンの一覧。見せるシーンはそのシーン自身のファイル
    // (SceneFile.h) から読む。前の形のプロジェクトはシーンを中に持っている。そこから読み、
    // 次の保存で分ける。
    void LoadData(const json::Value& data) override {
        // スクリプトを先に: シーンのスクリプトは、その型の部品になる。(グローバル変数は前の形では、
        // 名前のヘッダーをプロジェクトと一緒に書く。)
        {
            GameSettings peek;
            reflect::Load(peek, data.Get("settings"));
            scripts_.SetGlobals(peek.globals);
        }
        scripts_.Open();
        UseScriptTypes();
        hadData_ = data.IsObject();
        slot_ = {"", NewSceneFile(), 0};
        if (hadData_) {
            reflect::Warnings warnings;
            reflect::Load(settings_, data.Get("settings"), &unknowns_, &warnings);
            if (data.Find("export")) reflect::Load(exportSettings_, data.Get("export"), &exportUnknowns_, &warnings);
            for (const auto& w : warnings) log::Warn("ゲームの設定: {}", w);
            warnings.clear();
            json::Value rest = data;
            if (std::optional<json::Value> old = TakeOldScene(rest)) {
                MirrorZ(*old, host_.Registry());  // (.rmpf の中のころのシーン: 右手系だった)
                scene_.Load(*old, &warnings);
                oldForm_ = true;
                log::Info("このプロジェクトのシーンは .rmpf の中にあります。次に保存するとき {} に分けます (前の .rmpf は {}{} に残します)", slot_.path,
                          PathToUtf8(host_.GetProject().file.filename()), kOldSceneBackupSuffix);
            } else {
                reflect::Load(sceneList_, data.Get("sceneList"), &sceneListUnknowns_, &warnings);
                ReadOpenScene(warnings);
            }
            if (scripts_.Building() && !warnings.empty()) {
                // 新しいプロジェクト (または DLL より新しいソース) の最初のビルドが進んでいる間は、
                // スクリプトはまだ知らない部品になっている。終わった後に知らせるのは、そのときもまだ
                // 分からないものだけ (ReloadScripts が新しい型でシーンを読み直して、自分の分を知らせる)。
                heldSceneWarnings_ = std::move(warnings);
                log::Info("シーン: スクリプトのビルドが終わってから、使えない部品が残っていないか確かめます");
            } else {
                for (const auto& w : warnings) log::Warn("シーン: {}", w);
            }
            RefreshPrefabsOnOpen();
            ++dataVersion_;  // (シーンを作り直した: 履歴の項目はそれを知らせない)
        }
        host_.Inspect({});  // (ゲーム設定は専用のウィンドウ: 頼まれたときに開く)
    }

    bool SaveData(json::Value& data, std::vector<editor::ProjectFileWrite>& files) override {
        // 前回の保存が出したのに取り込まれなかったもの (Saved が呼ばれていない): その保存が途中で
        // 失敗した。シーンのファイルは書けたかどうか分からない - ディスクにあるものがもう分からない
        // (今回は書き直す)。.meta が書けた新しいシーンは、その id とファイルを持ち続ける。
        if (pending_) {
            std::error_code ec;
            const bool metaThere = slot_.guid.empty() && fs::exists(FullPath(pending_->slot.path + std::string(asset::kMetaSuffix)), ec);
            slot_ = SlotAfterFailedSave(slot_, pending_->slot, metaThere);
            if (metaThere) host_.AssetOf(FullPath(slot_.path));  // (id が分かった: SlotNow が見つける)
            log::Warn("前の保存が途中で失敗したので、シーンをもう一度書きます: {}", slot_.path);
        }
        pending_.reset();
        gave_ = false;
        // 新しいプロジェクトと同じ状態のあいだは何も書かない (プラグインも使用済みとして記録しない)。
        // プロジェクトを一度開いただけで、このプラグインに結びつけてはいけない。
        const GameSettings defaults;
        const ExportSettings exportDefaults;
        const bool untouched = reflect::Equals(reflect::TypeOf<GameSettings>(), &settings_, &defaults) && unknowns_.Empty() &&
                               reflect::Equals(reflect::TypeOf<ExportSettings>(), &exportSettings_, &exportDefaults) && exportUnknowns_.Empty();
        if (!hadData_ && untouched && scene_.Empty()) return false;
        gave_ = true;
        data.Set("settings", reflect::Save(settings_, &unknowns_));
        if (data.Find("export") || !reflect::Equals(reflect::TypeOf<ExportSettings>(), &exportSettings_, &exportDefaults) || !exportUnknowns_.Empty()) {
            data.Set("export", reflect::Save(exportSettings_, &exportUnknowns_));
        }
        // 読めなかったシーンのファイルには上書きしない: 編集したものは新しいファイルに行く。
        if (sceneUnread_ && scene_.Empty()) return true;
        // 置いた各 Prefab が変えたもの。今、その Prefab と比べて求める (ファイルに持つので、プロジェクトを
        // 開くときに、変えた Prefab をその下に入れ直せる)。
        Scene saved = scene_;
        UpdateOverrides(saved);
        const SceneSlot slot = SlotNow();
        ScenePlan plan = PlanSceneSave(saved.Save(), slot, asset::NewGuid());
        if (oldForm_) {
            // .rmpf の元のまま (シーンが中にある形)。新しい形で書く前に、隣に一度だけ取っておく。
            const fs::path rmpf = host_.GetProject().file;
            fs::path backup = rmpf;
            backup += kOldSceneBackupSuffix;
            std::error_code ec;
            std::string err;
            if (!fs::exists(backup, ec)) {
                auto bytes = platform::ReadFileBytes(rmpf, &err);
                if (!bytes) {
                    // 前の .rmpf を取っておくまで分けない: 前と同じ形 (シーンを中に、編集もすべて) で保存し、
                    // 後の保存で分ける。
                    log::Error("前の .rmpf を控え ({}) に写せないので、シーンはまだ .rmpf の中に保存します (次の保存でもう一度分けます): {}",
                               PathToUtf8(backup.filename()), err);
                    data.Set("scene", saved.Save());
                    return true;
                }
                files.push_back({PathToUtf8(backup.filename()), std::move(*bytes)});
            }
        }
        for (FileWrite& f : plan.files) files.push_back({std::move(f.path), std::move(f.text)});
        // 一覧: そのシーンの id を入れる (ファイルが消えたシーンの場所に)。最初に見せる。
        SceneList list = sceneList_;
        auto& ids = list.scenes;
        const auto at = std::find(ids.begin(), ids.end(), AssetRef{slot.guid.empty() ? replaces_ : slot.guid});
        if (at != ids.end() && !at->Empty()) {
            *at = AssetRef{plan.after.guid};
        } else if (std::find(ids.begin(), ids.end(), AssetRef{plan.after.guid}) == ids.end()) {
            ids.push_back(AssetRef{plan.after.guid});
        }
        list.open = AssetRef{plan.after.guid};
        data.Set("sceneList", reflect::Save(list, &sceneListUnknowns_));
        data.Erase("scene");  // (前の形のプロジェクト: 今はファイルの中)
        pending_ = Pending{plan.after, std::move(list)};
        return true;
    }

    // --- 書き出し (GameExport.h) -------------------------------------------------------------------
    bool CanExport() const override { return true; }

    // exe の名前: 入力されたもの。なければゲームの名前から作る (欄にはそのまま見せる)。
    std::string ExeNameNow() const {
        return exportSettings_.exeName.empty() ? editor::exporter::ExeNameFrom(settings_.title) : exportSettings_.exeName;
    }
    // 出力先: この PC の選び方 (ビューのもので、プロジェクトのデータではない)。最初は <project>/Build/<exe の名前>。
    fs::path OutputNow() const {
        const std::string& o = view_.Get("export").Get("output").AsString();
        if (!o.empty()) return PathFromUtf8(o);
        return host_.GetProject().Root() / L"Build" / PathFromUtf8(ExeNameNow());
    }

    // 書き出しの元: 今のプロジェクトそのまま (編集中のシーンで、保存済みだけではない)。
    bool ExportInput(const fs::path& outDir, GameExportInput& in, std::string* error) {
        in.title = settings_.title;
        in.exeName = ExeNameNow();
        if (const std::string p = editor::exporter::ExeNameProblem(in.exeName); !p.empty()) {
            if (error) *error = p;
            return false;
        }
        in.outDir = outDir;
        in.settings = reflect::Save(settings_, &unknowns_);
        in.icon = settings_.icon;
        in.iconFile = settings_.icon.Empty() ? fs::path() : host_.AssetFile(settings_.icon);
        in.projectRoot = host_.GetProject().Root();
        in.assetFile = [this](const AssetRef& r) { return host_.AssetFile(r); };
        in.scripts = ScriptRequestFor(host_);
        in.scripts.globals = settings_.globals;
        // 一覧のシーン (見せているものは今の状態) を、一覧の順に。ゲームは一覧の 最初のシーン
        // から始まる。
        Scene saved = scene_;
        UpdateOverrides(saved);
        ExportScenes scenes = ScenesForExport(sceneList_, AssetRef{slot_.guid}, SlotNow().path, SceneFileText(saved.Save()),
                                              [this](const AssetRef& ref) -> std::optional<std::pair<std::string, std::string>> {
                                                  const fs::path file = host_.AssetFile(ref);
                                                  std::string err;
                                                  auto bytes = file.empty() ? std::nullopt : platform::ReadFileBytes(file, &err);
                                                  if (!bytes) return std::nullopt;
                                                  // 置いた Prefab は、今のファイルのとおりに (開いたときと同じ)。
                                                  if (const auto doc = ReadSceneFile(*bytes, &err, &host_.Registry())) {
                                                      Scene other = scene_;
                                                      reflect::Warnings warnings;
                                                      other.Load(*doc, &warnings);
                                                      RefreshPrefabsIn(other, warnings);
                                                      for (const auto& w : warnings) log::Warn("書き出し: {}: {}", RelativePath(file), w);
                                                      return std::make_pair(RelativePath(file), SceneFileText(other.Save()));
                                                  }
                                                  return std::make_pair(RelativePath(file), std::move(*bytes));
                                              });
        for (const std::string& id : scenes.unread) log::Warn("書き出し: シーン ({}) を読めないので入れません", id);
        in.scenes = std::move(scenes.scenes);
        in.first = scenes.first;
        // プラグイン: 記録されたもの、シーンが使うもの、ゲームの空間を描くもの。それから
        // その要求とコネクター。
        // ゲームが使うものから数える。プロジェクトの記録全体からではない (記録にはほかの
        // ソフト、たとえばイラストのプラグインの名前もある)。それらはゲームに入らず、
        // ないものがあっても止めない。
        std::vector<PluginId> used;
        auto add = [&](const PluginId& id) {
            if (std::find(used.begin(), used.end(), id) == used.end()) used.push_back(id);
        };
        add(game_production::kPluginId.Id());
        for (const PluginId& id : UsedPlugins()) add(id);
        // ... ゲームのほかのシーンすべて (その部品のプラグインも)。
        std::vector<std::string> types;
        for (const auto& s : in.scenes) {
            if (const auto doc = json::Parse(s.second)) ComponentTypesIn(*doc, types);
        }
        for (const PluginId& id : PluginsOffering(types, host_.Registry().Contributions<ComponentInfo>(kComponentPoint))) add(id);
        // ... ゲームが使うアセットが必要とするプラグインも (音: スクリプトが鳴らす音で、どのシーンにも 音源 がないもの)。
        const std::vector<ExportAsset> assets = CollectAssets(in);
        // ... ゲームが置くかもしれない Prefab すべての部品 (どのシーンにも置かれていない Prefab を
        // スクリプトの rm::Spawn が使う場合: そのファイルから読む。この起動で読んだ Prefab がすべてではない)。
        std::vector<std::string> prefabTypes;
        for (const ExportAsset& a : assets) {
            if (PathToUtf8(a.file.extension()) != kPrefabExtension) continue;
            std::string err;
            const auto bytes = platform::ReadFileBytes(a.file, &err);
            if (const auto doc = bytes ? json::Parse(*bytes) : std::nullopt) ComponentTypesIn(*doc, prefabTypes);
        }
        for (const PluginId& id : PluginsOffering(prefabTypes, host_.Registry().Contributions<ComponentInfo>(kComponentPoint))) add(id);
        for (const auto& b : host_.Registry().Contributions<ExportBaker>(kExportBakePoint)) {
            if (b.item->needsPlugin && std::any_of(assets.begin(), assets.end(), [&](const ExportAsset& a) { return b.item->needsPlugin(a); })) add(b.plugin);
        }
        const Contributed<SceneViewInfo>* space = nullptr;
        const auto views = host_.Registry().Contributions<SceneViewInfo>(kSceneViewPoint);
        for (const auto& v : views) {
            if (settings_.space == v.item->space) space = &v;
        }
        for (const auto& v : views) {
            if (!space || (settings_.space != space->item->space && v.item->order < space->item->order)) space = &v;
        }
        if (space) add(space->plugin);
        std::vector<PluginId> missing;
        in.plugins = editor::exporter::ExportPlugins(used, host_.PluginManifests(), &missing);
        if (!missing.empty()) {
            if (error) *error = editor::TrU8F("このエンジンに無いプラグインを使っています: {}", missing.front().Text());
            return false;
        }
        // 記録にあるが、このエンジンにはないプラグイン: 知らせるだけ (ゲームは使わない)。
        if (auto rec = editor::ParsePluginIdList(host_.GetProject().doc.Get("plugins"), nullptr)) {
            const auto manifests = host_.PluginManifests();
            for (const PluginId& id : *rec) {
                const bool here = std::any_of(manifests.begin(), manifests.end(), [&](const editor::PluginManifest& m) { return m.id == id; });
                if (!here) log::Warn("書き出し: プロジェクトが記録しているプラグイン {} はこのエンジンにありませんが、ゲームは使わないので続けます", id.Text());
            }
        }
        return true;
    }

    // プラグインのベイカー (ゲーム用の形に変える: 3D モデル、画像) - static で、どのスレッドからも使える。
    std::vector<const ExportBaker*> Bakers() const {
        std::vector<const ExportBaker*> out;
        for (const auto& b : host_.Registry().Contributions<ExportBaker>(kExportBakePoint)) out.push_back(b.item);
        return out;
    }

    // 書き出しチェックのコマンドライン: このスレッドで、行をログに出す。
    bool ExportNow(const fs::path& outDir, std::string* error) override {
        GameExportInput in;
        editor::exporter::Request req;
        if (!ExportInput(outDir, in, error)) return false;
        auto note = [](const std::string& line) { log::Info("書き出し: {}", line); };
        if (!MakeRequest(in, CollectAssets(in), Bakers(), req, note, error)) return false;
        return editor::exporter::Run(req, [&](float, const std::string& line) { note(line); }, error);
    }

    // 書き出し ボタン: 依頼はここで作り (今のデータ)、作業はワーカーで行う。
    void StartExport() {
        {
            std::lock_guard lock(exportRun_->mtx);  // (ワーカーがロックの下で入れる)
            if (exportRun_->running) return;
        }
        auto run = std::make_shared<ExportRun>();
        exportRun_ = run;
        GameExportInput in;
        std::string err;
        if (!ExportInput(OutputNow(), in, &err)) {
            run->lines.push_back(err);
            run->ok = false;
            log::Error("書き出し: {}", err);
            return;
        }
        // エディターが知っていることはここで読む。残り (ベイク、リンク) はワーカーで - ファイルだけ。
        std::vector<ExportAsset> assets = CollectAssets(in);
        in.assetFile = nullptr;
        run->running = true;
        log::Info("書き出しを始めます: {}", PathToUtf8(in.outDir));
        exportThread_ = std::jthread([run, in = std::move(in), assets = std::move(assets), bakers = Bakers()] {
            auto line = [&](float p, const std::string& text) {
                std::lock_guard lock(run->mtx);
                if (p >= 0) run->progress = p;
                run->lines.push_back(text);
                log::Info("書き出し: {}", text);
            };
            std::string error;
            editor::exporter::Request req;
            const bool ok = MakeRequest(in, assets, bakers, req, [&](const std::string& t) { line(-1, t); }, &error) &&
                            editor::exporter::Run(req, line, &error);
            std::lock_guard lock(run->mtx);
            if (!ok) run->lines.push_back(error);
            run->ok = ok;
            run->running = false;
        });
    }

    // パネルでの編集は値の置き場所へ行く: ゲームの名前はゲーム設定へ、exe の名前はプロジェクトの
    // 書き出し設定へ (どちらも Undo できる)、出力先はビューへ (この PC のもの)。
    void ApplyExportEdit(editor::inspector::Edit& edit) {
        const auto& fields = reflect::TypeOf<ExportView>().fields;
        if (edit.path.empty() || edit.path[0] >= fields.size()) return;
        const std::string_view name = fields[edit.path[0]].name;
        if (name == "output") {
            json::Value e = view_.Get("export").IsObject() ? view_.Get("export") : json::Value::MakeObject();
            e.Set("output", edit.after.AsString());
            view_.Set("export", std::move(e));
            return;
        }
        // Play 中 (2026-11-19): ゲームの名前はその場で変わる - ゲーム設定は ゲーム設定 の編集と同じく Stop で
        // 戻る - 履歴には入らない。exe の名前 (プロジェクトの書き出し設定: Stop で戻さない) は
        // そのときは変えない。
        if (Running() && name == "exeName") {
            log::Info("Play 中は exe の名前を変えられません (止めてから)");
            return;
        }
        auto push = [&](const reflect::TypeDesc& type, std::string_view field, void* object, reflect::Unknowns* unknowns) {
            for (size_t i = 0; i < type.fields.size(); ++i) {
                if (type.fields[i].name != field) continue;
                json::Value before = reflect::SaveField(type.fields[i], object, unknowns);
                auto cmd = std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{i}, std::move(before), edit.after,
                                                                      [object, unknowns] { return reflect::EditTarget{object, unknowns}; }, edit.label,
                                                                      edit.merge);
                if (Running()) {
                    cmd->Do();
                } else {
                    host_.History().Push(std::move(cmd), false);
                }
            }
        };
        if (name == "title") push(reflect::TypeOf<GameSettings>(), "title", &settings_, &unknowns_);
        if (name == "exeName") push(reflect::TypeOf<ExportSettings>(), "exeName", &exportSettings_, &exportUnknowns_);
    }

    // ゲームの設定 ▸ ビルド: ゲームの名前、exe の名前、出力先 (右に 出力先を選ぶ - ウィンドウが狭いときは
    // その下)、入れるシーン、それから 書き出す と、その結果。
    void DrawBuildTab(ui::Context& c, const Rect& r) {
        auto& p = c.P();
        // 低いときはタブがスクロールする (書き出す ボタンとログに届くように)。
        float& contentH = c.FloatState(c.MakeId("export.contentH"), 300.f);
        const float off = editor::w::BeginScroll(c, "export.scroll", r, contentH);
        const Rect area = Rect{r.x, r.y - off, r.w, std::max(r.h, contentH)}.Inset(14, 10, 14, 10);
        const float top = area.y;
        ExportView v{settings_.title, ExeNameNow(), PathToUtf8(OutputNow())};
        constexpr float kBrowseW = 150;
        const bool browseBeside = area.w >= 460;  // (狭いとき: ボタンは欄の下に行く)
        // 名前 (インスペクターが描くとおりに)、それから 出力先 とそのボタン。
        editor::inspector::Block names{&reflect::TypeOf<ExportView>(), &v, nullptr, {}, {}, {}};
        names.fields = {"title", "exeName"};
        const Rect box{area.x - 12, area.y, area.w + 12, std::max(2 * 30.f + 10, exportInspector_.contentHeight)};
        if (auto edit = editor::inspector::Draw(c, exportInspector_, box, std::span(&names, 1))) ApplyExportEdit(*edit);
        float y = box.Bottom() + 2;
        editor::inspector::Block where{&reflect::TypeOf<ExportView>(), &v, nullptr, {}, {}, {}};
        where.fields = {"output"};
        const Rect whereBox{area.x - 12, y, (browseBeside ? area.w - kBrowseW - 8 : area.w) + 12, std::max(30.f + 10, outputInspector_.contentHeight)};
        if (auto edit = editor::inspector::Draw(c, outputInspector_, whereBox, std::span(&where, 1))) ApplyExportEdit(*edit);
        const Rect browse = browseBeside ? Rect{area.Right() - kBrowseW, y + 4, kBrowseW, 26} : Rect{area.Right() - kBrowseW, whereBox.Bottom() + 2, kBrowseW, 26};
        if (editor::w::Button(c, "export_browse", browse, Tr(L"出力先を選ぶ…"))) {
            // 出力フォルダの中 (まだないとき: その上の一番近いフォルダ。ないドライブのとき:
            // プロジェクトのフォルダ)。
            const fs::path start = platform::ExistingFolderAtOrAbove(OutputNow()).empty() ? host_.GetProject().Root() : OutputNow();
            host_.PickFolder(Tr(L"書き出すフォルダを選ぶ"), start, [this](fs::path folder) {
                json::Value e = view_.Get("export").IsObject() ? view_.Get("export") : json::Value::MakeObject();
                e.Set("output", PathToUtf8(folder));
                view_.Set("export", std::move(e));
            });
        }
        y = std::max(whereBox.Bottom(), browse.Bottom()) + 8;
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextDim;
        // アイコン: ゲーム設定のもの (ここでは見せるだけで、選ぶのは ゲーム設定 で)。
        const fs::path icon = settings_.icon.Empty() ? fs::path() : host_.AssetFile(settings_.icon);
        p.Text({area.x, y, area.w, 22}, TrF(L"アイコン: {}", icon.empty() ? Tr(L"(なし。「ゲーム設定」のタブの「アイコン」で PNG か ICO を選びます)") : icon.filename().native()), ts);
        y += 28;
        // 入れるシーン (ゲームが最初に始めるシーンは必ず入る)。
        ui::TextStyle head = ts;
        head.color = theme::Text;
        head.bold = true;
        p.Text({area.x, y, area.w, 22}, Tr(L"ビルドに入れるシーン"), head);
        y += 26;
        const AssetRef first = FirstScene(sceneList_);
        for (const AssetRef& ref : GameScenes(sceneList_, AssetRef{slot_.guid})) {
            bool built = ref.Empty() || SceneBuilt(sceneList_, ref);
            const bool initial = ref == first || (first.Empty() && ref == AssetRef{slot_.guid});
            const bool always = initial || ref.Empty();  // (まだ保存していないシーン: 開いているもの - 必ず入る)
            std::wstring label = Utf8ToWide(SceneTitle(ref));
            if (initial) {
                label += Tr(L" (初期シーン: いつも入る)");
            } else if (always) {
                label += Tr(L" (保存していないシーン: いつも入る)");
            }
            c.PushId(ref.Empty() ? std::string("new") : ref.guid);
            const bool was = built;
            editor::w::Checkbox(c, "built", {area.x + 4, y, area.w - 4, 24}, built, label);
            c.PopId();
            if (built != was && !always && !Running()) {
                EditSceneList(built ? TrU8("シーンをビルドに入れる") : TrU8("シーンをビルドから外す"), [&](SceneList& l) {
                    std::erase(l.notBuilt, ref);
                    if (!built) l.notBuilt.push_back(ref);
                });
            }
            y += 26;
        }
        y += 8;
        const std::string problem = editor::exporter::ExeNameProblem(v.exeName);
        if (!problem.empty()) {
            ui::TextStyle es = ts;
            es.color = theme::Danger;
            p.Text({area.x, y, area.w, 22}, Utf8ToWide(problem), es);
            y += 26;
        }
        ExportRun& run = *exportRun_;
        std::unique_lock lock(run.mtx);
        const bool running = run.running;
        if (editor::w::Button(c, "export_go", {area.x, y, 160, 32}, running ? Tr(L"書き出し中…") : Tr(L"書き出す"), editor::w::ButtonKind::Primary,
                              !running && problem.empty())) {
            lock.unlock();
            StartExport();
            editor::w::EndScroll(c, "export.scroll", r, contentH);
            return;
        }
        if (running || run.ok) {
            editor::w::ProgressBar(c, {area.x + 172, y + 11, std::max(10.f, area.w - 172), 10}, run.ok ? 1.f : run.progress);
        }
        (void)running;  // (進み具合はウィンドウのタイマーがまた描く)
        y += 42;
        // この書き出しのログ (パネルのほかの部分と一緒にスクロールする)。
        ts.wrap = false;
        for (size_t i = 0; i < run.lines.size(); ++i) {
            const bool last = i + 1 == run.lines.size();
            ui::TextStyle ls = ts;
            if (last && run.ok == false) ls.color = theme::Danger;
            p.Text({area.x, y, area.w, 20}, Utf8ToWide(run.lines[i]), ls);
            y += 20;
        }
        contentH = y - top + 20;
        editor::w::EndScroll(c, "export.scroll", r, contentH);
    }

    // シーンと、そこに置く Prefab の部品を出すプラグイン (.rmpf に記録する: 書き出したゲームが
    // それをリンクする)。
    std::vector<PluginId> UsedPlugins() const override {
        std::vector<std::string> types;
        ComponentTypesIn(scene_.Save(), types);
        for (const auto& [guid, prefab] : prefabs_) ComponentTypesIn(prefab, types);
        return PluginsOffering(types, host_.Registry().Contributions<ComponentInfo>(kComponentPoint));
    }

    void Saved() override {
        if (gave_) hadData_ = true;  // (今は .rmpf にある: 空にもう一度する保存は、それも書く)
        gave_ = false;
        if (!pending_) return;
        const bool newFile = slot_.guid.empty();
        slot_ = pending_->slot;
        sceneList_ = std::move(pending_->list);
        pending_.reset();
        if (oldForm_) log::Info("シーンを .rmpf から {} に分けました", slot_.path);
        oldForm_ = false;
        sceneUnread_ = false;
        replaces_.clear();
        if (newFile) host_.AssetOf(FullPath(slot_.path));  // id が分かった (見るのを待たずに)
    }

    // --- Play --------------------------------------------------------------------------------
    void Tick(ui::Context& c) override {
        // スクリプトをもう一度ビルドした: ここで、フレームの間に入れ替える (Play 中は入れ替えない: Stop で)。
        // (一覧の編集: ヘッダーとビルドを 1 秒後に - Play 自身の値からは決してしない: それらは
        // Stop で戻る)
        if (!Running()) scripts_.SetGlobals(settings_.globals);
        scripts_.Poll(c);
        if (debugWindow_ && !debugWindow_->IsOpen()) debugWindow_.reset();  // (× / Alt+F4 で閉じた)
        if (settingsWindow_ && !settingsWindow_->IsOpen()) settingsWindow_.reset();
        // (Play 中: Stop で、またはすぐ - ゲーム設定の Play 中のビルド)
        if (auto dll = scripts_.TakeReady(Running() && settings_.scriptReload == ScriptReload::AtStop)) ReloadScripts(*dll);
        if (!heldSceneWarnings_.empty() && !scripts_.Building()) {
            // ビルドに失敗した (新しい DLL がない): シーンが使えなかったものを、今知らせる。
            for (const auto& w : heldSceneWarnings_) log::Warn("シーン: {}", w);
            heldSceneWarnings_.clear();
        }
        if (c.Focused() == 0) {
            if (c.KeyPressed(ui::Key::P, true)) Running() ? StopPlay() : StartPlay(c);
            if (c.KeyPressed(ui::Key::P, true, true) && Running()) TogglePause(c);
        }
        scripts_.mouseInGame = [this] { return mouseInGame_; };
        if (play_ == PlayState::Playing && playScene_) {
            // 前のフレームからの時間 (最大 4 分の 1 秒: 時間がかかったフレーム -
            // ウィンドウをドラッグした、ソフトを切り替えた - でゲームが飛ばないように)。
            const double dt = std::clamp(c.Time() - lastTick_, 0.0, 0.25);
            lastTick_ = c.Time();
            // このフレームの左ボタンの押下 (ウィンドウのメッセージ: フレームより短いクリックも)
            // - 押すのはゲームビューの上だけ。
            pointerWentDown_ = mouseInGame_ && c.In().pressed[0];
            pointerWentUp_ = c.In().released[0];
            const double t0 = platform::NowSeconds();
            AdvancePlay(dt);
            updateMeter_.Frame(t0, (platform::NowSeconds() - t0) * 1000.0);
            playGraph_.Push(t0, (platform::NowSeconds() - t0) * 1000.0);
            mouseInGame_ = false;  // (ゲームビューは、描かれるときにもう一度言う)
            c.RequestAnimation();
        }
    }

    // ▶ ⏸ ⏭ ■ をタブの行の真ん中に置く。その隣に、今していることも出す。
    void DrawToolbar(ui::Context& c, const Rect& r) override {
        constexpr float kB = 30, kGap = 4;
        const float groupW = kB * 4 + kGap * 3;
        float x = std::max(r.x, r.x + (r.w - groupW) * 0.5f);
        const float y = r.y + (r.h - 26) * 0.5f;
        auto next = [&] {
            const Rect b{x, y, kB, 26};
            x += kB + kGap;
            return b;
        };
        const bool running = Running();
        if (editor::w::IconToggle(c, "play", next(), editor::w::Icon::Play, play_ == PlayState::Playing, true,
                                  running ? Tr(L"続ける (Ctrl+P で停止)") : Tr(L"Play (Ctrl+P)"))) {
            if (play_ == PlayState::Paused) {
                TogglePause(c);
            } else if (!running) {
                StartPlay(c);
            }
        }
        if (editor::w::IconToggle(c, "pause", next(), editor::w::Icon::Pause, play_ == PlayState::Paused, running,
                                  Tr(L"一時停止 (Ctrl+Shift+P)"))) {
            TogglePause(c);
        }
        if (editor::w::IconToggle(c, "step", next(), editor::w::Icon::Step, false, running, Tr(L"1 コマ進める (一時停止して)"))) {
            StepFrame(c);
        }
        if (editor::w::IconToggle(c, "stop", next(), editor::w::Icon::Stop, false, running, Tr(L"停止 (Play の前に戻る)"))) {
            StopPlay();
        }
        if (r.Right() - x > 80) {
            ui::TextStyle ts;
            ts.size = 12;
            ts.color = theme::Text;
            std::wstring text;
            if (running) {
                text = play_ == PlayState::Paused ? Tr(L"一時停止中 — ■ で止めると Play の前に戻ります")
                                                  : Tr(L"Play 中 — インスペクターで変えた値も、■ で止めると Play の前に戻ります");
            }
            // Play 中にビルドしたスクリプト: Stop から使う。
            if (running && scripts_.WaitingForStop()) text = Tr(L"スクリプトをビルドしました — Stop で反映されます");
            if (scripts_.Building()) text += (text.empty() ? L"" : L"   ") + Tr(L"スクリプトをビルドしています…");
            if (!text.empty()) c.P().Text({x + 8, r.y, r.Right() - x - 16, r.h}, text, ts);
        }
    }

    bool Running() const override { return play_ != PlayState::Stopped; }

    // エディターの デバッグ メニュー: ゲームデバッグ がデバッグ ウィンドウを開く / 閉じる。
    std::vector<DebugMenuItem> DebugMenu() override {
        DebugMenuItem item;
        item.label = Tr(L"ゲームデバッグ");
        item.checked = debugWindow_ && debugWindow_->IsOpen();
        item.run = [this] { ToggleDebugWindow(); };
        return {std::move(item)};
    }

    // --- tree::Source (the 階層) ----------------------------------------------------------------
    void Items(std::vector<tree::Item>& out) override {
        for (const TreeEntry& t : Shown().TreeOrder()) {
            tree::Item it;
            it.id = static_cast<tree::ItemId>(t.entity->data.id);
            it.parent = static_cast<tree::ItemId>(t.entity->data.parent);
            it.name = t.entity->data.name;
            it.dimmed = !t.activeInHierarchy;
            it.icon = tree::Icon::Object;
            for (const Component& c : t.entity->components) {
                const ComponentLook* look = c.Known() ? LookOf(c.type) : nullptr;
                if (look && look->icon) it.icon = tree::IconNamed(look->icon);
            }
            out.push_back(std::move(it));
        }
    }

    // 一番上の行: 開いているシーン (その名前)。選ぶとシーン自身の値が出る (InspectScene)。
    std::optional<TopRow> Top() const override {
        std::string name = SceneNameOf(slot_.path);
        return TopRow{name.empty() ? Tr(L"シーン") : Utf8ToWide(name), editor::w::Icon::Scene, inspectingScene_};
    }
    void TopClicked() override { InspectScene(); }
    // 一番上の行の右クリック: 開いているシーンの複製・削除 (シーン パネルの行と同じもの)。
    std::vector<tree::Action> TopActions() override { return SceneFileActions(AssetRef{slot_.guid}); }

    std::wstring EmptyText() const override {
        return Tr(L"エンティティはまだありません。\nここを右クリックして「追加」から作れます。");
    }

    bool CanRename(tree::ItemId id) const override { return !Running() && scene_.Find(static_cast<int>(id)) != nullptr; }

    void Rename(tree::ItemId id, const std::string& name) override {
        if (Running()) return;
        // インスペクターで 名前 を編集するのと同じ欄、同じコマンド。
        const reflect::TypeDesc& t = reflect::TypeOf<Entity>();
        SceneEntity* e = scene_.Find(static_cast<int>(id));
        if (!e) return;
        for (size_t i = 0; i < t.fields.size(); ++i) {
            if (std::string_view(t.fields[i].name) != "name") continue;
            json::Value before = reflect::SaveField(t.fields[i], &e->data, &e->unknowns);
            host_.History().Push(std::make_unique<reflect::SetFieldCommand>(t, reflect::FieldPath{i}, std::move(before), json::Value(name),
                                                                            ResolveEntity(static_cast<int>(id)),
                                                                            std::format("「{}」の名前を変更", e->data.name)),
                                 false);
            return;
        }
    }

    bool CanMove() const override { return !Running(); }

    void Move(std::span<const tree::ItemId> items, tree::ItemId parent, size_t index) override {
        if (Running()) return;
        const std::vector<int> ids = ToEntityIds(items);
        EditTouching(ids, ids.size() == 1 ? std::format("「{}」を移動", NameOf(ids[0])) : std::format("{} 個のエンティティを移動", ids.size()),
             [&] { scene_.Move(ids, static_cast<int>(parent), index); });
    }

    void SelectionChanged(std::span<const tree::ItemId> ids) override {
        selection_.clear();
        for (const tree::ItemId id : ids) selection_.push_back(static_cast<int>(id));
        if (!ids.empty()) inspectingScene_ = false;
        if (ids.size() == 1) {
            host_.Inspect(EntityInspectable(static_cast<int>(ids[0])));
            inspectingEntities_ = true;
        } else if (ids.size() > 1) {
            host_.Inspect(ManyInspectable(ToEntityIds(ids)));
            inspectingEntities_ = true;
        } else if (inspectingEntities_) {
            host_.Inspect({});
            inspectingEntities_ = false;
        }
    }

    std::vector<tree::Action> Actions(std::span<const tree::ItemId> selection) override {
        std::vector<tree::Action> list = EditActions(ToEntityIds(selection));
        if (Running()) {
            // Play: シーンは変えない (コピーが動く)。行は残し、灰色にして、注意書きの下に置く。
            for (auto& a : list) {
                a.enabled = false;
                a.run = nullptr;
            }
            tree::Action note;
            note.label = Tr(L"Play 中は編集できません (■ で止めてから)");
            note.enabled = false;
            tree::Action sep;
            sep.separator = true;
            list.insert(list.begin(), {note, sep});
        }
        return list;
    }

private:
    std::vector<tree::Action> EditActions(const std::vector<int>& sel) {
        std::vector<tree::Action> list;

        // 追加 ▸: 空のエンティティ、それからプラグインが作るものをそのフォルダの中に (3D オブジェクト ▸ 箱 ...)。
        const std::wstring add = Tr(L"追加");
        tree::Action create;
        create.label = Tr(L"空のエンティティ");
        create.folder = add;
        create.key = ui::Key::N;
        create.ctrl = create.shift = true;
        create.run = [this, sel] {
            const auto [parent, index] = After(sel);
            CreateEntity(parent, index);
        };
        list.push_back(std::move(create));

        tree::Action child;
        child.label = Tr(L"空の子エンティティ");
        child.folder = add;
        child.enabled = sel.size() == 1;
        if (child.enabled) child.run = [this, sel] { CreateEntity(sel[0], Scene::kEnd); };
        list.push_back(std::move(child));

        std::vector<const CreateItemInfo*> items;
        for (const auto& ci : host_.Registry().Contributions<CreateItemInfo>(kCreateItemPoint)) {
            if (ci.item->label && (ci.item->make || ci.item->fill)) items.push_back(ci.item);
        }
        std::stable_sort(items.begin(), items.end(), [](const CreateItemInfo* a, const CreateItemInfo* b) { return CreateRank(a->folder) < CreateRank(b->folder); });
        bool line = false;
        for (const CreateItemInfo* info : items) {
            tree::Action make;
            make.label = Tr(info->label);
            make.folder = add;
            if (info->folder) {
                for (const std::wstring& part : SplitPath(info->folder)) make.folder += L"/" + Tr(part);
            } else if (!line) {  // (空のものと残りの間の線)
                tree::Action l;
                l.separator = true;
                l.folder = add;
                list.push_back(std::move(l));
                line = true;
            }
            make.run = [this, sel, info] { CreateItem(*info, sel); };
            list.push_back(std::move(make));
        }

        tree::Action sep;
        sep.separator = true;
        list.push_back(sep);

        // クリップボード: エンティティを JSON のテキスト (ファイルの形) で。別のプロジェクトも取れる。
        tree::Action copy;
        copy.label = Tr(L"コピー");
        copy.key = ui::Key::C;
        copy.ctrl = true;
        copy.enabled = !sel.empty();
        if (copy.enabled) copy.run = [this, sel] { CopyEntities(sel); };
        list.push_back(std::move(copy));

        tree::Action cut;
        cut.label = Tr(L"切り取り");
        cut.key = ui::Key::X;
        cut.ctrl = true;
        cut.enabled = !sel.empty();
        if (cut.enabled) {
            cut.run = [this, sel] {
                if (!CopyEntities(sel)) return;
                EditTouching(sel, sel.size() == 1 ? std::format("「{}」を切り取り", NameOf(sel[0])) : std::format("{} 個のエンティティを切り取り", sel.size()),
                     [&] { scene_.Delete(sel); });
            };
        }
        list.push_back(std::move(cut));

        tree::Action paste;
        paste.label = Tr(L"貼り付け");
        paste.key = ui::Key::V;
        paste.ctrl = true;
        paste.run = [this, sel] {
            // 選んだ (最後の) エンティティの直後に、その兄弟として。何も選んでいないとき: 最上位の末尾。
            const auto [parent, index] = After(sel);
            PasteEntities(parent, index);
        };
        list.push_back(std::move(paste));

        tree::Action pasteChild;
        pasteChild.label = Tr(L"子として貼り付け");
        pasteChild.enabled = sel.size() == 1;
        if (pasteChild.enabled) pasteChild.run = [this, sel] { PasteEntities(sel[0], Scene::kEnd); };
        list.push_back(std::move(pasteChild));

        PrefabActions(sel, list);

        if (sel.empty()) return list;
        list.push_back(sep);

        tree::Action dup;
        dup.label = Tr(L"複製");
        dup.key = ui::Key::D;
        dup.ctrl = true;
        dup.run = [this, sel] {
            std::vector<int> made;
            EditTouching({}, sel.size() == 1 ? std::format("「{}」を複製", NameOf(sel[0])) : std::format("{} 個のエンティティを複製", sel.size()),
                 [&] { made = scene_.Duplicate(sel); });
            host_.SelectInTree({made.begin(), made.end()});
        };
        list.push_back(std::move(dup));

        const bool anyParent = std::any_of(sel.begin(), sel.end(), [&](int id) {
            const SceneEntity* e = scene_.Find(id);
            return e && e->data.parent != 0;
        });
        tree::Action unparent;
        unparent.label = Tr(L"親から外す (一番上に出す)");
        unparent.enabled = anyParent;
        if (anyParent) {
            unparent.run = [this, sel] {
                EditTouching(sel, std::format("{} 個のエンティティを一番上に出す", scene_.Roots(sel).size()),
                     [&] { scene_.Move(sel, 0, Scene::kEnd); });
            };
        }
        list.push_back(std::move(unparent));

        list.push_back(sep);
        tree::Action del;
        del.label = Tr(L"削除");
        del.key = ui::Key::Delete;
        del.run = [this, sel] {
            EditTouching(sel, sel.size() == 1 ? std::format("「{}」を削除", NameOf(sel[0])) : std::format("{} 個のエンティティを削除", sel.size()),
                 [&] { scene_.Delete(sel); });
        };
        list.push_back(std::move(del));
        return list;
    }

    // 選んだ (最後の) エンティティの直後に、その兄弟として。何も選んでいないとき: 最上位の末尾。
    std::pair<int, size_t> After(const std::vector<int>& sel) const {
        if (!sel.empty()) {
            if (const SceneEntity* e = scene_.Find(sel.back())) {
                const int parent = e->data.parent;
                const std::vector<int> siblings = scene_.Children(parent);
                return {parent, static_cast<size_t>(std::find(siblings.begin(), siblings.end(), sel.back()) - siblings.begin()) + 1};
            }
        }
        return {0, Scene::kEnd};
    }

    // --- Play ----------------------------------------------------------------------------------
    // 見せているシーン: 動いているコピー、またはシーンそのもの。
    Scene& Shown() { return playScene_ ? *playScene_ : scene_; }
    const Scene& Shown() const { return playScene_ ? *playScene_ : scene_; }

    void StartPlay(ui::Context& c) {
        if (Running()) return;
        // ドラッグの途中ではない (ギズモ、行): 残りが、動いているコピーの下で、シーンと履歴へ
        // 行ってしまうから。
        const ui::Input& in = c.In();
        if (in.down[0] || in.down[1] || in.down[2]) {
            log::Info("ドラッグ中は Play を始めません (ボタンを離してから)");
            return;
        }
        playScene_ = std::make_unique<Scene>(scene_);  // 動かすコピー (部品はディープコピー)
        runner_.Reset();
        runner_.SetCamera(SceneCamera(*playScene_, cameraTypes_, settings_.startCamera.id));  // システムが切り替えてよい (SystemContext::camera)
        scripts_.Runner().Reset();
        scripts_.World().time = 0;
        scripts_.World().unscaledTime = 0;
        scripts_.World().frame = 0;
        scripts_.World().timeScale = 1;
        scripts_.World().layers = &settings_.layers;
        debug_.Clear();
        scripts_.World().debug = &debug_;  // エディターが rm::Debug の線を見せる (書き出したゲームにはない)
        // Game::LoadScene が切り替えられるシーン: ゲームの (一覧。これが一覧にないときはこれも)。
        // (書き出したゲームの番号のつけ方と同じ: ビルドから外したものはここにもない)
        playScenes_ = PlayScenes(sceneList_, AssetRef{slot_.guid});
        ScriptWorld& world = scripts_.World();
        world.sceneNames.clear();
        world.sceneIds.clear();
        world.sceneIndex = -1;
        world.sceneAsked = -1;
        world.quitAsked = false;
        world.worldAsked = json::Value();
        world.worldAskedScene = -1;
        world.sceneBase = playScene_->NextId();  // (世界のセーブ: これ以上の id は Play の中で作った物)
        scripts_.StartInput();
        for (size_t i = 0; i < playScenes_.size(); ++i) {
            world.sceneNames.push_back(SceneTitle(playScenes_[i]));
            world.sceneIds.push_back(playScenes_[i].guid);
            if (playScenes_[i] == AssetRef{slot_.guid}) world.sceneIndex = static_cast<int>(i);
        }
        scripts_.World().actions = &settings_.input;  // (Play 中に編集: フレームごとに今のものを読む)
        scripts_.World().prefab = [this](const AssetRef& r) { return Cached(r); };  // rm::Spawn(prefab): 最後に読んだファイル
        settingsBeforePlay_ = reflect::Save(settings_, &unknowns_);  // (Play のインスペクターで変更: ■ で戻る)
        ResetGlobals(true);  // すべてのグローバル変数が 初めの値 に。rm::Save は Library/Saves へ
        {
            // スクリプトが保存したプレイヤーのキー割り当て (rm::Input::SaveBindings)。ゲームが読むとおりに。
            std::string why;
            if (!world.LoadBindings(&why) && !why.empty()) log::Warn("入力の割り当て (input.json) を読めません: {}", why);
        }
        kept_.clear();       // (この値を残す: この Play のものだけ)
        keptLeft_.clear();
        StartServices();     // 音 ... (プラグインの PlayServices): ■ まで
        play_ = PlayState::Playing;
        lastTick_ = c.Time();
        EnsureViews();  // ゲームビューは、ゲーム設定の 最初のカメラ を通して見る
        log::Info("▶ Play (システム {} 個)。止めると Play の前のシーンに戻ります", runner_.Systems().size());
        c.RequestAnimation();
        host_.Redraw();
    }

    // システムの 1 フレーム。切り替えたカメラはコンソールに知らせる。
    void AdvancePlay(double dt) {
        const uint64_t before = runner_.Camera();
        // スクリプトが先 (入力 -> スクリプトの Tick -> 固定ステップ ...)。カメラもスクリプトが切り替えてよい。
        ScriptWorld& world = scripts_.World();
        world.camera = runner_.Camera();
        // ゲームの時間: 実際の秒数に時間の倍率 (rm::SetTimeScale) をかけたもの。ゲーム UI は実際の秒数を使う。
        const double realDt = dt;
        dt *= world.timeScale;
        world.unscaledDt = static_cast<float>(realDt);
        world.keyboardPlayer = std::clamp(settings_.keyboardPlayer - 1, 0, kPlayers - 1);  // (Play 中に編集: フレームごとに読む)
        world.SetPointer(pointer_, GameScreen());
        // (デバッグ パネルを出しているとき: 各部分の時間を測る - スクリプトは型ごとに、システムは 1 つずつ)
        const bool timing = platform::NowSeconds() - debugShownAt_ < 1.0;
        scripts_.Runner().SetTiming(timing);
        runner_.SetTiming(timing);
        PlayTimes times;
        double mark = platform::NowSeconds();
        auto lap = [&](double& into) {
            const double t = platform::NowSeconds();
            into += (t - mark) * 1000.0;
            mark = t;
        };
        const double start = mark;
        debug_.BeginFrame(dt);  // 前のフレームの線を消し (秒数が過ぎたものも)、それからスクリプトが今回の線を描く
        scripts_.Runner().Frame(*playScene_, world, scripts_.Module(), settings_.scriptOrder, static_cast<float>(dt));
        lap(times.scripts);
        runner_.SetCamera(world.camera);
        runner_.Advance(*playScene_, dt, settings_.fixedTimestep);
        lap(times.systems);
        // システムの固定ステップがスクリプトに伝えること (物理の接触 ...)。
        scripts_.Runner().Deliver(*playScene_, world, scripts_.Module(), settings_.scriptOrder, runner_.Hooks());
        lap(times.hooks);
        // サービス (音、ゲーム UI ...): ゲーム画面はゲーム設定の 画面 のとおり、マウスは
        // ゲームビューの上のもの。サービスが頼むフック (ボタンの OnClick) は、今スクリプトへ渡す。
        std::vector<HookCall> asked;
        PlayFrame pf{*playScene_, runner_.Camera(), static_cast<float>(realDt)};
        pf.screen = GameScreen();
        pf.pointer = pointer_;
        pf.pointerWentDown = pointerWentDown_;
        pf.pointerWentUp = pointerWentUp_;
        pf.input = &world.inputNow;
        pf.inputBefore = &world.inputBefore;
        pf.actions = &world.Actions();  // (プレイヤーのキー割り当て)
        pf.hooks = &asked;
        pf.world = &world;
        // ゲーム UI の入力欄に打った文字 (ゲームビューが集めた: TypeIntoGame)。サービスが「打っている」と
        // 言えば、ゲームビューがこのウィンドウの IME をつなぐ。
        pf.typed = &typed_;
        TextInputAsk textAsk;
        pf.textInput = &textAsk;
        services_.Frame(pf);
        textAsk_ = textAsk;
        typed_.chars.clear();
        typed_.keys.clear();
        typed_.imeResult.clear();
        lap(times.services);
        scripts_.Runner().Deliver(*playScene_, world, scripts_.Module(), settings_.scriptOrder, asked);
        lap(times.hooks);
        times.total = (platform::NowSeconds() - start) * 1000.0;
        playTimes_ = times;
        if (timing) {
            scriptTimes_ = scripts_.Runner().Times();
            systemTimes_ = runner_.Times();
        }
        if (std::exchange(world.quitAsked, false)) {
            // ゲームを終わる: ゲームが閉じることになる - Play が止まる (■ と同じ)。
            log::Info("ゲームを終わる → Play を止めます (書き出したゲームではウィンドウが閉じます)");
            StopPlay();
            return;
        }
        // スクリプトが頼んだシーン (Game::LoadScene): フレームの間に、動いているコピーの中で切り替える
        // - 編集中のシーンはそのまま (■ でそこへ戻る)。
        const std::string from = world.sceneIndex >= 0 ? world.sceneNames[static_cast<size_t>(world.sceneIndex)] : std::string();
        std::string err;
        // Play 前のシーンの この値を残す: 切り替えで離れるときの値 (そのエンティティの id は、
        // 切り替えた後は別のシーンのものになる)。
        std::vector<std::pair<Kept, json::Value>> leaving;
        if (world.sceneAsked >= 0 && PlayingOwnScene()) leaving = ReadKept();
        const SceneSwitch sw = SwitchAskedScene(*playScene_, world, scripts_.Runner(), scripts_.Module(), settings_.scriptOrder,
                                                [this](size_t index, Scene& into, std::string* why) { return ReadPlayScene(index, into, why); }, &err);
        if (sw == SceneSwitch::Switched) {
            for (auto& kv : leaving) keptLeft_.push_back(std::move(kv));
            kept_.clear();
            services_.SceneSwitched();
            runner_.SetCamera(SceneCamera(*playScene_, cameraTypes_, settings_.startCamera.id));
            log::Info("シーンを切り替えました: {} → {} (Play の中だけ。■ で {} に戻ります)", from, world.sceneNames[static_cast<size_t>(world.sceneIndex)],
                      SceneTitle(AssetRef{slot_.guid}));
            host_.SelectInTree({});
            host_.Redraw();
            return;
        }
        if (sw == SceneSwitch::Failed) log::Error("シーンを切り替えられません: {}", err);
        if (runner_.Camera() != before) {
            const SceneEntity* e = playScene_->Find(static_cast<int>(runner_.Camera()));
            log::Info("ゲームのカメラを「{}」に切り替えました", e ? e->data.name : std::string("(なし)"));
        }
    }

    // Play のために読んだゲームのシーン (Game::LoadScene): 開いているものは今の状態、ほかは
    // そのファイルから (保存されたまま)。
    bool ReadPlayScene(size_t index, Scene& into, std::string* error) {
        if (index >= playScenes_.size()) return false;
        const AssetRef& ref = playScenes_[index];
        if (ref == AssetRef{slot_.guid}) {
            into.Load(scene_.Save());
            return true;
        }
        const fs::path file = host_.AssetFile(ref);
        std::string err;
        const auto bytes = file.empty() ? std::nullopt : platform::ReadFileBytes(file, &err);
        const auto scene = bytes ? ReadSceneFile(*bytes, &err, &host_.Registry()) : std::nullopt;
        if (!scene) {
            if (error) *error = std::format("{}: {}", SceneTitle(ref), file.empty() ? TrU8("ファイルが Assets に見つかりません") : err);
            return false;
        }
        reflect::Warnings warnings;
        into.Load(*scene, &warnings);
        reflect::Warnings prefabWarnings;
        RefreshPrefabsIn(into, prefabWarnings);  // (その Prefab は今のファイルのとおりに。開いたときと同じ)
        for (const auto& w : warnings) log::Warn("シーン: {}", w);
        for (const auto& w : prefabWarnings) log::Warn("Prefab: {}", w);
        return true;
    }

    void StopPlay() {
        if (!Running()) return;
        textAsk_ = {};  // (ゲーム UI の入力欄: 打つのは終わり。フォーカスは次に描くとき離す)
        typed_ = {};
        log::Info("■ 停止 ({:.2f} 秒・{} コマ)。シーンは Play の前に戻りました", runner_.Time(), runner_.Frames());
        const auto kept = TakeKept();  // (見せていたとおりに: スクリプトの EndPlay の前)
        scripts_.Runner().Stop(*playScene_, scripts_.World(), scripts_.Module(), settings_.scriptOrder);  // EndPlay
        scripts_.World().services = nullptr;
        scripts_.StopInput();  // (すべてのパッドを静止)
        scriptTimes_.clear();
        systemTimes_.clear();
        scripts_.World().playerActions.reset();
        scripts_.World().timeScale = 1;
        services_.Stop();    // すべての音が止まる (■: これ以後ゲームの音は何も聞こえない)
        playScene_.reset();  // (Play 中にビルドしたスクリプトは次のフレームで取り込む: Play 前のシーンに対して)
        // Play の前のゲーム設定 (Play 中はインスペクターがその場で変える)。
        if (!settingsBeforePlay_.IsNull()) {
            if (reflect::Save(settings_, &unknowns_) != settingsBeforePlay_) {
                reflect::Load(settings_, settingsBeforePlay_, &unknowns_);
                log::Info("Play 中に変えたゲームの設定を、Play の前に戻しました");
            }
            settingsBeforePlay_ = json::Value();
        }
        debug_.Clear();      // Stop: デバッグの線がすべて消える
        ResetGlobals(false);  // Play の前のとおりに (保存したものは Library/Saves に残る)
        play_ = PlayState::Stopped;
        KeepValues(kept);  // この値を残す: Play 前のシーンへ (Undo の項目は 1 つ)
        host_.SelectInTree({});  // (選択は、切り替えたシーンのエンティティを指しているかもしれない)
        host_.Redraw();
    }

    // この Play の、プラグインの PlayServices: アセットはそのファイルから (今の状態) と、
    // その .meta の取り込みの設定で。
    void StartServices() {
        PlayHost play;
        play.registry = &host_.Registry();
        play.settings = &settings_;
        play.asset = [this](const AssetRef& ref) -> std::optional<std::string> {
            const fs::path file = host_.AssetFile(ref);
            if (file.empty()) return std::nullopt;
            std::string err;
            auto bytes = platform::ReadFileBytes(file, &err);
            if (!bytes) log::Warn("アセット ({}) を読めません: {}", PathToUtf8(file.filename()), err);
            return bytes;
        };
        play.import = [this](const AssetRef& ref) {
            const fs::path file = host_.AssetFile(ref);
            return file.empty() ? json::Value::MakeObject() : asset::ReadImportJson(file);
        };
        play.name = [this](const AssetRef& ref) { return PathToUtf8(host_.AssetFile(ref).filename()); };
        play.log = [](int level, std::string_view t) {
            if (level >= 2) {
                log::Error("{}", t);
            } else if (level == 1) {
                log::Warn("{}", t);
            } else {
                log::Info("{}", t);
            }
        };
        services_.Start(std::move(play));
        scripts_.World().services = &services_;
    }

    // エディターの保存 (Library/Saves/save_*.json) をごみ箱へ。
    void DeleteSaves() {
        const fs::path folder = ProjectSaveFolder(host_.GetProject().Root());
        std::vector<fs::path> files;
        std::error_code ec;
        for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            const std::wstring name = it->path().filename().native();
            if (name.starts_with(L"save_") && it->path().extension() == L".json") files.push_back(it->path());
        }
        if (files.empty()) {
            log::Info("消すセーブはありません ({})", PathToUtf8(folder));
            return;
        }
        std::string err;
        if (platform::MoveToRecycleBin(files, &err)) {
            log::Info("セーブを {} 個ごみ箱へ移しました ({})", files.size(), PathToUtf8(folder));
        } else {
            log::Error("セーブを消せませんでした: {}", err);
        }
    }

    // ゲームのグローバル変数を 初めの値 に戻す (Play と Stop)。型に合わない 初めの値 は、
    // Play で知らせる。
    void ResetGlobals(bool tell) {
        std::vector<std::string> warnings;
        ScriptWorld& world = scripts_.World();
        world.globals.Reset(settings_.globals, &warnings);
        world.saveFolder = ProjectSaveFolder(host_.GetProject().Root());
        if (tell) {
            for (const std::string& w : warnings) log::Warn("{}", w);
        }
    }

    void TogglePause(ui::Context& c) {
        if (!Running()) return;
        play_ = play_ == PlayState::Paused ? PlayState::Playing : PlayState::Paused;
        services_.SetPaused(play_ == PlayState::Paused);  // (音は位置を保つ)
        lastTick_ = c.Time();  // 一時停止していた時間は数えない
        c.RequestAnimation();
    }

    // 1 フレーム (1 / 目標 FPS 秒) 進めて、一時停止。
    void StepFrame(ui::Context& c) {
        if (!Running() || !playScene_) return;
        play_ = PlayState::Paused;
        services_.SetPaused(true);
        AdvancePlay(1.0 / std::max(1, settings_.targetFps));
        lastTick_ = c.Time();
        host_.Redraw();
    }

    // ビューは、空間を知っているプラグイン (ゲーム制作 × 3D / × 2D) から来る: 一度だけ聞く。
    // 各空間にそれぞれのシーンビューとゲームビューがある (それぞれカメラを持つ)。
    void EnsureViews() {
        if (viewsAsked_) return;
        viewsAsked_ = true;
        for (const auto& info : host_.Registry().Contributions<SceneViewInfo>(kSceneViewPoint)) {
            if (!info.item->create || !info.item->space) continue;
            View v{info.item, info.item->create(), info.item->create()};
            if (!v.scene || !v.game) continue;
            views_.push_back(std::move(v));
        }
        std::stable_sort(views_.begin(), views_.end(), [](const View& a, const View& b) { return a.info->order < b.info->order; });
        for (View& v : views_) v.scene->LoadState(ViewState(v));
    }

    // ビューに持つ状態。空間ができる前は 1 つの "sceneView" だった (既定のビューのもの)。
    json::Value ViewState(const View& v) const {
        const json::Value& states = view_.Get("sceneViews");
        if (states.IsObject() && !states.Get(v.info->space).IsNull()) return states.Get(v.info->space);
        if (!views_.empty() && &v == &views_.front()) return view_.Get("sceneView");
        return {};
    }

    // ゲームの空間: ゲームが選んだビュー。何も選んでいない、またはそのプラグインが今ないときは
    // 既定のもの (最初のもの)。ビューを出すプラグインがなければ null。
    View* Current() {
        EnsureViews();
        for (View& v : views_) {
            if (settings_.space == v.info->space) return &v;
        }
        return views_.empty() ? nullptr : &views_.front();
    }
    std::string CurrentSpace() {
        const View* v = Current();
        return v ? v->info->space : std::string{};
    }

    // ゲームの設定 ▸ ゲーム設定 の「ゲームの空間」(空間が 2 つ以上あるとき): 切り替えはゲームの編集
    // (GameSettings::space - Undo、未保存): シーンビュー・ゲームビューと新しいエンティティがそれに従う。
    // (2026-11-25 まではシーンビューの上の帯の 3D / 2D だった。今その帯の 3D / 2D は、同じ 3D の
    // シーンを正面から平行投影で見るだけ - シーンビューの見え方で、ゲームは変えない)
    // 描いた高さを返す (出さないときは 0)。
    float DrawSpaceChoice(ui::Context& c, const Rect& r, float y) {
        EnsureViews();
        if (views_.size() < 2) return 0;
        auto& p = c.P();
        ui::TextStyle label;
        label.size = 12;
        label.color = theme::Text;
        label.v = ui::VAlign::Center;
        p.Text({r.x + 14, y, 120, 26}, Tr(L"ゲームの空間"), label);
        auto nameOf = [](const View& v) { return v.info->label ? Tr(v.info->label) : Utf8ToWide(v.info->space); };
        const View* now = Current();
        const Rect combo{r.x + 140, y, std::max(80.f, r.w - 160), 26};
        if (editor::w::ComboBox(c, "game_space", combo, now ? nameOf(*now) : std::wstring{}, !Running()).clicked) spacePopup_.OpenAt(combo.x, combo.Bottom());
        if (c.IsHovered(combo)) {
            editor::w::Tooltip(c, Running() ? Tr(L"Play 中は切り替えられません")
                                            : Tr(L"このゲームの空間。新しいエンティティに付く Transform と、シーンビュー・ゲームビューもこれに合わせます"));
        }
        if (spacePopup_.open && Running()) spacePopup_.open = false;
        if (spacePopup_.open) {
            std::vector<editor::w::MenuItem> items;
            for (const View& v : views_) {
                editor::w::MenuItem it;
                it.label = nameOf(v);
                it.checked = &v == now;
                items.push_back(std::move(it));
            }
            const int chosen = editor::w::MenuPopup(c, spacePopup_, items, combo.w);
            if (chosen >= 0 && static_cast<size_t>(chosen) < views_.size() && &views_[static_cast<size_t>(chosen)] != now) {
                ChooseSpace(views_[static_cast<size_t>(chosen)].info->space);
            }
        }
        float h = 30;
        ui::TextStyle note;
        note.size = 12;
        note.color = theme::TextDim;
        note.wrap = true;
        const std::wstring text = Tr(L"シーンビューの上の 3D / 2D は見え方だけを変えます (2D: 同じシーンを正面から平行投影で)。2D 空間のゲームを作るときは、ここで 2D を選びます");
        const float th = p.TextHeight(text, 12, r.w - 28);
        p.Text({r.x + 14, y + h, r.w - 28, th}, text, note);
        h += th + 10;
        return h;
    }

    // ゲームビューの上 (停止中): 止まっていること、それとゲーム設定 (最初のカメラ ...)。
    void DrawStoppedStrip(ui::Context& c, const Rect& strip) {
        editor::w::DrawBar(c, strip, theme::Bg);
        constexpr float kButtonW = 150;
        const Rect button{strip.Right() - 8 - kButtonW, strip.y + 4, kButtonW, strip.h - 8};
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextDim;
        ts.v = ui::VAlign::Center;
        c.P().Text({strip.x + 10, strip.y, button.x - strip.x - 18, strip.h}, Tr(L"止まっています (▶ / Ctrl+P で Play)。最初のカメラから見たシーン"), ts);
        if (editor::w::Button(c, "game_settings", button, Tr(L"ゲームの設定を開く"))) InspectSettings();
        // スクリプト: 今ビルドする、Visual Studio で書く。
        const Rect vs{button.x - 6 - kButtonW, button.y, kButtonW, button.h};
        const Rect build{vs.x - 6 - kButtonW, button.y, kButtonW, button.h};
        if (build.x > strip.x + 200) {
            if (editor::w::Button(c, "build_scripts", build, scripts_.Building() ? Tr(L"ビルドしています…") : Tr(L"スクリプトをビルド"),
                                  editor::w::ButtonKind::Secondary, !scripts_.Building())) {
                scripts_.Build();
            }
            if (editor::w::Button(c, "open_vs", vs, Tr(L"Visual Studio で開く"))) scripts_.OpenVisualStudio();
        }
    }

    // 部品の型: プラグインのものとスクリプトのもの (スクリプトは何度でも付けてよい)。
    void UseScriptTypes() {
        std::vector<const reflect::TypeDesc*> types = baseTypes_, multiple = baseMultiple_;
        for (const reflect::TypeDesc* t : scripts_.Module().Types()) {
            types.push_back(t);
            multiple.push_back(t);
        }
        scene_.SetComponentTypes(std::move(types), std::move(multiple));
    }

    // ホットリロード (フレームの境目): 値を名前で書き出し、古い DLL のオブジェクトを消し、新しい
    // DLL を読み込んで、読み戻す。新しいフィールドは既定値になり、なくなったフィールドは
    // ファイルに残り、なくなったスクリプトは値を持ったまま「ない」ものとして残る (Scene: unknown
    // component)。Play 中 (Play 中のビルド = すぐ入れ替える) は、動いているコピーにも同じことをする:
    // 始まっていたスクリプトは始まったままで、BeginPlay の代わりに OnReloaded を受ける。
    void ReloadScripts(const std::filesystem::path& dll) {
        std::unique_ptr<ScriptModule> next = scripts_.LoadModule(dll);
        if (!next) return;  // (知らせた) 古い DLL はそのまま動く
        // コルーチンのフレームは古い DLL のもの: DLL が消える前に止める (OnReloaded がまた始めてよい)。
        if (!scripts_.World().coroutines.empty()) {
            log::Info("スクリプトを入れ替えたので、動いていたコルーチン {} 個を止めました", scripts_.World().coroutines.size());
            scripts_.World().ClearCoroutines();
        }
        const json::Value saved = scene_.Save();
        scene_.SetComponentTypes(baseTypes_, baseMultiple_);
        scene_.Load(saved);  // 古いスクリプトのオブジェクトが消える (読んだままに保つ)
        std::vector<ScriptRunner::RunState> running;
        json::Value playing;
        if (playScene_) {
            running = scripts_.Runner().Remember(*playScene_);
            playing = playScene_->Save();
            playScene_->SetComponentTypes(baseTypes_, baseMultiple_);
            playScene_->Load(playing);
        }
        // 履歴の編集は古い型を指す (そのコードは古い DLL にある): それらも消す。
        if (host_.History().UndoCount() + host_.History().RedoCount() > 0) {
            host_.History().Clear();
            log::Info("スクリプトを読み直したので、元に戻す (Undo) の履歴を消しました");
        }
        scriptTimes_.clear();  // (古い DLL の型を指している)
        scripts_.Swap(std::move(next));
        UseScriptTypes();
        reflect::Warnings warnings;
        scene_.Load(saved, &warnings);
        heldSceneWarnings_.clear();  // (読み直した: 今知らせるものが、開いたときに持っていたものと入れ替わる)
        for (const auto& w : warnings) log::Warn("シーン: {}", w);
        if (playScene_) {
            playScene_->SetComponentTypes(scene_.ComponentTypes(), scene_.MultipleTypes());
            playScene_->Load(playing);
            scripts_.Runner().Resume(*playScene_, scripts_.World(), scripts_.Module(), settings_.scriptOrder, running);
            log::Info("Play 中にスクリプトを入れ替えました (値は名前で引き継ぎ、OnReloaded を呼びました)");
        }
        ++dataVersion_;
        host_.Redraw();
    }

    void ChooseSpace(const std::string& space) {
        const reflect::TypeDesc& type = reflect::TypeOf<GameSettings>();
        for (size_t f = 0; f < type.fields.size(); ++f) {
            if (std::string_view(type.fields[f].name) != "space") continue;
            json::Value before = reflect::SaveField(type.fields[f], &settings_, nullptr);
            json::Value after = json::Value(space);
            host_.History().Push(std::make_unique<reflect::SetFieldCommand>(
                                     type, reflect::FieldPath{f}, std::move(before), std::move(after),
                                     [this] { return reflect::EditTarget{&settings_, &unknowns_}; }, "ゲームの空間を切り替え"),
                                 false);
            log::Info("ゲームの空間を「{}」にしました (シーンビュー・ゲームビューと、新しいエンティティの Transform も)", space);
            return;
        }
    }

    SceneViewHost ViewHost(bool game) {
        SceneViewHost h{host_, Shown(), selection_,
                        [this](std::vector<int> ids) { host_.SelectInTree(std::vector<tree::ItemId>(ids.begin(), ids.end())); },
                        [this](int id, const std::string& type) { return ResolveComponent(id, type); }};
        h.playing = Running();
        h.timeScale = play_ == PlayState::Playing ? scripts_.World().timeScale : Running() ? 0.f : 1.f;
        h.game = game;
        h.background = settings_.clearColor;
        h.camera = Running() ? runner_.Camera() : SceneCamera(scene_, cameraTypes_, settings_.startCamera.id);  // Play 中: システムが残したとおりに
        h.aspect = static_cast<float>(std::max(1, settings_.display.width)) / static_cast<float>(std::max(1, settings_.display.height));
        h.openSettings = [this] { InspectSettings(); };
        h.settings = &settings_;
        h.debug = Running() ? &debug_ : nullptr;
        h.editScene = [this](std::vector<int> touched, std::string label, std::function<std::vector<int>(Scene&)> change) {
            std::vector<int> select;
            EditTouching(std::move(touched), std::move(label), [&] { select = change(scene_); });
            if (!select.empty()) host_.SelectInTree(std::vector<tree::ItemId>(select.begin(), select.end()));
        };
        h.setting = [this](const std::string& type) -> std::function<reflect::EditTarget()> {
            return [this, type] {
                Component* s = scene_.FindSetting(type);
                return s && s->Known() ? reflect::EditTarget{s->value.Get(), &s->unknowns} : reflect::EditTarget{};
            };
        };
        h.space = CurrentSpace();
        return h;
    }

    // シーンビューは、空間を知っているプラグイン (ゲーム制作 × 3D / × 2D) から来る。なければ注意書き。
    void DrawSceneView(ui::Context& c, const Rect& panel) {
        if (!Current()) {
            Placeholder(c, panel, Tr(L"シーンビュー"),
                        Tr(L"3D 空間 / 2D 空間のプラグインが入ると、ここにシーンが映り、エンティティを置いて動かせます。"));
            return;
        }
        SceneViewHost h = ViewHost(false);
        Current()->scene->Draw(c, panel, h);  // (上の帯はビューが自分で描く: 3D は 3D / 2D と見え方)
        if (play_ == PlayState::Playing) c.RequestAnimation();  // このパネルは専用のウィンドウにあるかもしれない
    }

    // ゲーム自身が見る自分: Play 中は動いているコピー、そうでなければシーンそのもの (上の
    // 帯がそう言い、設定へ行く道も出す)。
    void DrawGameView(ui::Context& c, const Rect& panel) {
        EnsureViews();
        View* view = Current();
        if (!view) {
            Placeholder(c, panel, Tr(L"ゲームビュー"), Tr(L"3D 空間 / 2D 空間のプラグインが入ると、ゲームがここに映ります。"));
            const Rect button{panel.x + (panel.w - 200) * 0.5f, panel.y + panel.h * 0.5f + 70, 200, 36};
            if (editor::w::Button(c, "settings", button, Tr(L"ゲームの設定を開く"))) InspectSettings();
            return;
        }
        Rect r = panel;
        if (!Running() && r.h > 120) DrawStoppedStrip(c, ui::TakeTop(r, kSpaceStripH));
        mouseInGame_ = c.IsHovered(r);  // (ゲームのマウスボタン: ここの上だけ - 次のフレームが読む)
        SceneViewHost h = ViewHost(true);
        view->game->Draw(c, r, h);
        // その上にほかの空間を、その順に (3D のゲームの上に 2D。それを映すカメラがあるとき)。
        for (View& other : views_) {
            if (&other != view) other.game->DrawLayer(c, r, h);
        }
        // 一番上に: ゲーム UI ... - ゲーム画面の矩形に、ゲーム設定の 画面 のとおりに。
        const Rect game = GameScreenRect(r, h.aspect);
        const Vec2 screen = GameScreen();
        const ui::Input& in = c.In();
        pointer_.reset();
        if (mouseInGame_ && game.Contains(in.mouseX, in.mouseY) && game.w > 0 && game.h > 0) {
            pointer_ = Vec2{(in.mouseX - game.x) / game.w * screen.x, (in.mouseY - game.y) / game.h * screen.y};
        }
        DrawScreen(c, game, screen);
        TypeIntoGame(c, game, screen);
        if (play_ == PlayState::Playing) c.RequestAnimation();
    }

    // ゲーム UI の入力欄に打っている間 (サービスがそう言った: textAsk_): このウィンドウのキー・文字・IME を
    // ゲームへ渡す。エディターの欄と同じフォーカスを取るので、その間はエディターのショートカットが効かない。
    // 候補の窓はその欄の下に。
    void TypeIntoGame(ui::Context& c, const Rect& game, Vec2 screen) {
        const ui::Id id = c.MakeId("game_typing");
        if (play_ != PlayState::Playing || !textAsk_.want || game.w < 1 || game.h < 1) {
            if (gameTyping_ && c.Focused() == id) c.ClearFocus();
            gameTyping_ = false;
            return;
        }
        if (!gameTyping_) {
            c.FocusText(id, L"", false);  // (打ち始めた: 欄を押した人はここに打ちたい)
            gameTyping_ = true;
        }
        if (c.Focused() != id) return;  // (エディターのほかの欄に移った: ゲームには何も届かない)
        c.MarkFocusAlive();
        const float kx = game.w / screen.x, ky = game.h / screen.y;
        c.SetTextInput({game.x + textAsk_.x * kx, game.y + textAsk_.y * ky, textAsk_.w * kx, textAsk_.h * ky});
        const ui::Input& in = c.In();
        typed_.chars += in.chars;
        typed_.keys.insert(typed_.keys.end(), in.keys.begin(), in.keys.end());
        typed_.imeResult += in.imeResult;
        typed_.imeComposition = in.imeComposition;
        typed_.imeCursor = in.imeCursor;
    }

    // ゲームの画面 (ゲーム設定の 画面 のとおり、DIP)。
    Vec2 GameScreen() const {
        return {static_cast<float>(std::max(1, settings_.display.width)), static_cast<float>(std::max(1, settings_.display.height))};
    }

    // プラグインがゲーム画面全体に描くもの (ScreenDrawInfo: ゲーム UI) を `game` に。
    void DrawScreen(ui::Context& c, const Rect& game, Vec2 screen) {
        if (!screensAsked_) {
            screensAsked_ = true;
            std::vector<const ScreenDrawInfo*> list;
            for (const auto& d : host_.Registry().Contributions<ScreenDrawInfo>(kScreenDrawPoint)) list.push_back(d.item);
            std::stable_sort(list.begin(), list.end(), [](const ScreenDrawInfo* a, const ScreenDrawInfo* b) { return a->order < b->order; });
            for (const ScreenDrawInfo* s : list) {
                if (s->create) screenDrawers_.push_back(s->create());
            }
        }
        if (screenDrawers_.empty() || game.w < 1 || game.h < 1) return;
        ScreenDrawFrame f{Shown(), settings_, host_.Graphics(), c.P().DC(), game.x, game.y, game.w, game.h, screen,
                          [this](const AssetRef& ref) -> std::optional<std::string> {
                              const std::filesystem::path file = host_.AssetFile(ref);
                              if (file.empty()) return std::nullopt;
                              return platform::ReadFileBytes(file, nullptr);
                          },
                          Running()};
        f.paused = play_ == PlayState::Paused;
        c.P().PushClip(game);
        for (const auto& s : screenDrawers_) s->Draw(f);
        c.P().PopClip();
    }

    // --- インスペクター -----------------------------------------------------------------------------
    // Play の 外す: コピーの部品は次のフレームの終わりに消え、スクリプトの EndPlay が先に走る
    // (rm::Entity::Remove が頼むとおり)。どのみち ■ でコピーごと消える。
    void PlayRemoveComponent(int id, const std::string& type, size_t nth) {
        SceneEntity* e = playScene_ ? playScene_->Find(id) : nullptr;
        Component* c = e ? e->FindComponent(type, nth) : nullptr;
        if (!c) return;
        if (c->Known()) {
            scripts_.World().removeAsked.push_back({id, c->value.Get()});
        } else {
            playScene_->RemoveComponent(id, type, nth);
        }
    }

    // Play 中のインスペクターの上の注意書き (ブロックは見せるだけで、編集しない)。
    static editor::InspectBlock PlayNote() {
        return {nullptr, {}, {}, Tr(L"Play 中です。ここで変えた値は動いているゲームにすぐ効きますが、保存されず、■ で止めると Play の前に戻ります (元に戻す の履歴にも入りません)。欄を右クリック →「この値を残す」で、止めたときの値を Play の前のシーンへ書き戻します。"), {}};
    }

    // Play: フィールドを右クリック → この値を残す (設計 BluePrint 20-b 3)。フィールドは
    // エンティティ、部品 (その型と位置。"" = エンティティ自身のフィールド)、パスで覚える。
    // Stop のとき、そのときの値を Play 前のシーンに書く (KeepValues)。
    struct Kept {
        SceneField field;
        std::string label;
    };
    std::function<std::vector<editor::w::Action>(const reflect::FieldPath&, std::string_view)> KeepMenu(int id, std::string component, size_t nth) {
        return [this, id, component = std::move(component), nth](const reflect::FieldPath& path, std::string_view label) {
            editor::w::Action keep;
            keep.label = Tr(L"この値を残す");
            keep.run = [this, id, component, nth, path, label = std::string(label)] {
                if (!PlayingOwnScene()) {
                    log::Warn("「{}」: 今動いているのは Play の前のシーンではない (シーンを切り替えた) ので、値を残せません", label);
                    return;
                }
                const SceneField f{id, component, nth, path};
                std::erase_if(kept_, [&](const Kept& k) {
                    return k.field.entity == f.entity && k.field.component == f.component && k.field.nth == f.nth && k.field.path == f.path;
                });
                kept_.push_back({f, label});
                log::Info("「{}」の値を、止めたときに Play の前のシーンへ書き戻します", label);
            };
            return std::vector<editor::w::Action>{keep};
        };
    }

    // 動いているシーンが Play 前のもの (Game::LoadScene が切り替えた先ではない) のとき: その
    // エンティティだけが、編集中のシーンのもの。
    bool PlayingOwnScene() {
        const int i = scripts_.World().sceneIndex;
        return i < 0 || static_cast<size_t>(i) >= playScenes_.size() || playScenes_[static_cast<size_t>(i)] == AssetRef{slot_.guid};
    }
    // 残す値の、動いているコピーでの今の値。
    std::vector<std::pair<Kept, json::Value>> ReadKept() {
        std::vector<std::pair<Kept, json::Value>> out;
        for (const Kept& k : kept_) {
            if (auto v = ReadSceneField(*playScene_, k.field)) out.push_back({k, std::move(*v)});
        }
        return out;
    }
    // Stop のとき: 残す値 - Play 前のシーンを離れたときの値、それがそのシーンなら動いている
    // コピーでの値 (その EndPlay の前) ...
    std::vector<std::pair<Kept, json::Value>> TakeKept() {
        std::vector<std::pair<Kept, json::Value>> out = std::exchange(keptLeft_, {});
        if (PlayingOwnScene()) {
            for (auto& kv : ReadKept()) out.push_back(std::move(kv));
        }
        kept_.clear();
        return out;
    }
    // ... Play 前のシーンに書く: Undo の項目は 1 つ (未保存)。シーンにないもの (Play 中に作った)
    // は知らせる。
    void KeepValues(const std::vector<std::pair<Kept, json::Value>>& values) {
        if (values.empty()) return;
        std::vector<int> touched;
        for (const auto& [k, v] : values) {
            if (!scene_.Find(k.field.entity)) {
                log::Warn("「{}」は Play の中で作られた物の欄なので、残せません", k.label);
                continue;
            }
            if (std::find(touched.begin(), touched.end(), k.field.entity) == touched.end()) touched.push_back(k.field.entity);
        }
        if (touched.empty()) return;
        size_t done = 0;
        EditTouching(touched, values.size() == 1 ? std::format("Play の値を残す (「{}」)", values[0].first.label) : std::format("Play の値を残す ({} 個)", values.size()), [&] {
            for (const auto& [k, v] : values) {
                if (WriteSceneField(scene_, k.field, v)) ++done;
            }
        });
        log::Info("Play の値を {} 個残しました (Ctrl+Z で戻せます)", done);
    }

    // シーン自身の値 (SceneSettingInfo: 物理の 重力、3D の 空 ...) を 1 つずつブロックで - ゲームの
    // 空間以外のものは除く。Play では動いているコピーのもので、見せるだけで編集しない。
    void InspectScene() {
        inspectingEntities_ = false;
        inspectingScene_ = true;
        editor::Inspectable in;
        std::string name = SceneNameOf(slot_.path);
        in.title = name.empty() ? Tr(L"シーン") : TrF(L"シーン「{}」", Utf8ToWide(name));
        in.blocks = [this] {
            std::vector<editor::InspectBlock> blocks;
            const bool play = Running();
            if (play) blocks.push_back(PlayNote());
            const std::string space = CurrentSpace();
            for (const Component& c : Shown().Settings()) {
                if (!c.Known()) continue;  // そのプラグインがない: ファイルにそのまま残す
                if (const auto it = settingSpaces_.find(c.type); it != settingSpaces_.end() && !space.empty() && it->second != space) continue;
                const std::string type = c.type;
                editor::InspectBlock b{c.value.Type(), [this, type, play] {
                                           Component* s = (play ? Shown() : scene_).FindSetting(type);
                                           return s && s->Known() ? reflect::EditTarget{s->value.Get(), &s->unknowns} : reflect::EditTarget{};
                                       },
                                       Utf8ToWide(TrU8(c.value.Type()->label)), {}, {}};
                b.direct = play;  // (Play: 動いているコピーを、その場で変える)
                if (const SceneSettingLook* look = SettingLookOf(type); look && (look->menu || look->presets)) {
                    SceneSettingMenuHost mh;
                    mh.host = &host_;
                    mh.play = play;
                    mh.edit = [this, type](std::string label, std::function<void(void*)> change) {
                        Edit(std::move(label), [&] {
                            if (Component* s = scene_.FindSetting(type); s && s->Known()) change(s->value.Get());
                        });
                    };
                    mh.apply = [this, type, play, edit = mh.edit](std::string label, std::function<void(void*)> change) {
                        if (!play) {
                            edit(std::move(label), std::move(change));
                        } else if (Component* s = Shown().FindSetting(type); s && s->Known()) {
                            change(s->value.Get());  // (動いているコピー: すぐに、■ で戻る)
                        }
                    };
                    if (look->menu) b.menu = look->menu(mh);
                    if (look->presets) {
                        b.presets = look->presets(mh);
                        b.presetLabel = look->presetLabel ? look->presetLabel() : std::wstring();
                    }
                }
                blocks.push_back(std::move(b));
            }
            if (blocks.empty() || (play && blocks.size() == 1)) {
                blocks.push_back({nullptr, {}, {}, Tr(L"このシーンの設定はまだありません (物理・3D 空間 などのプラグインで増えます)"), {}});
            }
            return blocks;
        };
        host_.Inspect(std::move(in));
        host_.SelectInTree({});
    }

    const SceneSettingLook* SettingLookOf(const std::string& type) const {
        for (const SceneSettingLook* l : settingLooks_) {
            if (l->type().name == type) return l;
        }
        return nullptr;
    }

    // ゲームの設定: 専用のウィンドウ (エディターに固定しない)、形は デバッグ ウィンドウと同じ:
    // ゲーム設定、シーン設定 (ゲームが始めるシーン、タグ、レイヤー)、セーブ設定 (グローバル変数、
    // セーブ)、入力設定、ビルド。編集は Undo の項目 (Play 中は: その場で、■ で戻る)。
    void InspectSettings() { OpenSettingsWindow(-1); }

    void OpenSettingsWindow(int tab) {
        if (tab >= 0) settingsTab_ = tab;
        if (settingsWindow_ && settingsWindow_->IsOpen()) {
            settingsWindow_->BringToFront();
            settingsWindow_->Redraw();
            return;
        }
        editor::ToolWindowSpec spec;
        spec.title = Tr(L"ゲームの設定");
        spec.widthDip = 640;
        spec.heightDip = 720;
        spec.minWidthDip = 420;
        spec.minHeightDip = 300;
        spec.refreshSeconds = 0.25;  // (ビルドの進み具合、Play の開始 / 停止)
        spec.shape = [](float w, float h) { return FolderWindowShape(w, h); };
        spec.hit = [](float x, float y, float w, float h) { return HitFolderWindow(LayoutFolderWindow(w, h), x, y); };
        spec.draw = [this](ui::Context& c, float w, float h) { DrawSettingsWindow(c, w, h); };
        settingsWindow_ = host_.OpenToolWindow(std::move(spec));
        if (!settingsWindow_) log::Warn("ゲームの設定の窓を作れませんでした");
    }

    void DrawSettingsWindow(ui::Context& c, float width, float height) {
        static const wchar_t* kTitles[kFolderTabs] = {TrKey(L"ゲーム設定"), TrKey(L"シーン設定"), TrKey(L"セーブ設定"), TrKey(L"入力設定"), TrKey(L"ビルド")};
        std::vector<std::wstring> titles;
        for (const wchar_t* t : kTitles) titles.push_back(Tr(t));
        const std::wstring status = Running() ? Tr(L"ゲームの設定 ・ Play 中 (変えた値は ■ で Play の前に戻ります)") : Tr(L"ゲームの設定");
        const FolderWindowLayout l = DrawFolderWindow(c, width, height, titles, settingsTab_, status, [this] {
            if (settingsWindow_) settingsWindow_->Close();
        });
        if (settingsTab_ == 4) {
            DrawBuildTab(c, l.content);
        } else {
            DrawSettingsTab(c, l.content, settingsTab_);
        }
    }

    // ゲーム設定の 1 つのタブ: そのフィールドのいくつか (インスペクターが描く) と、それに付くもの。
    void DrawSettingsTab(ui::Context& c, const Rect& r, int tab) {
        auto& p = c.P();
        editor::inspector::Block b{&reflect::TypeOf<GameSettings>(), &settings_, &unknowns_, {}, {}, {}};
        switch (tab) {
            case 0: b.fields = {"title", "icon", "targetFps", "fixedTimestep", "clearColor", "display", "volume", "startCamera", "scriptOrder", "scriptReload"}; break;
            case 1: b.fields = {"tags", "layers"}; break;
            case 2: b.fields = {"globals"}; break;
            default: b.fields = {"input", "keyboardPlayer"}; break;
        }
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextDim;
        ts.wrap = true;
        float y = r.y + 6;
        if (tab == 1) {
            // ゲームが始めるシーン (書き出したゲーム。エディターの Play は開いているシーンから始まる)。
            ui::TextStyle label = ts;
            label.wrap = false;
            label.color = theme::Text;
            label.v = ui::VAlign::Center;
            p.Text({r.x + 14, y, 120, 26}, Tr(L"初期シーン"), label);
            const AssetRef first = FirstScene(sceneList_);
            const Rect combo{r.x + 140, y, std::max(80.f, r.w - 160), 26};
            if (editor::w::ComboBox(c, "first_scene", combo, first.Empty() ? Tr(L"(なし)") : Utf8ToWide(SceneTitle(first)), !Running()).clicked) {
                firstScenePopup_.OpenAt(combo.x, combo.Bottom());
            }
            if (firstScenePopup_.open && Running()) firstScenePopup_.open = false;  // (開いている間に Play が始まった)
            if (firstScenePopup_.open) {
                std::vector<editor::w::MenuItem> items;
                for (const AssetRef& ref : sceneList_.scenes) {
                    editor::w::MenuItem it;
                    it.label = Utf8ToWide(SceneTitle(ref));
                    it.checked = ref == first;
                    items.push_back(std::move(it));
                }
                const int chosen = editor::w::MenuPopup(c, firstScenePopup_, items, combo.w);
                if (chosen >= 0 && static_cast<size_t>(chosen) < sceneList_.scenes.size()) {
                    const AssetRef ref = sceneList_.scenes[static_cast<size_t>(chosen)];
                    EditSceneList(TrU8("初期シーンを変える"), [&](SceneList& l) { l.first = ref; });
                }
            }
            y += 30;
            const std::wstring note = Tr(L"書き出したゲームはこのシーンから始まります (エディターの Play は開いているシーンから)。シーンごとの値 (重力・空) は、ヒエラルキーの一番上のシーンの行を押すとインスペクターに出ます");
            const float h = p.TextHeight(note, 12, r.w - 28);
            p.Text({r.x + 14, y, r.w - 28, h}, note, ts);
            y += h + 10;
        }
        if (tab == 0) y += DrawSpaceChoice(c, r, y);
        if (tab == 2) {
            const std::wstring note = Tr(L"グローバル変数はゲーム全体の値で、rm::Save で保存できます。Play 中の rm::Save は、このプロジェクトの Library/Saves に書きます (書き出したゲームは %LOCALAPPDATA% の「exe の名前/Saves」)");
            const float h = p.TextHeight(note, 12, r.w - 28);
            p.Text({r.x + 14, y, r.w - 28, h}, note, ts);
            y += h + 6;
            if (editor::w::Button(c, "delete_saves", {r.x + 14, y, 230, 26}, Tr(L"セーブを消す (Library/Saves)"))) {
                editor::PostToUi([this] { DeleteSaves(); });
            }
            y += 34;
        }
        editor::inspector::Refs refs;
        host_.FillInspectorRefs(refs);
        if (auto edit = editor::inspector::Draw(c, settingsInspectors_[static_cast<size_t>(tab)], {r.x, y, r.w, std::max(0.f, r.Bottom() - y)},
                                                std::span(&b, 1), {}, {}, &refs)) {
            ApplySettingsEdit(*edit);
        }
    }

    // ゲーム設定のフィールドをそのウィンドウで変えた: Undo の項目 - Play 中は、その場で (■ で戻る)。
    void ApplySettingsEdit(editor::inspector::Edit& edit) {
        const reflect::TypeDesc& type = reflect::TypeOf<GameSettings>();
        auto cmd = std::make_unique<reflect::SetFieldCommand>(type, std::move(edit.path), std::move(edit.before), std::move(edit.after),
                                                              [this] { return reflect::EditTarget{&settings_, &unknowns_}; }, std::move(edit.label), edit.merge);
        if (Running()) {
            cmd->Do();
        } else {
            host_.History().Push(std::move(cmd), false);
        }
        host_.Redraw();
    }

    // エンティティが今どこにあるか (Undo で一覧が作り直されたかもしれない): そのつど id で探す。
    // `play`: 動いているコピーの中 (Play 中に見せる。編集はしない)。
    std::function<reflect::EditTarget()> ResolveEntity(int id, bool play = false) {
        return [this, id, play] {
            SceneEntity* e = (play ? Shown() : scene_).Find(id);
            return e ? reflect::EditTarget{&e->data, &e->unknowns} : reflect::EditTarget{};
        };
    }

    std::function<reflect::EditTarget()> ResolveComponent(int id, std::string type, size_t nth = 0, bool play = false) {
        return [this, id, type, nth, play] {
            SceneEntity* e = (play ? Shown() : scene_).Find(id);
            Component* c = e ? e->FindComponent(type, nth) : nullptr;
            return c && c->Known() ? reflect::EditTarget{c->value.Get(), &c->unknowns} : reflect::EditTarget{};
        };
    }

    const ComponentLook* LookOf(const std::string& type) const {
        for (const ComponentLook* l : componentLooks_) {
            if (l->type().name == type) return l;
        }
        return nullptr;
    }

    // エンティティと、その部品 `type` を持つ親の部品: 一番上の親のものが先。
    // エンティティやその部品がなくなっていれば空。
    static std::vector<const void*> Chain(const Scene& scene, int id, const std::string& type) {
        std::vector<const void*> chain;
        const SceneEntity* self = scene.Find(id);
        const Component* own = self ? self->FindComponent(type) : nullptr;
        if (!own || !own->Known()) return chain;
        size_t steps = 0;  // 壊れたファイルが親の輪を作るかもしれない
        for (const SceneEntity* e = self; e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
            const Component* c = e->FindComponent(type);
            if (c && c->Known()) chain.push_back(c->value.Get());
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    // 部品のプラグインが、親をたどって求める値: インスペクターが聞くたびに求め直し、自分専用の
    // 値に入れる (その編集は EditDerived を通る)。
    std::function<reflect::EditTarget()> Derived(int id, std::string type, const ComponentLook* info, bool play) {
        auto value = std::make_shared<reflect::Object>(info->derivedType());
        return [this, id, type = std::move(type), info, value, play] {
            const std::vector<const void*> chain = Chain(play ? Shown() : scene_, id, type);
            if (chain.empty()) return reflect::EditTarget{};  // なくなった (Undo)
            info->derive(chain, value->Get());
            return reflect::EditTarget{value->Get(), nullptr};
        };
    }

    // インスペクターに入力した派生値: 部品のプラグインが、それを与える部品を求める。変わる
    // フィールドは部品の編集になる (Undo の項目は 1 つ。1 つのフィールドを打つ間は 1 つの項目に
    // まとまる)。
    void EditDerived(int id, const std::string& type, const ComponentLook* info, editor::InspectBlock::Change ch, bool play = false) {
        const std::vector<const void*> chain = Chain(play ? Shown() : scene_, id, type);
        if (chain.empty()) return;
        const reflect::TypeDesc& dt = info->derivedType();
        const reflect::TypeDesc& ct = info->type();
        reflect::Object derived(dt);
        info->derive(chain, derived.Get());
        const reflect::PathTarget pt = reflect::Resolve(dt, derived.Get(), nullptr, ch.path);
        if (!pt.field) return;
        reflect::LoadField(*pt.field, pt.owner, ch.after, nullptr, nullptr);
        reflect::Object next(ct);
        ct.copy(next.Get(), chain.back());
        info->underive(chain, derived.Get(), next.Get());
        const uint64_t diff = reflect::Diff(ct, chain.back(), next.Get());
        if (diff == 0) return;
        const auto resolve = ResolveComponent(id, type, 0, play);
        std::vector<std::unique_ptr<ICommand>> cmds;
        for (size_t i = 0; i < ct.fields.size() && i < 64; ++i) {
            if (!(diff >> i & 1)) continue;
            const reflect::FieldDesc& f = ct.fields[i];
            cmds.push_back(std::make_unique<reflect::SetFieldCommand>(ct, reflect::FieldPath{i}, reflect::SaveField(f, chain.back(), nullptr),
                                                                      reflect::SaveField(f, next.Get(), nullptr), resolve, ch.label, ch.merge));
        }
        if (play) {  // (動いているコピー: その場で、どこにも残らない)
            for (auto& cmd : cmds) cmd->Do();
            return;
        }
        if (cmds.size() == 1) {
            host_.History().Push(std::move(cmds.front()), false);
            return;
        }
        auto group = std::make_unique<CommandGroup>(ch.label);
        for (auto& cmd : cmds) group->Add(std::move(cmd));
        host_.History().Push(std::move(group), false);
    }

    // フィールドをまとめて持つプラグインの部品 (ComponentLook::adjust) のインスペクターに
    // 入力したフィールド: 入力したとおりのフィールドと、それでプラグインが変えたもの - Undo の項目は 1 つ
    // (プラグインがほかに何も変えない間は、1 つのフィールドへの入力が 1 つの項目にまとまる)。
    void EditAdjusted(int id, const std::string& type, size_t nth, const ComponentLook* info, editor::InspectBlock::Change ch, bool play = false) {
        Scene& scene = play ? Shown() : scene_;
        SceneEntity* e = scene.Find(id);
        const Component* c = e ? e->FindComponent(type, nth) : nullptr;
        if (!c || !c->Known()) return;
        const reflect::TypeDesc& ct = *c->value.Type();
        const auto resolve = ResolveComponent(id, type, nth, play);
        auto typedCmd = std::make_unique<reflect::SetFieldCommand>(ct, ch.path, ch.before, ch.after, resolve, ch.label, ch.merge);
        reflect::Object typed(ct);
        ct.copy(typed.Get(), c->value.Get());
        const reflect::PathTarget pt = reflect::Resolve(ct, typed.Get(), nullptr, ch.path);
        if (!pt.field) return;
        reflect::LoadField(*pt.field, pt.owner, ch.after, nullptr, nullptr);
        reflect::Object next(ct);
        ct.copy(next.Get(), typed.Get());
        info->adjust(scene, settings_, id, ch.path, c->value.Get(), next.Get());
        const uint64_t diff = reflect::Diff(ct, typed.Get(), next.Get());
        if (play) {  // (動いているコピー: フィールドと、それでプラグインが変えたものを、その場で)
            typedCmd->Do();
            const Component* now = scene.Find(id) ? scene.Find(id)->FindComponent(type, nth) : nullptr;
            if (now && now->Known()) ct.copy(const_cast<void*>(now->value.Get()), next.Get());
            return;
        }
        if (diff == 0) {
            host_.History().Push(std::move(typedCmd), false);
            return;
        }
        auto group = std::make_unique<CommandGroup>(ch.label);
        group->Add(std::move(typedCmd));
        for (size_t i = 0; i < ct.fields.size() && i < 64; ++i) {
            if (!(diff >> i & 1)) continue;
            const reflect::FieldDesc& f = ct.fields[i];
            group->Add(std::make_unique<reflect::SetFieldCommand>(ct, reflect::FieldPath{i}, reflect::SaveField(f, typed.Get(), nullptr),
                                                                  reflect::SaveField(f, next.Get(), nullptr), resolve, ch.label, 0));
        }
        host_.History().Push(std::move(group), false);
    }

    // 派生値のある子の部品の見せ方: 0 = 自分の値 (始まりのまま)、1 = 派生値。
    // 一度切り替えたらそのまま (UserSettings に持つ)。
    int ModeOf(const SceneEntity& e) const {
        const auto it = modes_.find(e.data.id);
        return it != modes_.end() ? it->second : 0;
    }

    // Prefab として置かれたものの一部のとき、エンティティのヘッダーの下に出す行。
    std::wstring PrefabNote(int id) const {
        const int root = scene_.PrefabRootOf(id);
        if (root == 0) return {};
        const SceneEntity* r = scene_.Find(root);
        const std::wstring name = Utf8ToWide(PrefabTitle(r->prefab.asset));
        if (root != id) return TrF(L"Prefab「{}」の一部です", name);
        const std::wstring where = Utf8ToWide(PrefabWhere(r->prefab.asset));
        const auto it = prefabs_.find(r->prefab.asset.guid);
        if (it == prefabs_.end()) return TrF(L"Prefab「{}」を置いた物です (ファイル {} が読めません)", name, where);
        const json::Value* overrides = CachedOverrides(root);
        const size_t n = overrides ? CountOverrides(*overrides) : 0;
        // 置いた別の Prefab に入れ子になった Prefab: どれか言う。
        if (const int around = r->prefab.outer > 0 ? scene_.PrefabRootOf(r->data.parent) : 0) {
            return TrF(L"Prefab「{}」を置いた物です ({})。Prefab「{}」の中に入っています。Prefab と違う所: {} 個 (名前と置き場所を含む)", name,
                       where, Utf8ToWide(PrefabTitle(scene_.Find(around)->prefab.asset)), n);
        }
        return TrF(L"Prefab「{}」を置いた物です ({})。Prefab と違う所: {} 個 (名前と置き場所を含む)", name, where, n);
    }

    // 部品の型を 部品を足す で入れるフォルダ: そのプラグインのもの (ComponentInfo::folder、各部分を
    // 翻訳する)、スクリプトは "スクリプト"、それ以外は "その他"。
    std::wstring FolderOf(const reflect::TypeDesc& t) const {
        if (t.kind == reflect::TypeKind::Script) return Tr(L"スクリプト");
        const auto it = componentFolders_.find(t.name);
        if (it == componentFolders_.end()) return Tr(L"その他");
        std::wstring out;
        std::string_view rest = it->second;
        while (true) {
            const size_t slash = rest.find('/');
            out += Utf8ToWide(TrU8(std::string(rest.substr(0, slash))));
            if (slash == std::string_view::npos) break;
            out += L'/';
            rest.remove_prefix(slash + 1);
        }
        return out;
    }

    // 型のフォルダが 部品を足す でどの順に来るか: 3D, 2D, UI, 物理, 音, エフェクト、それから
    // ほかのプラグインのフォルダ、スクリプト、その他 (一覧はこれで並べる。フォルダは最初の行の順に来る)。
    int FolderRank(const reflect::TypeDesc& t) const {
        if (t.kind == reflect::TypeKind::Script) return 100;
        const auto it = componentFolders_.find(t.name);
        if (it == componentFolders_.end()) return 101;
        static constexpr std::string_view kOrder[] = {"3D", "2D", "UI", "物理", "音", "エフェクト"};
        const std::string_view top = std::string_view(it->second).substr(0, std::string_view(it->second).find('/'));
        for (size_t i = 0; i < std::size(kOrder); ++i) {
            if (kOrder[i] == top) return static_cast<int>(i);
        }
        return 50;
    }

    // 部品の型を 部品を足す の順に (FolderRank。フォルダの中は、プラグインが出した順)。
    std::vector<const reflect::TypeDesc*> TypesForAdding() const {
        std::vector<const reflect::TypeDesc*> types = scene_.ComponentTypes();
        std::stable_sort(types.begin(), types.end(), [this](const reflect::TypeDesc* a, const reflect::TypeDesc* b) { return FolderRank(*a) < FolderRank(*b); });
        return types;
    }

    // 1 つのエンティティのインスペクター: エンティティ自身、それから部品 (それぞれ ⋮ → 外す)、
    // その下に "部品を足す"。描くたびに聞き直す (Undo が部品を足したり取ったりするかもしれない。
    // Play は動いているコピーを見せる)。
    editor::Inspectable EntityInspectable(int id) {
        editor::Inspectable in;
        in.title = Tr(L"エンティティ");
        in.blocks = [this, id] {
            std::vector<editor::InspectBlock> blocks;
            const bool play = Running();
            const SceneEntity* e = Shown().Find(id);
            if (!e) return blocks;
            if (play) blocks.push_back(PlayNote());
            editor::InspectBlock entity{&reflect::TypeOf<Entity>(), ResolveEntity(id, play), {}, PrefabNote(id), {}};
            entity.direct = play;
            if (play) entity.fieldMenu = KeepMenu(id, "", 0);
            blocks.push_back(std::move(entity));
            for (const Component& c : e->components) {
                const std::string type = c.type;
                const size_t nth = e->NthOf(c);
                const size_t count = e->CountComponents(type);
                const std::string label = c.Known() ? c.value.Type()->label : c.type;
                // 同じ型が複数ある: 番号を振る (ブロックのタイトルは、そのフィールドを区別するものでもある)。
                const std::wstring number = count > 1 ? std::format(L" ({})", nth + 1) : std::wstring();
                editor::w::Action remove;
                remove.label = Tr(L"外す");
                remove.run = [this, id, type, nth, label] {
                    if (Running()) {
                        PlayRemoveComponent(id, type, nth);
                        return;
                    }
                    EditTouching({id}, std::format("「{}」を外す", label), [&] { scene_.RemoveComponent(id, type, nth); });
                };
                // エンティティの部品の中での位置 (見せる順 - 動く順でもある)。
                const size_t at = static_cast<size_t>(&c - e->components.data());
                editor::w::Action up;
                up.label = Tr(L"上へ移動");
                up.enabled = at > 0;
                up.run = [this, id, type, nth, label] {
                    if (Running()) {
                        if (playScene_) playScene_->MoveComponent(id, type, -1, nth);
                        return;
                    }
                    EditTouching({id}, std::format("「{}」を上へ移動", label), [&] { scene_.MoveComponent(id, type, -1, nth); });
                };
                editor::w::Action down;
                down.label = Tr(L"下へ移動");
                down.enabled = at + 1 < e->components.size();
                down.run = [this, id, type, nth, label] {
                    if (Running()) {
                        if (playScene_) playScene_->MoveComponent(id, type, 1, nth);
                        return;
                    }
                    EditTouching({id}, std::format("「{}」を下へ移動", label), [&] { scene_.MoveComponent(id, type, 1, nth); });
                };
                editor::w::Action sep;
                sep.separator = true;
                std::vector<editor::w::Action> menu{up, down, sep, remove};
                if (c.Known()) {
                    // スクリプト: ほかのスクリプトの中で動く順 (ゲーム設定の スクリプトの実行順)。
                    std::wstring note;
                    if (c.value.Type()->kind == reflect::TypeKind::Script) note = TrF(L"スクリプト ・ 実行順 {}", ScriptOrderOf(c.type, settings_.scriptOrder));
                    editor::InspectBlock b{c.value.Type(), ResolveComponent(id, c.type, nth, play), Utf8ToWide(TrU8(label)) + number, note, menu};
                    b.direct = play;
                    if (play) b.fieldMenu = KeepMenu(id, type, nth);
                    // 子: ローカル / ワールド (プラグインが別の名前で呼ぶ場合はそれ)。どちらでも 1 つのブロックを見せる。
                    // 最上位ではどちらも同じ数値なので、切り替えはない。
                    if (const ComponentLook* info = LookOf(c.type); info && info->HasDerived() && nth == 0 && e->data.parent != 0) {
                        b.modes = {Tr(info->ownLabel), Tr(info->derivedLabel)};
                        b.mode = ModeOf(*e);
                        b.setMode = [this, id](int m) { modes_[id] = m; };
                        if (b.mode == 1) {
                            b.type = &info->derivedType();
                            b.resolve = Derived(id, c.type, info, play);
                            b.onEdit = [this, id, type, info, play](editor::InspectBlock::Change ch) { EditDerived(id, type, info, std::move(ch), play); };
                        }
                    }
                    if (const ComponentLook* info = LookOf(c.type); info && info->choices && !b.onEdit) {
                        b.choices = [this, id, info](const reflect::FieldPath& path) { return info->choices(host_, Shown(), id, path); };
                    }
                    if (const ComponentLook* info = LookOf(c.type); info && info->adjust && !b.onEdit) {
                        b.onEdit = [this, id, type, nth, info, play](editor::InspectBlock::Change ch) {
                            EditAdjusted(id, type, nth, info, std::move(ch), play);
                        };
                    }
                    blocks.push_back(std::move(b));
                } else {
                    blocks.push_back({nullptr, {}, Utf8ToWide(label) + Tr(L" (使えない部品)") + number,
                                      Tr(L"この部品のプラグインが入っていないか、形が読めません。データはそのまま残り、保存しても消えません。"), menu});
                }
            }
            return blocks;
        };
        in.addLabel = Tr(L"＋ 部品を足す");
        in.addMenu = [this, id] {
            std::vector<editor::w::Action> list;
            // (Play: 動いているコピーに足す - そのスクリプトは次のフレームで始まる。■ で消える)
            const SceneEntity* e = Shown().Find(id);
            for (const reflect::TypeDesc* t : TypesForAdding()) {
                if (!e || (e->FindComponent(t->name) && !Shown().AllowsMultiple(t->name))) continue;
                editor::w::Action add;
                add.label = Utf8ToWide(TrU8(t->label));
                add.folder = FolderOf(*t);
                if (e->FindComponent(t->name)) add.label += Tr(L" (もう 1 つ)");
                add.run = [this, id, t] {
                    if (Running()) {
                        if (playScene_) playScene_->AddComponent(id, *t);
                        return;
                    }
                    EditTouching({id}, std::format("「{}」を足す", t->label), [&] { scene_.AddComponent(id, *t); });
                };
                list.push_back(std::move(add));
            }
            if (list.empty()) {
                editor::w::Action none;
                none.label = scene_.ComponentTypes().empty() ? Tr(L"足せる部品がありません (3D 空間 などのプラグインで増えます)")
                                                             : Tr(L"足せる部品は全部付いています");
                none.enabled = false;
                list.push_back(std::move(none));
            }
            return list;
        };
        return in;
    }

    // 複数のエンティティを同時に: エンティティ自身の値と、全員が持つ部品 (型ごとに最初の 1 つ)
    // を、最後に選んだものから見せる。変更は全員に行く (違うところは "—")。
    // 外す / 部品を足す は全員に効く。
    editor::Inspectable ManyInspectable(std::vector<int> ids) {
        editor::Inspectable in;
        in.title = TrF(L"{} 個のエンティティ", ids.size());
        in.blocks = [this, ids] {
            std::vector<editor::InspectBlock> blocks;
            const bool play = Running();
            const Scene& shown = Shown();
            std::vector<int> here;  // まだある (Undo)
            for (const int id : ids) {
                if (shown.Find(id)) here.push_back(id);
            }
            if (here.empty()) return blocks;
            if (play) blocks.push_back(PlayNote());
            const int main = here.back();
            const std::vector<int> rest(here.begin(), here.end() - 1);
            editor::InspectBlock entity{&reflect::TypeOf<Entity>(), ResolveEntity(main, play), {}, {}, {}};
            for (const int id : rest) entity.also.push_back(ResolveEntity(id, play));
            entity.direct = play;
            blocks.push_back(std::move(entity));
            const SceneEntity* me = shown.Find(main);
            for (const Component& c : me->components) {
                if (!c.Known() || me->NthOf(c) != 0) continue;
                const bool everyone = std::all_of(rest.begin(), rest.end(), [&](int id) {
                    const Component* o = shown.Find(id)->FindComponent(c.type);
                    return o && o->Known();
                });
                if (!everyone) continue;
                const std::string type = c.type;
                const std::string label = c.value.Type()->label;
                editor::w::Action remove;
                remove.label = Tr(L"外す");
                remove.run = [this, here, type, label] {
                    if (Running()) {
                        for (const int id : here) PlayRemoveComponent(id, type, 0);
                        return;
                    }
                    EditTouching(here, std::format("{} 個のエンティティから「{}」を外す", here.size(), label), [&] {
                        for (const int id : here) scene_.RemoveComponent(id, type);
                    });
                };
                editor::InspectBlock b{c.value.Type(), ResolveComponent(main, type, 0, play), Utf8ToWide(TrU8(label)), {}, {remove}};
                for (const int id : rest) b.also.push_back(ResolveComponent(id, type, 0, play));
                b.direct = play;
                blocks.push_back(std::move(b));
            }
            return blocks;
        };
        in.addLabel = Tr(L"＋ 部品を足す");
        in.addMenu = [this, ids] {
            std::vector<editor::w::Action> list;
            for (const reflect::TypeDesc* t : TypesForAdding()) {
                // 少なくとも 1 つが持っていない (またはもっとあるかもしれない) 間は出す。足せるところに足す。
                const bool someCan = Shown().AllowsMultiple(t->name) || std::any_of(ids.begin(), ids.end(), [&](int id) {
                                         const SceneEntity* e = Shown().Find(id);
                                         return e && !e->FindComponent(t->name);
                                     });
                if (!someCan) continue;
                editor::w::Action add;
                add.label = Utf8ToWide(TrU8(t->label));
                add.folder = FolderOf(*t);
                add.run = [this, ids, t] {
                    if (Running()) {
                        for (const int id : ids) {
                            if (playScene_ && playScene_->Find(id)) playScene_->AddComponent(id, *t);
                        }
                        return;
                    }
                    EditTouching(ids, std::format("{} 個のエンティティに「{}」を足す", ids.size(), t->label), [&] {
                        for (const int id : ids) scene_.AddComponent(id, *t);
                    });
                };
                list.push_back(std::move(add));
            }
            if (list.empty()) {
                editor::w::Action none;
                none.label = scene_.ComponentTypes().empty() ? Tr(L"足せる部品がありません (3D 空間 などのプラグインで増えます)")
                                                             : Tr(L"足せる部品は全部付いています");
                none.enabled = false;
                list.push_back(std::move(none));
            }
            return list;
        };
        return in;
    }

    // --- シーンの一覧 (ゲームのシーン) ---------------------------------------------------------
    // 画面と Game::LoadScene に使うシーンの名前: そのファイルのもの (まだ保存していない新しい
    // ものは、これからできるファイルのもの)。
    std::string SceneTitle(const AssetRef& ref) const {
        if (ref.Empty() || ref.guid == slot_.guid) return SceneNameOf(slot_.path);
        const fs::path file = host_.AssetFile(ref);
        return file.empty() ? std::string() : SceneNameOf(PathToUtf8(file.filename()));
    }

    void EditSceneList(std::string label, const std::function<void(SceneList&)>& change) {
        SceneList before = sceneList_;
        change(sceneList_);
        host_.History().Push(std::make_unique<SceneListCommand>(sceneList_, std::move(before), sceneList_, std::move(label)));
        host_.Redraw();
    }

    // 見せるシーンが変わる前に: 保存していないものを、ユーザーが言うとおりに保存する (しない)。
    // それから `go`。
    void AfterAskingToSave(std::wstring what, std::function<void()> go) {
        if (Running()) {
            log::Info("Play 中はシーンを開けません (■ で止めてから)");
            return;
        }
        if (!host_.Unsaved()) {
            go();
            return;
        }
        host_.Ask(Tr(L"保存されていない変更があります"), TrF(L"{}前に、今の変更を保存しますか？\n保存しないと、このシーンの変更は失われます。", what),
                  {Tr(L"保存する"), Tr(L"保存しない"), Tr(L"キャンセル")}, [this, go = std::move(go)](int answer) {
                      if (answer == 0 && host_.SaveProject()) go();
                      if (answer == 1) go();
                  });
    }

    // 見せるシーンが別のものになる: `ref` をそのファイルから読む (空: 新しいシーン、`newPath`)。
    // 履歴は消える (その項目は、見せていたシーンの編集だから)。
    void ShowScene(const AssetRef& ref, const std::string& newPath = {}) {
        scene_.Load(json::Value());
        slot_ = {"", newPath.empty() ? NewSceneFile() : newPath, 0};
        sceneUnread_ = false;
        replaces_.clear();
        pending_.reset();
        reflect::Warnings warnings;
        if (!ref.Empty()) {
            sceneList_.open = ref;  // (次の保存で書く: プロジェクトが開くときのシーン)
            ReadOpenScene(warnings);
        }
        for (const auto& w : warnings) log::Warn("シーン: {}", w);
        RefreshPrefabsOnOpen();
        ++dataVersion_;
        host_.ClearHistory();
        host_.SelectInTree({});
        sceneChosen_ = ref;
        renamingScene_ = false;
        log::Info("シーン「{}」を開きました (エンティティ {} 個)", SceneTitle(ref), scene_.Entities().size());
        host_.Inspect({});  // (ゲーム設定は専用のウィンドウ: 頼まれたときに開く)
        host_.Redraw();
    }

    void OpenSceneAsked(const AssetRef& ref) {
        if (ref.Empty() || ref.guid == slot_.guid) return;
        AfterAskingToSave(Tr(L"別のシーンを開く"), [this, ref] { ShowScene(ref); });
    }

    // 新しい空のシーン (Assets/Scenes/Scene.rmscene ...) をすぐ保存する: 一覧にもディスクにもある。
    void NewSceneAsked() {
        AfterAskingToSave(Tr(L"新しいシーンを作る"), [this] {
            ShowScene({}, NewSceneFile("Scene"));
            if (host_.SaveProject()) sceneChosen_ = AssetRef{slot_.guid};
        });
    }

    // シーンの右クリック (シーン パネルの行・ヒエラルキーの一番上の行) の「複製」「削除」。
    // ファイルの操作なので Undo には積まない (アセットのパネルと同じ: 削除の戻り道はごみ箱)。
    // ゲームの一覧の変更だけが、ほかの一覧の編集と同じく履歴に積まれ、未保存になる。
    std::vector<tree::Action> SceneFileActions(const AssetRef& ref) {
        std::vector<tree::Action> list;
        const bool playing = Running();
        const bool isOpen = ref.Empty() || ref.guid == slot_.guid;
        const bool hasFile = !ref.Empty() && !host_.AssetFile(ref).empty();
        tree::Action dup;
        dup.label = Tr(L"複製");
        dup.enabled = !playing && (hasFile || isOpen);
        dup.run = [this, ref] { DuplicateScene(ref); };
        list.push_back(std::move(dup));
        tree::Action del;
        del.label = Tr(L"削除");
        if (ref.Empty()) {
            del.label = Tr(L"削除 (まだ保存していないシーン)");
            del.enabled = false;
        } else if (isOpen && SceneToShowAfterDeleting(ref).Empty()) {
            del.label = Tr(L"削除 (ほかにシーンが無いので消せません)");
            del.enabled = false;
        } else {
            del.enabled = !playing;
        }
        del.run = [this, ref] { DeleteSceneAsked(ref); };
        list.push_back(std::move(del));
        return list;
    }

    // 複製: 元と同じフォルダーに "<名前> 2" ... (CopyScenePath)、新しい id (.meta はアセットの表が
    // 今付ける)。開いているシーンは画面の今の中身 (保存していない変更も。保存と同じテキスト)、
    // ほかのものはファイルのまま (新しい版の知らないキーも)。エンティティの id はシーンごとなので
    // そのまま。ゲームの一覧にあれば、そのすぐ後ろに入れる。開いているシーンは変わらない。
    void DuplicateScene(const AssetRef& ref) {
        if (Running()) {
            log::Info("Play 中はシーンを複製できません (■ で止めてから)");
            return;
        }
        const bool isOpen = ref.Empty() || ref.guid == slot_.guid;
        const fs::path file = ref.Empty() ? fs::path() : host_.AssetFile(ref);
        std::string from = file.empty() ? std::string() : RelativePath(file);
        std::optional<std::string> text;
        if (isOpen && !(sceneUnread_ && scene_.Empty())) {
            Scene saved = scene_;
            UpdateOverrides(saved);
            text = SceneFileText(saved.Save());
            if (from.empty()) from = slot_.path;  // (まだ保存していない新しいシーン: これからできるファイルの隣)
        } else if (file.empty()) {
            log::Warn("シーン ({}) のファイルが Assets に見つからないので、複製できません", ref.guid);
            return;
        } else {
            std::string err;
            text = platform::ReadFileBytes(file, &err);
            if (!text) {
                log::Error("シーン「{}」を読めないので、複製できません: {}", SceneNameOf(from), err);
                return;
            }
        }
        const std::string to = CopyScenePath(from, [this](const std::string& p) {
            std::error_code ec;
            return fs::exists(FullPath(p), ec);
        });
        std::string err;
        if (!platform::WriteFileAtomic(FullPath(to), *text, &err)) {
            log::Error("シーンの複製 ({}) を書けませんでした: {}", to, err);
            return;
        }
        const AssetRef copy = host_.AssetOf(FullPath(to));  // (新しい .meta: 見るのを待たずに今)
        sceneFilesTime_ = 0;  // (一覧に無いシーンとして、すぐ出す)
        if (copy.Empty()) {
            log::Warn("シーンの複製 ({}) に id を付けられませんでした。ゲームの一覧には足していません", to);
        } else {
            const SceneList next = SceneListWithCopy(sceneList_, ref, copy, AssetRef{slot_.guid});
            if (next.scenes != sceneList_.scenes) EditSceneList(TrU8("シーンを複製する"), [&](SceneList& l) { l = next; });
        }
        log::Info("シーン「{}」を複製しました: {}", SceneNameOf(from), to);
        host_.Redraw();
    }

    // 開いているシーン `gone` を消したあとに開くもの (空: ほかにシーンが無い)。
    AssetRef SceneToShowAfterDeleting(const AssetRef& gone) {
        return SceneAfterDelete(sceneList_, gone, ScenesNotListed(), [this](const AssetRef& r) { return !host_.AssetFile(r).empty(); });
    }

    // 削除: 名前を出して確かめてから、ファイルを (.meta と一緒に) ごみ箱へ。ファイルがもう無い一覧の
    // 行は、確かめずに一覧から外すだけ (Undo で戻る)。
    void DeleteSceneAsked(const AssetRef& ref) {
        if (Running()) {
            log::Info("Play 中はシーンを削除できません (■ で止めてから)");
            return;
        }
        if (ref.Empty()) return;
        const bool isOpen = ref.guid == slot_.guid;
        const bool hasFile = !host_.AssetFile(ref).empty();
        if (!hasFile && !isOpen) {
            DeleteScene(ref);
            return;
        }
        const AssetRef next = isOpen ? SceneToShowAfterDeleting(ref) : AssetRef{};
        if (isOpen && next.Empty()) {
            log::Info("ほかにシーンが無いので、このシーンは削除できません (先に新しいシーンを作ってください)");
            return;
        }
        std::wstring text = hasFile ? TrF(L"シーン「{}」をごみ箱へ移しますか？", Utf8ToWide(SceneTitle(ref)))
                                    : TrF(L"シーン「{}」を削除しますか？ (ファイルはもうありません)", Utf8ToWide(SceneNameOf(slot_.path)));
        if (isOpen) text += TrF(L"\n開いているシーンなので、保存していない変更は失われ、「{}」を開きます。", Utf8ToWide(SceneTitle(next)));
        if (ref == FirstScene(sceneList_)) text += Tr(L"\nゲームが始まるシーンです。ほかのシーン (一覧の最初のもの) から始まるようになります。");
        if (hasFile) text += Tr(L"\nファイルはごみ箱から戻せます (Ctrl+Z では戻りません)。");
        host_.Ask(Tr(L"シーンを削除"), text, {Tr(L"削除する"), Tr(L"キャンセル")}, [this, ref](int answer) {
            if (answer == 0) DeleteScene(ref);
        });
    }

    void DeleteScene(const AssetRef& ref) {
        if (Running() || ref.Empty()) return;  // (確かめている間に Play が始まった)
        const bool isOpen = ref.guid == slot_.guid;
        const AssetRef next = isOpen ? SceneToShowAfterDeleting(ref) : AssetRef{};
        if (isOpen && next.Empty()) return;
        const std::string name = SceneTitle(ref);
        const bool hadFile = !host_.AssetFile(ref).empty();
        if (hadFile) {
            const std::wstring problem = host_.DeleteAsset(ref);  // (理由はコンソールにも出る)
            if (!problem.empty() || !host_.AssetFile(ref).empty()) {
                log::Warn("シーン「{}」を削除できませんでした{}", name, problem.empty() ? std::string() : ": " + WideToUtf8(problem));
                return;
            }
        }
        // 開いていたもの: 別のシーンを開く (その編集は失われる - 確かめた。履歴はシーンを開くと消える
        // ので、一覧の変更はその後に積む)。
        if (isOpen) ShowScene(next);
        const AssetRef firstBefore = FirstScene(sceneList_);
        const SceneList after = SceneListWithout(sceneList_, ref);
        if (after.scenes != sceneList_.scenes || after.notBuilt != sceneList_.notBuilt || after.first != sceneList_.first || after.open != sceneList_.open) {
            EditSceneList(TrU8("シーンを削除する"), [&](SceneList& l) { l = after; });
        }
        sceneFilesTime_ = 0;
        if (hadFile) {
            log::Info("シーン「{}」をごみ箱へ移しました", name);
        } else {
            log::Info("ファイルの無いシーン ({}) をゲームの一覧から外しました", ref.guid);
        }
        if (const AssetRef first = FirstScene(sceneList_); first != firstBefore && !first.Empty()) {
            log::Info("ゲームが始まるシーンは「{}」になりました", SceneTitle(first));
        }
        host_.Redraw();
    }

    // シーン: ゲームのシーンを、ヒエラルキー がエンティティを見せるとおりに (同じツリー表示):
    // クリックで 1 つ開き (開いているものが選ばれた行)、ドラッグで順を変え (ゲームの順)、
    // F2 / ダブルクリック / メニューで名前を変え、右クリックで: 開く、新しいシーン
    // (Assets にあって一覧にないシーンには 一覧に足す)。ゲームが始めるのはどれか:
    // ゲームの設定 ▸ シーン設定。
    class SceneListSource final : public editor::tree::Source {
    public:
        explicit SceneListSource(GameSoftware& g) : g_(g) {}

        static editor::tree::ItemId IdOf(const AssetRef& ref) {
            return ref.Empty() ? 1 : (std::hash<std::string>()(ref.guid) | 2);
        }
        std::optional<AssetRef> RefOf(editor::tree::ItemId id) const {
            for (const AssetRef& r : refs_) {
                if (IdOf(r) == id) return r;
            }
            return std::nullopt;
        }

        void Items(std::vector<editor::tree::Item>& out) override {
            refs_ = GameScenes(g_.sceneList_, AssetRef{g_.slot_.guid});
            listed_ = refs_.size();
            for (const AssetRef& r : g_.ScenesNotListed()) refs_.push_back(r);
            for (size_t i = 0; i < refs_.size(); ++i) {
                const AssetRef& r = refs_[i];
                editor::tree::Item it;
                it.id = IdOf(r);
                it.name = g_.SceneTitle(r);
                it.canHaveChildren = false;
                it.icon = editor::tree::Icon::Scene;
                if (r.Empty()) it.badge = Tr(L"未保存");
                if (it.name.empty() && !r.Empty()) {  // (一覧にあるがファイルが無い: 消された・Assets の外へ)
                    it.name = TrU8("(ファイルが見つかりません)");
                    it.dimmed = true;
                }
                if (i >= listed_) {
                    it.dimmed = true;
                    it.badge = Tr(L"一覧に無い");
                }
                out.push_back(std::move(it));
            }
        }
        std::wstring EmptyText() const override { return Tr(L"シーンがありません。右クリック →「新しいシーン」"); }
        bool CanRename(editor::tree::ItemId id) const override { return !g_.Running() && RefOf(id).has_value(); }
        void Rename(editor::tree::ItemId id, const std::string& name) override {
            if (const auto r = RefOf(id)) {
                g_.RenameScene(*r, name);
                if (!g_.sceneProblem_.empty()) log::Warn("シーンの名前を変えられません: {}", WideToUtf8(g_.sceneProblem_));
            }
        }
        bool CanMove() const override { return !g_.Running(); }
        // 新しい順: 一覧にあるもの (間にドラッグで入れた Assets のシーンも) を、今の行の並びのとおりに。
        void Move(std::span<const editor::tree::ItemId> items, editor::tree::ItemId parent, size_t index) override {
            std::vector<AssetRef> moved;
            for (const editor::tree::ItemId id : items) {
                if (const auto r = RefOf(id); r && !r->Empty()) moved.push_back(*r);
            }
            if (moved.empty()) return;
            std::vector<AssetRef> order;
            for (const AssetRef& r : refs_) {
                if (std::find(moved.begin(), moved.end(), r) == moved.end()) order.push_back(r);
            }
            // (どのシーンもほかのシーンを持たない: 行の真ん中に落とす = その直後)
            size_t at = std::min(index, order.size());
            if (parent != 0) {
                if (const auto p = RefOf(parent)) {
                    const auto it = std::find(order.begin(), order.end(), *p);
                    at = it == order.end() ? order.size() : static_cast<size_t>(it - order.begin()) + 1;
                }
            }
            order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), moved.begin(), moved.end());
            const std::vector<AssetRef>& listed = g_.sceneList_.scenes;
            std::vector<AssetRef> next;
            for (const AssetRef& r : order) {
                const bool inList = std::find(listed.begin(), listed.end(), r) != listed.end();
                const bool dragged = std::find(moved.begin(), moved.end(), r) != moved.end();
                if (!r.Empty() && (inList || dragged)) next.push_back(r);
            }
            if (next == listed) return;
            g_.EditSceneList(TrU8("シーンの順を変える"), [&](SceneList& l) { l.scenes = next; });
            g_.KeepOpenSceneChosen();
        }
        // 行を選んだ: ボタンを離したときに開く (その行でドラッグが始まったときは開かない)。
        void SelectionChanged(std::span<const editor::tree::ItemId> ids) override {
            if (ids.size() != 1) return;
            const auto r = RefOf(ids[0]);
            if (r && *r != AssetRef{g_.slot_.guid}) g_.sceneToOpen_ = *r;
        }
        std::vector<editor::tree::Action> Actions(std::span<const editor::tree::ItemId> selection) override {
            std::vector<editor::tree::Action> list;
            const bool playing = g_.Running();
            if (!selection.empty()) {
                if (const auto r = RefOf(selection.front())) {
                    const AssetRef ref = *r;
                    const bool listed = std::find(g_.sceneList_.scenes.begin(), g_.sceneList_.scenes.end(), ref) != g_.sceneList_.scenes.end();
                    editor::tree::Action open;
                    open.label = Tr(L"開く");
                    open.enabled = !playing && ref != AssetRef{g_.slot_.guid};
                    open.run = [this, ref] { g_.OpenSceneAsked(ref); };
                    list.push_back(std::move(open));
                    if (!listed && !ref.Empty()) {
                        editor::tree::Action add;
                        add.label = Tr(L"一覧に足す");
                        add.enabled = !playing;
                        add.run = [this, ref] { g_.EditSceneList(TrU8("シーンを一覧に足す"), [&](SceneList& l) { l.scenes.push_back(ref); }); };
                        list.push_back(std::move(add));
                    }
                    editor::tree::Action sep;
                    sep.separator = true;
                    list.push_back(sep);
                    for (auto& a : g_.SceneFileActions(ref)) list.push_back(std::move(a));
                    list.push_back(std::move(sep));
                }
            }
            editor::tree::Action make;
            make.label = Tr(L"新しいシーン");
            make.enabled = !playing;
            make.run = [this] { g_.NewSceneAsked(); };
            list.push_back(std::move(make));
            return list;
        }

    private:
        GameSoftware& g_;
        std::vector<AssetRef> refs_;  // 最後に一覧に出した行
        size_t listed_ = 0;           // ... ゲームのもの (残り: 一覧にない Assets のシーン)
    };

    // Assets にあって、ゲームの一覧にないシーン (開いているものは別にして)。
    std::vector<AssetRef> ScenesNotListed() {
        std::vector<AssetRef> out;
        const AssetRef open{slot_.guid};
        for (const std::string& path : SceneFiles()) {
            const AssetRef ref = host_.AssetOf(host_.GetProject().Root() / PathFromUtf8(path));
            if (ref.Empty() || ref == open || std::find(sceneList_.scenes.begin(), sceneList_.scenes.end(), ref) != sceneList_.scenes.end()) continue;
            out.push_back(ref);
        }
        return out;
    }

    // 開いているシーンが選ばれた行 (先に聞くクリックの後、ドラッグの後、別のシーンを開いた後)。
    void KeepOpenSceneChosen() { editor::tree::RequestSelect(sceneTree_, {SceneListSource::IdOf(AssetRef{slot_.guid})}); }

    void DrawScenesPanel(ui::Context& c, const Rect& r) {
        if (!sceneSource_) sceneSource_ = std::make_unique<SceneListSource>(*this);
        const AssetRef open{slot_.guid};
        if (shownInList_ != open) {
            shownInList_ = open;
            KeepOpenSceneChosen();
        }
        editor::tree::Draw(c, sceneTree_, *sceneSource_, r);
        // クリックした行: ボタンが上がり、ドラッグにならなかったら開く (先に保存を聞く)。
        if (sceneTree_.dragging) sceneToOpen_.reset();
        if (sceneToOpen_ && !c.In().down[0]) {
            const AssetRef ref = *std::exchange(sceneToOpen_, std::nullopt);
            KeepOpenSceneChosen();  // (別のものが開くまで、開いているものに留まる)
            if (Running()) {
                log::Info("Play 中はシーンを切り替えられません (■ で止めてから)");
            } else {
                OpenSceneAsked(ref);
            }
        }
    }

    // シーンのファイルの名前を変えた (id はそのまま: 一覧と Game::LoadScene は新しい名前を使う)。
    // まだ保存していない新しいシーンは、これからできるファイルのもの。
    void RenameScene(const AssetRef& ref, const std::string& to) {
        sceneProblem_.clear();
        if (to.empty()) return;
        if (ValidateFileName(to, 64) != FileNameError::None) {
            sceneProblem_ = Tr(L"ファイルの名前に使えない文字が入っています");
            return;
        }
        if (ref.Empty()) {
            std::string path = std::string(kScenesFolder) + "/" + to + kSceneExtension;
            std::error_code ec;
            if (fs::exists(FullPath(path), ec)) {
                sceneProblem_ = Tr(L"その名前のシーンはもうあります");
                return;
            }
            slot_.path = std::move(path);
        } else {
            const std::wstring problem = host_.RenameAsset(ref, to + kSceneExtension);
            if (!problem.empty()) {
                sceneProblem_ = problem;
                return;
            }
            if (ref.guid == slot_.guid) SlotNow();  // (そのファイルは id で見つかる)
        }
        log::Info("シーンの名前を「{}」にしました", to);
        renamingScene_ = false;
        host_.Redraw();
    }

    // Assets/ のシーンファイル (プロジェクトのフォルダから)。ときどき見て回る。
    const std::vector<std::string>& SceneFiles() {
        const double now = platform::NowSeconds();
        if (now - sceneFilesTime_ < 2.0 && sceneFilesTime_ > 0) return sceneFiles_;
        sceneFilesTime_ = now;
        sceneFiles_.clear();
        const fs::path root = host_.GetProject().Root();
        std::error_code ec;
        for (fs::recursive_directory_iterator it(host_.GetProject().AssetsDir(), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != kSceneExtension) continue;
            sceneFiles_.push_back(PathToUtf8(it->path().lexically_relative(root).generic_wstring()));
            if (sceneFiles_.size() >= 200) break;
        }
        std::sort(sceneFiles_.begin(), sceneFiles_.end());
        return sceneFiles_;
    }

    // --- Prefab -------------------------------------------------------------------------------
    // Prefab のファイルはアセット (Assets/ の .rmprefab) で、id (AssetRef) で名指しする。動かしたり
    // 名前を変えたりしても、置いたものすべてのリンクが保たれる。アセットに id がなかった前のデータは、
    // プロジェクトのフォルダからのパスで名指ししている - 同じように読み (SoftwareHost::AssetFile)、
    // 開いたときに id にする。
    static constexpr size_t kMaxNesting = kMaxPrefabNesting;  // (runtime/Prefab.h)

    std::filesystem::path FullPath(const std::string& path) const { return host_.GetProject().Root() / PathFromUtf8(path); }
    // ファイルの、プロジェクトのフォルダからのパス。部分の間は "/"。
    std::string RelativePath(const fs::path& file) const {
        return PathToUtf8(file.lexically_normal().lexically_relative(host_.GetProject().Root().lexically_normal()).generic_wstring());
    }

    // --- シーン (runtime/SceneFile.h) --------------------------------------------------------
    // 新しいシーンのファイルの置き場所: Assets/Scenes/Main.rmscene (取られていたら "Main 2" ...)。
    std::string NewSceneFile(std::string_view name = "Main") const {
        return NewScenePath(name, [this](const std::string& p) {
            std::error_code ec;
            return fs::exists(FullPath(p), ec);
        });
    }

    // 一覧が見せるシーンを読む (そのファイルは id で見つける)。読めないものはシーンを空にし、
    // 上書きしない (編集したシーンは、保存のとき新しいファイルへ行く)。
    void ReadOpenScene(reflect::Warnings& warnings) {
        const AssetRef open = OpenScene(sceneList_);
        if (open.Empty()) return;  // まだシーンがない: 最初の保存で新しいファイル
        const fs::path file = host_.AssetFile(open);
        std::string err;
        std::optional<json::Value> scene;
        if (file.empty()) {
            err = TrU8("ファイルが Assets に見つかりません");
        } else if (auto bytes = platform::ReadFileBytes(file, &err)) {
            scene = ReadSceneFile(*bytes, &err, &host_.Registry());
            if (scene) slot_ = {open.guid, RelativePath(file), TextSign(*bytes)};
        }
        if (!scene) {
            sceneUnread_ = true;
            replaces_ = file.empty() ? open.guid : std::string();  // ないファイルの、一覧での場所は新しいものへ行く
            log::Error("シーン ({}) を読めませんでした: {}。空のシーンで開きます。編集して保存すると新しいファイルに書きます (元のファイルは上書きしません)",
                       file.empty() ? open.guid : PathToUtf8(file), err);
            return;
        }
        scene_.Load(*scene, &warnings);
    }

    // 今のシーンの置き場所: その後に動かしたり名前を変えたりしたファイルは id で見つかる。その後に
    // 消されたものは新しいシーンとして書き直す (新しいファイルと id で、一覧の同じ場所に)。
    SceneSlot SlotNow() {
        if (slot_.guid.empty()) {
            // 読んだときではなく、今決める: その後に置かれたファイル (ごみ箱から戻した Main.rmscene)
            // には、決して上書きしない。
            slot_ = {"", NewSceneFile(), 0};
            return slot_;
        }
        if (const fs::path file = host_.AssetFile(AssetRef{slot_.guid}); !file.empty()) {
            slot_.path = RelativePath(file);
            return slot_;
        }
        log::Warn("シーンのファイルが無くなっていたので、新しいファイルに保存します: {}", slot_.path);
        replaces_ = slot_.guid;
        slot_ = {"", NewSceneFile(), 0};
        return slot_;
    }

    // Prefab の名前 (.rmprefab を除いたファイルの名前) と、そのファイルの場所。画面用。
    std::string PrefabTitle(const AssetRef& ref) const {
        const std::filesystem::path f = host_.AssetFile(ref);
        return f.empty() ? PrefabName(ref.guid) : PathToUtf8(f.stem());
    }
    std::string PrefabWhere(const AssetRef& ref) const {
        const std::filesystem::path f = host_.AssetFile(ref);
        return f.empty() ? ref.guid : PathToUtf8(f.lexically_relative(host_.GetProject().Root()).generic_wstring());
    }

    // Prefab のファイルを今読む (その最後に分かった形として持つ)。読めないときは何も返さない。
    const json::Value* ReadPrefab(const AssetRef& ref) {
        const std::filesystem::path file = host_.AssetFile(ref);
        if (file.empty()) {
            log::Warn("Prefab のファイル ({}) がプロジェクトの Assets に見つかりません (消されたか、外へ動かされました)", ref.guid);
            return nullptr;
        }
        std::string err;
        const auto bytes = platform::ReadFileBytes(file, &err);
        if (!bytes) {
            log::Warn("Prefab「{}」を読めませんでした: {}", PathToUtf8(file.filename()), err);
            return nullptr;
        }
        json::ParseError perr;
        auto v = json::Parse(*bytes, &perr);
        if (!v || !IsPrefab(*v)) {
            log::Warn("「{}」は Prefab のファイルではありません", PathToUtf8(file.filename()));
            return nullptr;
        }
        UpgradePrefab(*v, host_.Registry());
        if (NewerPrefab(*v)) {
            log::Warn("Prefab「{}」は新しい版のエンジンで作られたので読みません (置いた物は保存したまま)", PathToUtf8(file.filename()));
            return nullptr;
        }
        ++dataVersion_;
        return &(prefabs_[ref.guid] = std::move(*v));
    }
    const json::Value* Cached(const AssetRef& ref) {
        const auto it = prefabs_.find(ref.guid);
        return it != prefabs_.end() ? &it->second : ReadPrefab(ref);
    }

    // `scene` に置いた各 Prefab が変えたもの。最後に読んだ Prefab と比べて今求める
    // (ファイルと、入れ子の Prefab のリンクに持つので、変えた Prefab をその下に入れ直せる)。
    // `scene` は scene_ か、今の状態のそのコピー (SaveData)。
    void UpdateOverrides(Scene& scene) const {
        for (const int root : scene.PrefabRoots()) {
            if (const json::Value* o = CachedOverrides(root)) scene.Find(root)->prefab.overrides = *o;
        }
    }

    // 置いた Prefab `root` が、最後に読んだ Prefab と比べて変えたもの。シーンの変更 (履歴の変更番号)、
    // Prefab のファイル、読み込んだデータが変わるごとに 1 回だけ求める。インスペクターを描くたびではない。
    // null: 置いた Prefab ではない、またはそのファイルを読んでいない。
    const json::Value* CachedOverrides(int root) const {
        const std::pair<uint64_t, uint64_t> stamp{host_.History().Changes(), dataVersion_};
        if (stamp != overridesStamp_) {
            overridesCache_.clear();
            overridesStamp_ = stamp;
        }
        if (const auto it = overridesCache_.find(root); it != overridesCache_.end()) return &it->second;
        const SceneEntity* r = scene_.Find(root);
        if (!r || !r->prefab.Root()) return nullptr;
        const auto p = prefabs_.find(r->prefab.asset.guid);
        if (p == prefabs_.end()) return nullptr;
        return &(overridesCache_[root] = Overrides(scene_, root, p->second, placementTypes_));
    }

    // `root` が中にある Prefab (とそれ自身): Prefab は自分の中を埋めることはない。
    std::vector<std::string> ChainOf(int root) const { return ChainOf(scene_, root); }
    static std::vector<std::string> ChainOf(const Scene& s, int root) {
        std::vector<std::string> chain;
        size_t steps = 0;
        for (const SceneEntity* e = s.Find(root); e && steps++ <= s.Entities().size(); e = s.Find(e->data.parent)) {
            if (e->prefab.Root()) chain.push_back(e->prefab.asset.guid);
        }
        return chain;
    }

    // `prefab` の中に `guid` があるとき true (入れ子の、どの深さでも。最後に読んだファイルをたどって)。
    bool ContainsPrefab(const json::Value& prefab, const std::string& guid, size_t depth = 0) {
        if (depth > kMaxNesting) return true;  // まともな深さを超えた: 輪になっている
        for (const std::string& a : NestedAssets(prefab)) {
            if (a == guid) return true;
            if (const json::Value* inner = Cached(AssetRef{a}); inner && ContainsPrefab(*inner, guid, depth + 1)) return true;
        }
        return false;
    }

    // 置いた Prefab `root` の中に入れ子になった Prefab を、それぞれのファイルから埋める (それぞれの
    // 上書きも付けて)。さらにその中に入れ子のものも ...
    void FillNested(int root, std::vector<std::string>& chain, reflect::Warnings& warnings) {
        FillNestedPrefabs(
            scene_, root, chain, [this](const AssetRef& r) { return Cached(r); }, warnings, [this](const AssetRef& r) { return PrefabTitle(r); });
    }

    // 置いた Prefab `root` を、`prefab` と `overrides` からもう一度作り、それから中の入れ子も。
    void Remake(int root, const json::Value& prefab, const json::Value& overrides, reflect::Warnings& warnings) {
        scene_.ApplyPrefab(root, prefab, overrides, &warnings);
        std::vector<std::string> chain = ChainOf(root);
        FillNested(root, chain, warnings);
    }

    // Prefab のファイルが、そこに入れ子になった Prefab (エンティティ `id`) のために持つ上書き。
    static json::Value FileOverrides(const json::Value& prefab, int id) {
        for (const auto& e : prefab.Get("entities").Items()) {
            if (e.Get("id").AsInt(0) == id) return e.Get("prefab").Get("overrides");
        }
        return {};
    }

    // プロジェクトを開くとき: 置いた Prefab すべてを、そのファイルから (変えたものも付けて)
    // 外側から作り直す。だから (別のシーンや手で) 後から変えた Prefab が見える。編集ではない:
    // プロジェクトはそのまま。
    void RefreshPrefabsOnOpen() {
        prefabs_.clear();
        ++dataVersion_;
        // アセットに id がなかった前のデータ: パスで名指ししていた Prefab は、そのファイルの id をもらう
        // (次の保存でそう書かれる)。
        for (const int root : scene_.PrefabRoots()) {
            AssetRef& ref = scene_.Find(root)->prefab.asset;
            if (asset::IsGuid(ref.guid)) continue;
            if (const std::filesystem::path file = host_.AssetFile(ref); !file.empty()) {
                if (const AssetRef id = host_.AssetOf(file); !id.Empty()) ref = id;
            }
        }
        reflect::Warnings warnings;
        RefreshPrefabsIn(scene_, warnings);
        for (const auto& w : warnings) log::Warn("Prefab: {}", w);
    }

    // `s` に置いた Prefab すべてを、そのファイルから (変えたものも付けて) 外側から作り直す:
    // 開いたシーンと、Play が切り替えるときや書き出すときのゲームのほかのシーン (そのファイルは
    // 最後の保存時点の Prefab を持つ - 後から変えた Prefab も、そこに見える)。
    void RefreshPrefabsIn(Scene& s, reflect::Warnings& warnings) {
        for (const int root : s.PrefabRoots()) {
            const SceneEntity* r = s.Find(root);
            if (!r) continue;  // 周りの Prefab と一緒に消えた
            if (r->prefab.outer > 0 && s.PrefabRootOf(r->data.parent) != 0) continue;  // 周りのものと一緒に埋めた
            const AssetRef ref = r->prefab.asset;
            const json::Value overrides = r->prefab.overrides;
            if (const json::Value* prefab = Cached(ref)) {
                const json::Value copy = *prefab;
                s.ApplyPrefab(root, copy, overrides, &warnings);
            }  // (そうでなければ保存したまま。中のものは今もそのファイルに従う)
            std::vector<std::string> chain = ChainOf(s, root);
            FillNestedPrefabs(
                s, root, chain, [this](const AssetRef& a) { return Cached(a); }, warnings, [this](const AssetRef& a) { return PrefabTitle(a); });
        }
    }

    // `ref` に置いた Prefab すべてを `next` から作り直す。それぞれが `before` から変えたものは
    // 保つ (`skip` は除く)。そこで変えていない Prefab に入れ子になった Prefab は、`next` が持つものを
    // 取る。Edit の中で。UpdateOverrides は、キャッシュがまだ `before` を持っている間に呼んである。
    void ReapplyPrefab(const AssetRef& ref, const json::Value& before, const json::Value& next, int skip) {
        reflect::Warnings warnings;
        for (const int root : scene_.PrefabRoots(ref.guid)) {
            if (root == skip || !scene_.Find(root)) continue;
            const json::Value overrides = Overrides(scene_, root, before, placementTypes_);
            std::vector<std::pair<int, int>> untouched;  // (入れ子の root、ファイルでのその id)
            for (const int n : scene_.NestedPrefabs(root)) {
                const PrefabLink& l = scene_.Find(n)->prefab;
                if (l.overrides == FileOverrides(before, l.outer)) untouched.emplace_back(n, l.outer);
            }
            scene_.ApplyPrefab(root, next, overrides, &warnings);
            for (const auto& [n, id] : untouched) {
                SceneEntity* e = scene_.Find(n);
                if (const json::Value fresh = FileOverrides(next, id); e && fresh.IsObject()) e->prefab.overrides = fresh;
            }
            std::vector<std::string> chain = ChainOf(root);
            FillNested(root, chain, warnings);
        }
        for (const auto& w : warnings) log::Warn("Prefab「{}」: {}", PrefabTitle(ref), w);
    }

    // Assets/ の Prefab ファイル (探し直すのは 2 秒に 1 回まで: メニューはよく聞かれる)。
    const std::vector<std::string>& PrefabFiles() {
        const double now = platform::NowSeconds();
        if (now - prefabFilesTime_ < 2.0 && prefabFilesTime_ > 0) return prefabFiles_;
        prefabFilesTime_ = now;
        prefabFiles_.clear();
        const std::filesystem::path root = host_.GetProject().Root();
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(host_.GetProject().AssetsDir(), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != kPrefabExtension) continue;
            prefabFiles_.push_back(PathToUtf8(it->path().lexically_relative(root).generic_wstring()));
            if (prefabFiles_.size() >= 200) break;
        }
        std::sort(prefabFiles_.begin(), prefabFiles_.end());
        return prefabFiles_;
    }

    // エンティティの名前からつける、新しい Prefab のための Assets/ のファイル名 (すでにあるものではない)。
    std::string NewPrefabPath(const std::string& entityName) const {
        const std::string stem = ValidateFileName(entityName, 48) == FileNameError::None ? entityName : std::string("Prefab");
        std::error_code ec;
        for (int n = 1;; ++n) {
            const std::string name = n == 1 ? stem : std::format("{} ({})", stem, n);
            const std::string path = "Assets/" + name + kPrefabExtension;
            if (!std::filesystem::exists(FullPath(path), ec)) return path;
        }
    }

    bool WriteFile(const std::filesystem::path& file, const json::Value& prefab) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::string err;
        if (!platform::WriteFileAtomic(file, json::Write(prefab), &err)) {
            log::Error("Prefab「{}」を書けませんでした: {}", PathToUtf8(file.filename()), err);
            return false;
        }
        prefabFilesTime_ = 0;  // もう一度探す
        return true;
    }

    // Prefab ファイルのテキストを書き (アセットが今どこにあっても)、その最後に分かった形として持つ。
    bool WriteBytes(const AssetRef& ref, const std::string& text) {
        const std::filesystem::path file = host_.AssetFile(ref);
        std::string err;
        if (file.empty() || !platform::WriteFileAtomic(file, text, &err)) {
            log::Error("Prefab「{}」のファイルを書けませんでした: {}", PrefabTitle(ref), file.empty() ? std::string("見つかりません") : err);
            return false;
        }
        if (auto v = json::Parse(text); v && IsPrefab(*v)) {
            UpgradePrefab(*v, host_.Registry());
            prefabs_[ref.guid] = std::move(*v);
        } else {
            prefabs_.erase(ref.guid);
        }
        ++dataVersion_;
        return true;
    }

    void PrefabActions(const std::vector<int>& sel, std::vector<tree::Action>& list) {
        tree::Action sep;
        sep.separator = true;
        list.push_back(sep);
        if (sel.size() == 1 && scene_.Find(sel[0])) {
            const int id = sel[0];
            tree::Action make;
            make.label = Tr(L"Prefab にする (Assets に保存)");
            make.run = [this, id] { MakePrefabOf(id); };
            list.push_back(std::move(make));
            if (const int root = scene_.PrefabRootOf(id)) {
                const std::wstring name = Utf8ToWide(PrefabTitle(scene_.Find(root)->prefab.asset));
                tree::Action apply;
                apply.label = TrF(L"Prefab「{}」に反映 (置いたほかの物も変わる)", name);
                apply.run = [this, root] { ApplyToPrefab(root); };
                list.push_back(std::move(apply));
                tree::Action reload;
                reload.label = TrF(L"Prefab「{}」を読み直す", name);
                reload.run = [this, root] { ReloadPrefab(root); };
                list.push_back(std::move(reload));
                tree::Action unlink;
                unlink.label = TrF(L"Prefab「{}」から切り離す", name);
                unlink.run = [this, root] {
                    EditTouching({root}, std::format("「{}」を Prefab から切り離す", NameOf(root)), [&] { scene_.UnlinkPrefab(root); });
                };
                list.push_back(std::move(unlink));
            }
        }
        // 置く: 選んだエンティティの後に、貼り付け と同じく。
        const std::vector<std::string>& files = PrefabFiles();
        for (size_t i = 0; i < files.size() && i < kMaxPrefabRows; ++i) {
            tree::Action place;
            place.label = TrF(L"Prefab を置く: {}", Utf8ToWide(files[i]));
            place.run = [this, sel, path = files[i]] { PlacePrefabFile(path, sel); };
            list.push_back(std::move(place));
        }
        if (files.empty()) {
            tree::Action none;
            none.label = Tr(L"Prefab を置く (Assets に Prefab がありません)");
            none.enabled = false;
            list.push_back(std::move(none));
        } else if (files.size() > kMaxPrefabRows) {
            tree::Action more;
            more.label = TrF(L"… ほか {} 個の Prefab", files.size() - kMaxPrefabRows);
            more.enabled = false;
            list.push_back(std::move(more));
        }
    }

    void MakePrefabOf(int id) {
        const SceneEntity* e = scene_.Find(id);
        if (!e) return;
        const std::string name = e->data.name;
        const std::string path = NewPrefabPath(name);
        UpdateOverrides(scene_);  // 中に置いた Prefab は、それに加えた変更とともに保つ
        std::unordered_map<int, int> sources;
        const json::Value prefab = MakePrefab(scene_, id, &sources);
        if (!WriteFile(FullPath(path), prefab)) return;
        const AssetRef ref = host_.AssetOf(FullPath(path));  // 新しいファイルの id (.meta は今書く)
        if (ref.Empty()) {
            log::Error("Prefab「{}」をアセットにできませんでした (Assets の中に作れませんでした)", path);
            return;
        }
        prefabs_[ref.guid] = prefab;
        ++dataVersion_;
        EditTouching({id}, std::format("「{}」を Prefab にする", name), [&] { scene_.LinkPrefab(id, ref, sources); });
        log::Info("Prefab を作りました: {}", path);
    }

    void ApplyToPrefab(int root) {
        const SceneEntity* r = scene_.Find(root);
        if (!r || !r->prefab.Root()) return;
        const AssetRef ref = r->prefab.asset;
        const std::filesystem::path file = host_.AssetFile(ref);
        if (file.empty()) {
            log::Warn("Prefab「{}」のファイルが見つからないので反映できません", PrefabTitle(ref));
            return;
        }
        // 今のファイル: Undo のために丸ごと持つ。このバージョンが読めない Prefab (新しい版のもの、
        // 壊れたもの) のときは、決して上書きしない。
        std::string err;
        const auto onDisk = platform::ReadFileBytes(file, &err);
        auto parsed = onDisk ? json::Parse(*onDisk) : std::nullopt;
        if (parsed) UpgradePrefab(*parsed, host_.Registry());
        if (!parsed || !IsPrefab(*parsed) || NewerPrefab(*parsed)) {
            log::Warn("Prefab「{}」のファイルが読めない (新しい版で作られたか、壊れている) ので、反映しません", PrefabTitle(ref));
            return;
        }
        // 元の Prefab (置いたほかのものが変えたものは、これと比べて求める)。
        json::Value before = *parsed;
        if (const json::Value* old = Cached(ref)) before = *old;
        UpdateOverrides(scene_);  // キャッシュがまだ元の Prefab を持っている間に
        std::unordered_map<int, int> sources;
        const json::Value next = MakePrefab(scene_, root, &sources, &*parsed);  // (ファイルのほかのキーはそのまま)
        if (ContainsPrefab(next, ref.guid)) {
            log::Warn("Prefab「{}」の中に「{}」自身が入ることになるので、反映しません", PrefabTitle(ref), PrefabTitle(ref));
            return;
        }
        const std::string nextText = json::Write(next);
        if (!WriteBytes(ref, nextText)) return;
        // Undo はファイルを元のとおりに書き戻し (置いたものも一緒に)、Redo はもう一度書く。
        auto fileCmd = std::make_unique<FileCommand>([this, ref](const std::string& text) { WriteBytes(ref, text); }, *onDisk, nextText,
                                                   std::format("Prefab「{}」のファイル", PrefabTitle(ref)));
        EditTouching(scene_.PrefabRoots(ref.guid), 
            std::format("Prefab「{}」に反映", PrefabTitle(ref)),
            [&] {
                scene_.LinkPrefab(root, ref, sources);
                ReapplyPrefab(ref, before, next, root);
            },
            std::move(fileCmd));
        log::Info("Prefab「{}」に反映しました (置いた物 {} 個)", PrefabWhere(ref), scene_.PrefabRoots(ref.guid).size());
    }

    // 今のファイル (手で、または別のプロジェクトで変えた): 置いたものすべてがそれに従う。
    void ReloadPrefab(int root) {
        const SceneEntity* r = scene_.Find(root);
        if (!r || !r->prefab.Root()) return;
        const AssetRef ref = r->prefab.asset;
        const auto it = prefabs_.find(ref.guid);
        const json::Value before = it != prefabs_.end() ? it->second : json::Value();
        UpdateOverrides(scene_);
        const json::Value* read = ReadPrefab(ref);
        if (!read) return;
        const json::Value next = *read;
        EditTouching(scene_.PrefabRoots(ref.guid), std::format("Prefab「{}」を読み直す", PrefabTitle(ref)), [&] { ReapplyPrefab(ref, IsPrefab(before) ? before : next, next, 0); });
    }

    void PlacePrefabFile(const std::string& path, const std::vector<int>& sel) {
        const AssetRef ref = host_.AssetOf(FullPath(path));
        if (ref.Empty()) {
            log::Warn("「{}」は Assets の中のファイルではないので置けません", path);
            return;
        }
        const json::Value* prefab = ReadPrefab(ref);
        if (!prefab) return;
        const json::Value copy = *prefab;
        const auto [parent, index] = After(sel);
        int made = 0;
        reflect::Warnings warnings;
        EditTouching({}, std::format("Prefab「{}」を置く", PrefabTitle(ref)), [&] {
            made = scene_.PlacePrefab(copy, ref, parent, index, &warnings);
            if (!made) return;
            std::vector<std::string> chain = ChainOf(made);
            FillNested(made, chain, warnings);  // その中の Prefab を、それぞれのファイルから
        });
        for (const auto& w : warnings) log::Warn("Prefab「{}」: {}", path, w);
        if (made) host_.SelectInTree({static_cast<tree::ItemId>(made)});
    }

    // --- 編集 -------------------------------------------------------------------------------
    std::string NameOf(int id) const {
        const SceneEntity* e = scene_.Find(id);
        return e ? e->data.name : std::string();
    }

    // シーンの形の変更を実行し、履歴に入れる (編集 = Undo の項目 = 未保存)。
    // 何も変えない変更は項目を残さない。Play 中は決してしない (動くのはコピー)。
    // シーンの形の変更が履歴に入る唯一の道 (編集 = Undo の項目 = 未保存):
    // `touched` は、その変更が変えるエンティティ (その下のものすべてを含む - SceneRecord.h)
    // を挙げる。変更が作るものは自分で見つかる。それだけを前と後で持つ。`also`:
    // それと一緒にシーンの外でしたこと (ファイルを書いた) で、1 つの項目で一緒に Undo する。
    // 何も変えない変更は項目を残さない。Play 中は決してしない (動くのはコピー)。
    // デバッグビルド: すべての編集を、前と後でシーン全体と照らして調べる (触れたエンティティを
    // 挙げ忘れると Undo が間違う - 記録がまだ小さいうちに、これですぐ見つかる)。
    template <class Fn>
    void EditTouching(std::vector<int> touched, std::string label, Fn&& change, std::unique_ptr<ICommand> also = nullptr) {
        if (Running()) {
            log::Info("Play 中は編集できません (■ で止めてから)");
            return;
        }
#ifdef _DEBUG
        const json::Value wholeBefore = ShapeOf(scene_);
#endif
        SceneRecord record = SceneRecord::Begin(scene_, touched);
        change();
        record.End(scene_);
#ifdef _DEBUG
        {
            const json::Value wholeAfter = ShapeOf(scene_);
            Scene check = scene_;
            record.Undo(check);
            const bool undoOk = ShapeOf(check) == wholeBefore;
            record.Redo(check);
            const bool redoOk = ShapeOf(check) == wholeAfter;
            if (!undoOk || !redoOk) log::Error("SceneRecord:「{}」で、触ると言っていない物が変わりました (Undo が正しく戻せません)", label);
            assert(undoOk && redoOk && "SceneRecord: the edit changed an entity it did not name as touched");
        }
#endif
        if (!record.Changed() && !also) return;
        auto cmd = std::make_unique<RecordCommand>(scene_, std::move(record), label);
        if (!also) {
            host_.History().Push(std::move(cmd));
            return;
        }
        auto group = std::make_unique<CommandGroup>(label);
        group->Add(std::move(cmd));
        group->Add(std::move(also));
        host_.History().Push(std::move(group));
    }

    // (前と後のシーン全体: 今これを使う編集はない - どれも触れるものを挙げる。
    // 触れるエンティティを挙げられない変更のために残してある。)
    template <class Fn>
    void Edit(std::string label, Fn&& change, std::unique_ptr<ICommand> also = nullptr) {
        if (Running()) {
            log::Info("Play 中は編集できません (■ で止めてから)");
            return;
        }
        json::Value before = scene_.Save();
        change();
        json::Value after = scene_.Save();
        if (!also) {
            if (before == after) return;
            host_.History().Push(std::make_unique<SceneCommand>(scene_, std::move(before), std::move(after), std::move(label)));
            return;
        }
        auto group = std::make_unique<CommandGroup>(label);
        if (before != after) group->Add(std::make_unique<SceneCommand>(scene_, std::move(before), std::move(after), label));
        group->Add(std::move(also));
        host_.History().Push(std::move(group));
    }

    bool CopyEntities(const std::vector<int>& ids) {
        const json::Value clip = scene_.Copy(ids);
        if (!host_.SetClipboard(json::Write(clip))) {
            log::Warn("クリップボードに書けませんでした (ほかのアプリが使っているかもしれません)");
            return false;
        }
        log::Info("{} 個のエンティティをコピーしました", clip.Get("entities").Items().size());
        return true;
    }

    void PasteEntities(int parent, size_t index) {
        const auto clip = json::Parse(host_.Clipboard());
        if (!clip || !Scene::IsClip(*clip)) {
            log::Info("クリップボードにエンティティがありません");
            return;
        }
        std::vector<int> made;
        reflect::Warnings warnings;
        EditTouching({}, std::format("{} 個のエンティティを貼り付け", clip->Get("entities").Items().size()),
             [&] { made = scene_.Paste(*clip, parent, index, &warnings); });
        for (const auto& w : warnings) log::Warn("貼り付け: {}", w);
        host_.SelectInTree({made.begin(), made.end()});
    }

    // 追加 ▸ ... (プラグインの CreateItemInfo): 選んだものの下か、選択の後に。
    void CreateItem(const CreateItemInfo& info, const std::vector<int>& sel) {
        int parent = 0;
        size_t index = Scene::kEnd;
        if (info.child && sel.size() == 1) {
            parent = sel[0];
        } else {
            std::tie(parent, index) = After(sel);
        }
        int made = 0;
        EditTouching({}, std::format("「{}」を作成", WideToUtf8(info.label)), [&] {
            if (info.make) {
                made = info.make(scene_, parent, index);
            } else {
                made = MakeEntity(WideToUtf8(Tr(info.label)), parent, index);  // (使っている言語での名前)
                info.fill(scene_, made);
            }
            // ほかのプラグインがそれに足すもの (物理: その形のコライダー) - 同じ Undo の項目の中で。
            if (!made) return;
            for (const auto& c : host_.Registry().Contributions<CreatedInfo>(kCreatedPoint)) {
                if (c.item->added) c.item->added(scene_, made, info.made ? info.made : "");
            }
        });
        if (made) host_.SelectInTree({static_cast<tree::ItemId>(made)}, true);
    }

    // 空のエンティティ が作るエンティティ: 新しいエンティティがもらう部品 (たとえば
    // ゲームの空間の Transform)。
    int MakeEntity(std::string name, int parent, size_t index) {
        const int made = scene_.Create(std::move(name), parent, index);
        const std::string space = CurrentSpace();
        for (const ComponentInfo* info : newEntityInfos_) {
            if (!info->space || info->space == space) scene_.AddComponent(made, info->type());
        }
        return made;
    }

    // 追加 ▸ のフォルダの順 (プラグインが書いたままのキー。翻訳する前): ほかのものは
    // これらの後に、プラグインの順で。
    static int CreateRank(const wchar_t* folder) {
        static constexpr std::wstring_view kOrder[] = {L"3D オブジェクト", L"2D オブジェクト", L"カメラ", L"ライト", L"UI", L"エフェクト", L"音"};
        if (!folder) return -1;  // (追加 itself: first)
        const std::wstring_view f(folder);
        for (size_t i = 0; i < std::size(kOrder); ++i) {
            if (f == kOrder[i] || f.starts_with(std::wstring(kOrder[i]) + L"/")) return static_cast<int>(i);
        }
        return static_cast<int>(std::size(kOrder));
    }

    static std::vector<std::wstring> SplitPath(std::wstring_view path) {
        std::vector<std::wstring> parts;
        for (size_t start = 0; start <= path.size();) {
            const size_t slash = std::min(path.find(L'/', start), path.size());
            if (slash > start) parts.emplace_back(path.substr(start, slash - start));
            start = slash + 1;
        }
        return parts;
    }

    void CreateEntity(int parent, size_t index) {
        int made = 0;
        EditTouching({}, "エンティティを作成", [&] {
            made = MakeEntity(TrU8("エンティティ"), parent, index);  // 始めの名前。使っている言語で
        });
        if (made) host_.SelectInTree({static_cast<tree::ItemId>(made)}, true);
    }

    editor::SoftwareHost& host_;
    GameSettings settings_;
    reflect::Unknowns unknowns_;  // 新しい版のキーや enum の名前: そのまま書き戻す
    Scene scene_;
    std::vector<int> selection_;  // 階層の選択 (シーンビューに見せる)
    std::vector<View> views_;  // 出している空間ごとに 1 つ。既定のもの (一番小さい order) が先
    bool viewsAsked_ = false;
    std::vector<const ComponentInfo*> newEntityInfos_;  // 新しいエンティティがもらう部品 (ゲームの空間の)
    std::vector<const ComponentLook*> componentLooks_;      // エディターでの見せ方 (エディター部分)
    std::vector<std::string> placementTypes_;
    std::unordered_map<std::string, const char*> componentFolders_;  // TypeDesc::name -> ComponentInfo::folder               // 置いた Prefab がある場所 (Transform)
    json::Value view_;  // UserSettings: ユーザーの見方 (シーンビューのカメラ ...)
    std::unordered_map<int, int> modes_;  // エンティティ id -> 派生値のある部品の見せ方
    bool hadData_ = false;  // .rmpf にゲームのデータがある (そこから読んだ、または保存が書いた)
    bool gave_ = false;     // 最後の SaveData がデータを出した (Saved: 今は .rmpf にある)
    // シーン (runtime/SceneFile.h): .rmpf の一覧と、見せているシーンのファイル。
    SceneList sceneList_;
    reflect::Unknowns sceneListUnknowns_;  // 新しい版のキー: そのまま書き戻す
    SceneSlot slot_;
    std::string replaces_;      // ファイルがなくなったシーンの id: 新しいファイルが一覧でその場所に入る
    bool oldForm_ = false;      // .rmpf の中から読んだ (前の形のプロジェクト): 次の保存で分ける
    bool sceneUnread_ = false;  // 一覧のシーンが読めなかった: 上書きしない
    struct Pending {
        SceneSlot slot;
        SceneList list;
    };
    std::optional<Pending> pending_;  // SaveData が出したもの。Saved() が取り込む
    std::vector<std::string> sceneFiles_;  // SceneFiles()
    double sceneFilesTime_ = 0;
    // 書き出し (GameExport.h): プロジェクト側の部分、パネル、ワーカーで走る書き出し。
    ExportSettings exportSettings_;
    reflect::Unknowns exportUnknowns_;
    editor::inspector::State exportInspector_;
    struct ExportRun {
        std::mutex mtx;
        float progress = 0;
        std::vector<std::string> lines;
        bool running = false;
        std::optional<bool> ok;  // 終わったときに入れる
    };
    std::shared_ptr<ExportRun> exportRun_ = std::make_shared<ExportRun>();
    std::jthread exportThread_;
    bool inspectingEntities_ = false;  // インスペクターは階層の選択を見せている (設定ではなく)
    bool inspectingScene_ = false;     // ... シーン自身の値 (階層の一番上の行が選ばれて描かれる)
    std::vector<const SceneSettingLook*> settingLooks_;              // エディターでの見せ方 (エディター部分)
    std::unordered_map<std::string, std::string> settingSpaces_;    // 設定の型 -> それが使われるゲームの空間

    // Play
    PlayState play_ = PlayState::Stopped;
    GameScripts scripts_;  // ゲームの C++ スクリプト (ビルド、読み込み、入れ替え。Play 中に動く)
    reflect::Warnings heldSceneWarnings_;  // 最初のビルドの後に知らせる (LoadData を見る)
    DebugDraw debug_;      // Play 中にスクリプトが rm::Debug で描くもの (ビューが見せる。Stop で消える)
    std::vector<const reflect::TypeDesc*> baseTypes_, baseMultiple_;  // プラグインの部品の型
    std::unique_ptr<Scene> playScene_;  // 動かすコピー
    std::vector<Kept> kept_;            // この値を残す: Stop のときに書き戻す
    std::vector<std::pair<Kept, json::Value>> keptLeft_;  // ... Play 前のシーンを離れたときの値 (Game::LoadScene)
    bool mouseInGame_ = false;          // ゲームビューの上のマウス (最後に描いたとき)
    // デバッグ パネルと、それが見せるもの: ビューを描く頻度と Play のフレームが走る頻度、シーンビューの
    // 上のマウス。
    DebugPanel debugPanel_;
    std::unique_ptr<editor::ToolWindow> debugWindow_;  // 開いている間の デバッグ ウィンドウ
    // 開いている間の ゲームの設定 ウィンドウ: そのタブ、設定のタブごとのインスペクター、ビルド
    // タブの 2 つ目 (出力先)、初期シーン の一覧。
    std::unique_ptr<editor::ToolWindow> settingsWindow_;
    int settingsTab_ = 0;
    std::array<editor::inspector::State, 4> settingsInspectors_;
    editor::inspector::State outputInspector_;
    editor::w::Popup firstScenePopup_;
    editor::w::Popup spacePopup_;  // ゲームの設定の「ゲームの空間」
    FrameMeter sceneMeter_, gameMeter_, updateMeter_;
    json::Value settingsBeforePlay_;  // Play が始まったときのゲーム設定 (null: Play 中ではない)
    double debugShownAt_ = -1e9;  // そのときウィンドウを描いた (見せている間は Play の各部分の時間を測る)
    FrameGraph playGraph_, sceneGraph_;
    PlayTimes playTimes_;
    std::vector<ScriptRunner::ScriptTime> scriptTimes_;
    std::vector<SystemRunner::SystemTime> systemTimes_;
    std::optional<Vec2> sceneMouse_;
    std::optional<Vec2> pointer_;       // ... ゲーム画面の上 (ゲーム設定の 画面 の DIP)。nullopt: その上にない
    bool pointerWentDown_ = false, pointerWentUp_ = false;  // このフレームの左ボタンの押下 (PlayFrame)
    ui::TypedText typed_;     // ゲームビューが集めた、ゲーム UI の入力欄に打った文字 (次の PlayFrame)
    TextInputAsk textAsk_;    // 前のフレームでサービスが言った: 入力欄に打っている
    bool gameTyping_ = false; // その間、このパネルがウィンドウのキーのフォーカスを持っている
    std::vector<std::unique_ptr<ScreenDrawer>> screenDrawers_;  // ゲームビューの上 (ゲーム UI)
    std::map<std::string, std::unique_ptr<SceneView>> viewPanels_;  // プラグインのパネル (ViewPanelInfo)、id ごと
    bool screensAsked_ = false;
    std::vector<AssetRef> playScenes_;  // Play が切り替えられるシーン (始まったときの PlayScenes)
    std::vector<std::string> cameraTypes_;  // ゲームがそれを通して見る部品 (SceneCamera)
    // シーンの一覧 パネル: 選んだ行 (シーンの id。"" = まだ保存していない新しいシーン)、その名前変更。
    std::optional<AssetRef> sceneChosen_;
    bool renamingScene_ = false;
    std::string sceneNewName_;
    std::wstring sceneProblem_;
    // シーン パネル: そのツリー (ヒエラルキー のもの)、開くためにクリックした行、最後に見せたシーン。
    editor::tree::State sceneTree_;
    std::unique_ptr<SceneListSource> sceneSource_;
    std::optional<AssetRef> sceneToOpen_;
    std::optional<AssetRef> shownInList_;
    SystemRunner runner_;
    PlayServices services_;  // プラグインのもの (音 ...): ▶ から ■ まで
    double lastTick_ = 0;

    // Prefab: 最後に読んだ / 書いたファイル (パス -> 内容)
    std::unordered_map<std::string, json::Value> prefabs_;
    uint64_t dataVersion_ = 0;  // prefabs_ や、履歴を通さずに作ったシーンが変わるたびに増やす
    mutable std::unordered_map<int, json::Value> overridesCache_;  // root -> CachedOverrides
    mutable std::pair<uint64_t, uint64_t> overridesStamp_{~uint64_t{0}, ~uint64_t{0}};
    std::vector<std::string> prefabFiles_;
    double prefabFilesTime_ = 0;
};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    editor::SoftwareType t;
    t.id = "game";
    t.title = TrKey(L"ゲーム");
    t.create = [](editor::SoftwareHost& host) -> std::unique_ptr<editor::Software> {
        return std::make_unique<GameSoftware>(host);
    };
    registry.AddSoftware(std::move(t));
}

}  // namespace rm::game_production
