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
constexpr char kExportPanel[] = "export";
constexpr char kScenesPanel[] = "scenes";
constexpr size_t kMaxPrefabRows = 20;  // "Prefab を置く" rows in the 階層's menu
constexpr float kSpaceStripH = 34.f;   // the strip of spaces (3D / 2D) above the scene view

// The views of one space (a SceneViewInfo): the scene view, and the game view showing the game while it
// plays (the same kind of view).
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

// A change to the shape of the scene (create / delete / duplicate / move): the scene before and
// after. Field edits (name, switch) are reflect::SetFieldCommand like everywhere else.
class SceneCommand final : public ICommand {
public:
    SceneCommand(Scene& scene, json::Value before, json::Value after, std::string label)
        : scene_(scene), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {
        bytes_ = json::Write(before_, false).size() + json::Write(after_, false).size();  // once (the history's limit)
    }
    size_t Bytes() const override { return bytes_; }
    // The next id is kept: an id given out once is never given again (an entity made after an undo
    // never gets the id of one that was undone - references and saved views would point at it).
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

// A file an edit wrote (a prefab - Prefab に反映): undo writes back what was there, redo the new one.
// Put in the same entry as the scene's change, so undo never leaves the file and the scene apart.
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

// A change of the scene's shape kept as the entities it touched, before and after (SceneRecord) -
// not the whole scene twice. Undo / redo put back only those entities.
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

// An edit of the game's list of scenes (シーンの一覧: added, removed, moved, the first one).
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
// The scene as a whole, but for the next id (which undo never takes back: ids are not used again).
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

// Play: the scene is copied and the copy plays; stopping throws the copy away (the scene, the undo
// history and the unsaved mark never see it).
enum class PlayState { Stopped, Playing, Paused };

class GameSoftware final : public editor::Software, public tree::Source {
public:
    explicit GameSoftware(editor::SoftwareHost& host) : host_(host), scripts_(host) {
        // Component types come from other plugins (the connectors: "ゲーム制作 × 3D" offers Transform).
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
        // The scene's own values (重力, 空 ...): the インスペクター shows them when the scene is chosen.
        scene_.SetSettingTypes(SettingTypes(host.Registry()));
        for (const auto& s : host.Registry().Contributions<SceneSettingInfo>(kSceneSettingPoint)) {
            if (s.item->space) settingSpaces_[s.item->type().name] = s.item->space;
        }
        // How the editor shows them (the 階層's pictures, ローカル / ワールド): their plugins' editor parts.
        for (const auto& l : host.Registry().Contributions<ComponentLook>(kComponentLookPoint)) componentLooks_.push_back(l.item);
        for (const auto& l : host.Registry().Contributions<SceneSettingLook>(kSceneSettingLookPoint)) settingLooks_.push_back(l.item);
        // ... and so do the systems that move them while the game plays.
        runner_.SetSystems(RegisteredSystems(host.Registry()));
        // Systems read assets from the project's files (a mesh collider: the model, as the scene view reads it).
        runner_.SetAssets({nullptr, [this](const AssetRef& ref) { return host_.AssetFile(ref); }});
        cameraTypes_ = CameraTypes(host.Registry());
    }

    // The scripts' objects go before their DLL does (scripts_ is destroyed before the scenes).
    ~GameSoftware() override {
        playScene_.reset();
        scene_.Load(json::Value());
    }

    // --- Software ---------------------------------------------------------------------------
    std::vector<editor::dock::PanelType> Panels() override {
        std::vector<editor::dock::PanelType> panels{
            {kSceneView, TrKey(L"シーン"),
             [this](ui::Context& c, const Rect& r) {
                 // (measured for the デバッグ panel: how often, and how long it takes)
                 const double t0 = platform::NowSeconds();
                 DrawSceneView(c, r);
                 sceneMeter_.Frame(t0, (platform::NowSeconds() - t0) * 1000.0);
                 sceneGraph_.Push(t0, (platform::NowSeconds() - t0) * 1000.0);
                 sceneMouse_.reset();
                 if (c.IsHovered(r)) sceneMouse_ = Vec2{c.In().mouseX - r.x, c.In().mouseY - r.y};
             }},
            {kGameView, TrKey(L"ゲーム"),
             [this](ui::Context& c, const Rect& r) {
                 const double t0 = platform::NowSeconds();
                 DrawGameView(c, r);
                 gameMeter_.Frame(t0, (platform::NowSeconds() - t0) * 1000.0);
             }},
            {kExportPanel, TrKey(L"書き出し"), [this](ui::Context& c, const Rect& r) { DrawExportPanel(c, r); }},
            {kScenesPanel, TrKey(L"シーンの一覧"), [this](ui::Context& c, const Rect& r) { DrawScenesPanel(c, r); }},
        };
        // Panels the plugins add (ゲーム UI: the UI view) - not of a space.
        for (const auto& p : host_.Registry().Contributions<ViewPanelInfo>(kViewPanelPoint)) {
            const ViewPanelInfo* info = p.item;
            if (!info->create || !info->id || !*info->id) continue;
            panels.push_back({info->id, info->title ? info->title : L"", [this, info](ui::Context& c, const Rect& r) { DrawViewPanel(c, r, *info); }});
        }
        return panels;
    }

    // デバッグ: a window of its own (not docked in the editor): no Windows frame, shaped as a file folder with
    // five tabs (DebugPanel.h). Drawn by a timer - never frame after frame (that kept the editor's own
    // window from being drawn while the panel floated).
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
        spec.shape = [](float w, float h) { return DebugWindowShape(w, h); };
        spec.hit = [](float x, float y, float w, float h) { return HitDebugWindow(LayoutDebugWindow(w, h), x, y); };
        spec.draw = [this](ui::Context& c, float w, float h) {
            debugPanel_.DrawWindow(c, w, h, MakeDebugView(), [this] {
                if (debugWindow_) debugWindow_->Close();
            });
        };
        debugWindow_ = host_.OpenToolWindow(std::move(spec));
        if (!debugWindow_) log::Warn("デバッグの窓を作れませんでした");
    }

    // What the デバッグ window shows now.
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

    // A panel a plugin added: its view, made the first time it is drawn (its state kept like the scene
    // views': "viewPanels": {id: state}).
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
        // The plugins' panels (the UI view) are tabs next to the scene and the game.
        std::vector<std::string> views{kSceneView, kGameView};
        for (const auto& p : host_.Registry().Contributions<ViewPanelInfo>(kViewPanelPoint)) {
            if (p.item->create && p.item->id && *p.item->id) views.push_back(p.item->id);
        }
        auto top = l.MakeSplit(Axis::Horizontal, 0.17f, l.MakeLeaf({cp::kHierarchy, kScenesPanel}),
                               l.MakeSplit(Axis::Horizontal, 0.78f, l.MakeLeaf(views), l.MakeLeaf({cp::kInspector})));
        l.main = l.MakeSplit(Axis::Vertical, 0.72f, std::move(top), l.MakeLeaf({cp::kConsole, cp::kAssets, kExportPanel}));
        return l;
    }

    tree::Source* Hierarchy() override { return this; }

    // The view is kept as read, so a scene view not opened this time keeps its camera (and keys of a
    // newer version stay).
    // "inspectorModes": [[entity id, mode], ...] - how each entity's Transform is shown (switched by the user).
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
        // "sceneViews": {space: the view's state} - a space whose plugin is not here now keeps its own.
        if (!views_.empty()) {
            json::Value states = v.Get("sceneViews").IsObject() ? v.Get("sceneViews") : json::Value::MakeObject();
            for (const View& view : views_) {
                if (json::Value s = view.scene->SaveState(); !s.IsNull()) states.Set(view.info->space, std::move(s));
            }
            v.Set("sceneViews", std::move(states));
            v.Erase("sceneView");  // the one view before there were spaces: now in "sceneViews"
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
        std::sort(sorted.begin(), sorted.end());  // the same file for the same choices
        for (const auto& [id, mode] : sorted) {
            if (!scene_.Find(id)) continue;  // deleted since
            json::Value pair = json::Value::MakeArray();
            pair.Push(id);
            pair.Push(mode);
            modes.Push(std::move(pair));
        }
        v.Set("inspectorModes", std::move(modes));
        return v;
    }

    // The game's data in the .rmpf: the settings and the list of scenes; the scene shown is read from
    // its own file (SceneFile.h). A project of before has its scene inside: read from there, and split
    // off at the next save.
    void LoadData(const json::Value& data) override {
        // The scripts first: the scene's scripts are components of their types. (The global variables
        // before: the header of their names is written with the project.)
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
                scene_.Load(*old, &warnings);
                oldForm_ = true;
                log::Info("このプロジェクトのシーンは .rmpf の中にあります。次に保存するとき {} に分けます (前の .rmpf は {}{} に残します)", slot_.path,
                          PathToUtf8(host_.GetProject().file.filename()), kOldSceneBackupSuffix);
            } else {
                reflect::Load(sceneList_, data.Get("sceneList"), &sceneListUnknowns_, &warnings);
                ReadOpenScene(warnings);
            }
            if (scripts_.Building() && !warnings.empty()) {
                // The first build of a new project (or of sources newer than the DLL) is going on: its
                // scripts are parts it does not know yet. Told after it, only what is still unknown then
                // (ReloadScripts reads the scene again with the new types and tells its own).
                heldSceneWarnings_ = std::move(warnings);
                log::Info("シーン: スクリプトのビルドが終わってから、使えない部品が残っていないか確かめます");
            } else {
                for (const auto& w : warnings) log::Warn("シーン: {}", w);
            }
            RefreshPrefabsOnOpen();
            ++dataVersion_;  // (the scene was made again: no history entry tells it)
        }
        InspectSettings();
    }

    bool SaveData(json::Value& data, std::vector<editor::ProjectFileWrite>& files) override {
        // What the last save gave and was never taken on (Saved not called): that save failed part
        // way. Its scene file may be written or not - the one on disk is not known any more (written
        // again this time); a new scene whose .meta got written keeps that id and file.
        if (pending_) {
            std::error_code ec;
            const bool metaThere = slot_.guid.empty() && fs::exists(FullPath(pending_->slot.path + std::string(asset::kMetaSuffix)), ec);
            slot_ = SlotAfterFailedSave(slot_, pending_->slot, metaThere);
            if (metaThere) host_.AssetOf(FullPath(slot_.path));  // (its id known now: SlotNow finds it)
            log::Warn("前の保存が途中で失敗したので、シーンをもう一度書きます: {}", slot_.path);
        }
        pending_.reset();
        gave_ = false;
        // Nothing is written (and the plugin is not recorded as used) while everything is as a new
        // project has it - opening a project once must not tie it to this plugin.
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
        // A scene whose file could not be read is never written over: an edited one goes to a new file.
        if (sceneUnread_ && scene_.Empty()) return true;
        // What each placed prefab changed, worked out now against its prefab (kept in the file so a
        // changed prefab can be put under them again when the project is opened).
        Scene saved = scene_;
        UpdateOverrides(saved);
        const SceneSlot slot = SlotNow();
        ScenePlan plan = PlanSceneSave(saved.Save(), slot, asset::NewGuid());
        if (oldForm_) {
            // The .rmpf as it was (the scene inside), kept once next to it before it is written in the new form.
            const fs::path rmpf = host_.GetProject().file;
            fs::path backup = rmpf;
            backup += kOldSceneBackupSuffix;
            std::error_code ec;
            std::string err;
            if (!fs::exists(backup, ec)) {
                auto bytes = platform::ReadFileBytes(rmpf, &err);
                if (!bytes) {
                    // Never split without the .rmpf of before kept: saved as before (the scene inside,
                    // edits and all), split at a later save.
                    log::Error("前の .rmpf を控え ({}) に写せないので、シーンはまだ .rmpf の中に保存します (次の保存でもう一度分けます): {}",
                               PathToUtf8(backup.filename()), err);
                    data.Set("scene", saved.Save());
                    return true;
                }
                files.push_back({PathToUtf8(backup.filename()), std::move(*bytes)});
            }
        }
        for (FileWrite& f : plan.files) files.push_back({std::move(f.path), std::move(f.text)});
        // The list: the scene's id in it (in the place of the one whose file was gone), shown first.
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
        data.Erase("scene");  // (a project of before: now in the file)
        pending_ = Pending{plan.after, std::move(list)};
        return true;
    }

    // --- 書き出し (GameExport.h) -------------------------------------------------------------------
    bool CanExport() const override { return true; }

    // The exe's name: the one typed, else made from the game's name (shown in the field as it is).
    std::string ExeNameNow() const {
        return exportSettings_.exeName.empty() ? editor::exporter::ExeNameFrom(settings_.title) : exportSettings_.exeName;
    }
    // Where to: this PC's choice (the view, not project data); first <project>/Build/<exe name>.
    fs::path OutputNow() const {
        const std::string& o = view_.Get("export").Get("output").AsString();
        if (!o.empty()) return PathFromUtf8(o);
        return host_.GetProject().Root() / L"Build" / PathFromUtf8(ExeNameNow());
    }

    // What an export is made from: the project as it is now (the scene as edited, not only as saved).
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
        // The scenes of the list (the one shown as it is now), in the list's order; the game starts with
        // the list's 最初のシーン.
        Scene saved = scene_;
        UpdateOverrides(saved);
        ExportScenes scenes = ScenesForExport(sceneList_, AssetRef{slot_.guid}, SlotNow().path, SceneFileText(saved.Save()),
                                              [this](const AssetRef& ref) -> std::optional<std::pair<std::string, std::string>> {
                                                  const fs::path file = host_.AssetFile(ref);
                                                  std::string err;
                                                  auto bytes = file.empty() ? std::nullopt : platform::ReadFileBytes(file, &err);
                                                  if (!bytes) return std::nullopt;
                                                  // Its placed prefabs as their files are now (as when it is opened).
                                                  if (const auto doc = ReadSceneFile(*bytes, &err)) {
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
        // The plugins: as recorded, what the scenes use, the one drawing the game's space - then their
        // requirements and connectors.
        // Counted from what the game uses - not the project's whole record, which may name plugins of
        // other softwares (an illustration's): those never go into the game, and one that is missing
        // does not stop it.
        std::vector<PluginId> used;
        auto add = [&](const PluginId& id) {
            if (std::find(used.begin(), used.end(), id) == used.end()) used.push_back(id);
        };
        add(game_production::kPluginId.Id());
        for (const PluginId& id : UsedPlugins()) add(id);
        // ... and every other scene of the game (their components' plugins too).
        std::vector<std::string> types;
        for (const auto& s : in.scenes) {
            if (const auto doc = json::Parse(s.second)) ComponentTypesIn(*doc, types);
        }
        for (const PluginId& id : PluginsOffering(types, host_.Registry().Contributions<ComponentInfo>(kComponentPoint))) add(id);
        // ... and the plugins an asset the game uses needs (音: a sound a script plays, with no 音源 in any scene).
        const std::vector<ExportAsset> assets = CollectAssets(in);
        // ... and the components of every prefab the game may place (a script's rm::Spawn of one that no
        // scene places: read from its file - the prefabs read in this session are not all of them).
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
        // The record's plugins this engine does not have: only told (the game does not use them).
        if (auto rec = editor::ParsePluginIdList(host_.GetProject().doc.Get("plugins"), nullptr)) {
            const auto manifests = host_.PluginManifests();
            for (const PluginId& id : *rec) {
                const bool here = std::any_of(manifests.begin(), manifests.end(), [&](const editor::PluginManifest& m) { return m.id == id; });
                if (!here) log::Warn("書き出し: プロジェクトが記録しているプラグイン {} はこのエンジンにありませんが、ゲームは使わないので続けます", id.Text());
            }
        }
        return true;
    }

    // The plugins' bakers (ゲーム用の形に変える: 3D models, pictures) - statics, usable from any thread.
    std::vector<const ExportBaker*> Bakers() const {
        std::vector<const ExportBaker*> out;
        for (const auto& b : host_.Registry().Contributions<ExportBaker>(kExportBakePoint)) out.push_back(b.item);
        return out;
    }

    // The export check's command line: on this thread, the lines to the log.
    bool ExportNow(const fs::path& outDir, std::string* error) override {
        GameExportInput in;
        editor::exporter::Request req;
        if (!ExportInput(outDir, in, error)) return false;
        auto note = [](const std::string& line) { log::Info("書き出し: {}", line); };
        if (!MakeRequest(in, CollectAssets(in), Bakers(), req, note, error)) return false;
        return editor::exporter::Run(req, [&](float, const std::string& line) { note(line); }, error);
    }

    // The 書き出し button: the request made here (the data as it is now), the work on a worker.
    void StartExport() {
        {
            std::lock_guard lock(exportRun_->mtx);  // (the worker sets it under the lock)
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
        // What the editor knows is read here; the rest (baking, linking) on the worker - only files.
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

    // An edit of the panel goes where the value lives: the game's name to the game settings, the exe's
    // name to the project's export settings (both undone), where to into the view (this PC's).
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
        auto push = [&](const reflect::TypeDesc& type, std::string_view field, void* object, reflect::Unknowns* unknowns) {
            for (size_t i = 0; i < type.fields.size(); ++i) {
                if (type.fields[i].name != field) continue;
                json::Value before = reflect::SaveField(type.fields[i], object, unknowns);
                host_.History().Push(std::make_unique<reflect::SetFieldCommand>(type, reflect::FieldPath{i}, std::move(before), edit.after,
                                                                                [object, unknowns] { return reflect::EditTarget{object, unknowns}; },
                                                                                edit.label, edit.merge),
                                      false);
            }
        };
        if (name == "title") push(reflect::TypeOf<GameSettings>(), "title", &settings_, &unknowns_);
        if (name == "exeName") push(reflect::TypeOf<ExportSettings>(), "exeName", &exportSettings_, &exportUnknowns_);
    }

    void DrawExportPanel(ui::Context& c, const Rect& r) {
        auto& p = c.P();
        // The panel scrolls when it is low (the 書き出す button and the log stay reachable).
        float& contentH = c.FloatState(c.MakeId("export.contentH"), 300.f);
        const float off = editor::w::BeginScroll(c, "export.scroll", r, contentH);
        const Rect area = Rect{r.x, r.y - off, r.w, std::max(r.h, contentH)}.Inset(14, 10, 14, 10);
        const float top = area.y;
        ExportView v{settings_.title, ExeNameNow(), PathToUtf8(OutputNow())};
        const editor::inspector::Block b{&reflect::TypeOf<ExportView>(), &v, nullptr, {}, {}, {}};
        const Rect box{area.x - 12, area.y, area.w + 12, std::max(3 * 30.f + 10, exportInspector_.contentHeight)};
        if (auto edit = editor::inspector::Draw(c, exportInspector_, box, std::span(&b, 1))) ApplyExportEdit(*edit);
        float y = box.Bottom() + 4;
        if (editor::w::Button(c, "export_browse", {area.Right() - 160, y, 160, 26}, Tr(L"出力先を選ぶ…"))) {
            // In the output folder (not there yet: the nearest one above it; on a drive that is not there:
            // the project's folder).
            const fs::path start = platform::ExistingFolderAtOrAbove(OutputNow()).empty() ? host_.GetProject().Root() : OutputNow();
            host_.PickFolder(Tr(L"書き出すフォルダを選ぶ"), start, [this](fs::path folder) {
                json::Value e = view_.Get("export").IsObject() ? view_.Get("export") : json::Value::MakeObject();
                e.Set("output", PathToUtf8(folder));
                view_.Set("export", std::move(e));
            });
        }
        y += 32;
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextDim;
        // The icon: the game settings' (shown here, chosen there).
        const fs::path icon = settings_.icon.Empty() ? fs::path() : host_.AssetFile(settings_.icon);
        p.Text({area.x, y, area.w - 170, 26}, TrF(L"アイコン: {}", icon.empty() ? Tr(L"(なし。ゲームの設定の「アイコン」に PNG か ICO を選びます)") : icon.filename().native()), ts);
        if (editor::w::Button(c, "export_settings", {area.Right() - 160, y, 160, 26}, Tr(L"ゲームの設定を開く"))) InspectSettings();
        y += 34;
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
        y += 42;
        // The log of this export (scrolled to with the rest of the panel).
        ts.wrap = false;
        for (size_t i = 0; i < run.lines.size(); ++i) {
            const bool last = i + 1 == run.lines.size();
            ui::TextStyle ls = ts;
            if (last && run.ok == false) ls.color = theme::Danger;
            p.Text({area.x, y, area.w, 20}, Utf8ToWide(run.lines[i]), ls);
            y += 20;
        }
        if (running) c.RequestAnimation();
        contentH = y - top + 20;
        editor::w::EndScroll(c, "export.scroll", r, contentH);
    }

    // The plugins offering the components of the scene and of the prefabs it places (recorded in the
    // .rmpf: an exported game links them).
    std::vector<PluginId> UsedPlugins() const override {
        std::vector<std::string> types;
        ComponentTypesIn(scene_.Save(), types);
        for (const auto& [guid, prefab] : prefabs_) ComponentTypesIn(prefab, types);
        return PluginsOffering(types, host_.Registry().Contributions<ComponentInfo>(kComponentPoint));
    }

    void Saved() override {
        if (gave_) hadData_ = true;  // (in the .rmpf now: a save that empties it again writes that too)
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
        if (newFile) host_.AssetOf(FullPath(slot_.path));  // its id is known now (without waiting for a look)
    }

    // --- Play --------------------------------------------------------------------------------
    void Tick(ui::Context& c) override {
        // Scripts built again: swapped here, between frames (never while playing: at Stop).
        scripts_.SetGlobals(settings_.globals);  // (an edit of the list: the header and a build, a second later)
        scripts_.Poll(c);
        if (debugWindow_ && !debugWindow_->IsOpen()) debugWindow_.reset();  // (closed by its × / Alt+F4)
        // (while playing: at Stop, or now - the game settings' Play 中のビルド)
        if (auto dll = scripts_.TakeReady(Running() && settings_.scriptReload == ScriptReload::AtStop)) ReloadScripts(*dll);
        if (!heldSceneWarnings_.empty() && !scripts_.Building()) {
            // The build failed (no new DLL): what the scene could not use is told now.
            for (const auto& w : heldSceneWarnings_) log::Warn("シーン: {}", w);
            heldSceneWarnings_.clear();
        }
        if (c.Focused() == 0) {
            if (c.KeyPressed(ui::Key::P, true)) Running() ? StopPlay() : StartPlay(c);
            if (c.KeyPressed(ui::Key::P, true, true) && Running()) TogglePause(c);
        }
        scripts_.mouseInGame = [this] { return mouseInGame_; };
        if (play_ == PlayState::Playing && playScene_) {
            // The time since the last frame (at most a quarter of a second: a frame that took long -
            // a window dragged, the software switched - does not make the game jump).
            const double dt = std::clamp(c.Time() - lastTick_, 0.0, 0.25);
            lastTick_ = c.Time();
            // The left button's presses of this frame (the window's messages: a click shorter than a frame
            // too) - a press only over the game view.
            pointerWentDown_ = mouseInGame_ && c.In().pressed[0];
            pointerWentUp_ = c.In().released[0];
            const double t0 = platform::NowSeconds();
            AdvancePlay(dt);
            updateMeter_.Frame(t0, (platform::NowSeconds() - t0) * 1000.0);
            playGraph_.Push(t0, (platform::NowSeconds() - t0) * 1000.0);
            mouseInGame_ = false;  // (the game view says it again when it is drawn)
            c.RequestAnimation();
        }
    }

    // ▶ ⏸ ⏭ ■ in the middle of the tab row, and what is going on next to them.
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
        // デバッグ: its window, at the right end of the bar.
        constexpr float kDebugW = 92;
        const bool debugOpen = debugWindow_ && debugWindow_->IsOpen();
        const Rect debugButton{r.Right() - kDebugW - 8, y, kDebugW, 26};
        if (debugButton.x > x + 8 &&
            editor::w::Button(c, "debug_window", debugButton, Tr(L"デバッグ"), debugOpen ? editor::w::ButtonKind::Primary : editor::w::ButtonKind::Secondary)) {
            ToggleDebugWindow();
        }
        if (debugButton.x - x > 80) {
            ui::TextStyle ts;
            ts.size = 12;
            ts.color = theme::Text;
            std::wstring text;
            if (running) {
                text = play_ == PlayState::Paused ? Tr(L"一時停止中 — ■ で止めると Play の前に戻ります")
                                                  : Tr(L"Play 中 — インスペクターで変えた値も、■ で止めると Play の前に戻ります");
            }
            // Scripts built while playing: used from Stop.
            if (running && scripts_.WaitingForStop()) text = Tr(L"スクリプトをビルドしました — Stop で反映されます");
            if (scripts_.Building()) text += (text.empty() ? L"" : L"   ") + Tr(L"スクリプトをビルドしています…");
            if (!text.empty()) c.P().Text({x + 8, r.y, debugButton.x - x - 16, r.h}, text, ts);
        }
    }

    bool Running() const override { return play_ != PlayState::Stopped; }

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

    // The top row: the open scene (its name); choosing it shows the scene's own values (InspectScene).
    std::optional<TopRow> Top() const override {
        std::string name = SceneNameOf(slot_.path);
        return TopRow{name.empty() ? Tr(L"シーン") : Utf8ToWide(name), editor::w::Icon::Scene, inspectingScene_};
    }
    void TopClicked() override { InspectScene(); }

    std::wstring EmptyText() const override {
        return Tr(L"エンティティはまだありません。\nここを右クリックして「追加」から作れます。");
    }

    bool CanRename(tree::ItemId id) const override { return !Running() && scene_.Find(static_cast<int>(id)) != nullptr; }

    void Rename(tree::ItemId id, const std::string& name) override {
        if (Running()) return;
        // The same field and the same command as editing 名前 in the inspector.
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
            // Play: the scene is not changed (the copy plays). The rows stay, greyed, under a note.
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

        // 追加 ▸: an empty entity, then what the plugins make, in their folders (3D オブジェクト ▸ 箱 ...).
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
            } else if (!line) {  // (the line between the empty ones and the rest)
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

        // The clipboard: entities as JSON text (the file's form), so another project can take them.
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
            // Right after the (last) selected entity, as its sibling; nothing selected: the end of the top level.
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

    // Right after the (last) selected entity, as its sibling; nothing selected: the end of the top level.
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
    // The scene that is shown: the copy that plays, or the scene itself.
    Scene& Shown() { return playScene_ ? *playScene_ : scene_; }
    const Scene& Shown() const { return playScene_ ? *playScene_ : scene_; }

    void StartPlay(ui::Context& c) {
        if (Running()) return;
        // Not in the middle of a drag (a gizmo, a row): the rest of it would go to the scene and the
        // history under the copy that plays.
        const ui::Input& in = c.In();
        if (in.down[0] || in.down[1] || in.down[2]) {
            log::Info("ドラッグ中は Play を始めません (ボタンを離してから)");
            return;
        }
        playScene_ = std::make_unique<Scene>(scene_);  // the copy that plays (components are deep copies)
        runner_.Reset();
        runner_.SetCamera(SceneCamera(*playScene_, cameraTypes_, settings_.startCamera.id));  // the systems may switch it (SystemContext::camera)
        scripts_.Runner().Reset();
        scripts_.World().time = 0;
        scripts_.World().unscaledTime = 0;
        scripts_.World().frame = 0;
        scripts_.World().timeScale = 1;
        scripts_.World().layers = &settings_.layers;
        debug_.Clear();
        scripts_.World().debug = &debug_;  // the editor shows rm::Debug's lines (an exported game has none)
        // The scenes Game::LoadScene can switch to: the game's (the list, and this one when it is not in it).
        playScenes_ = GameScenes(sceneList_, AssetRef{slot_.guid});
        ScriptWorld& world = scripts_.World();
        world.sceneNames.clear();
        world.sceneIds.clear();
        world.sceneIndex = -1;
        world.sceneAsked = -1;
        world.quitAsked = false;
        scripts_.StartInput();
        for (size_t i = 0; i < playScenes_.size(); ++i) {
            world.sceneNames.push_back(SceneTitle(playScenes_[i]));
            world.sceneIds.push_back(playScenes_[i].guid);
            if (playScenes_[i] == AssetRef{slot_.guid}) world.sceneIndex = static_cast<int>(i);
        }
        scripts_.World().actions = &settings_.input;  // (edited while playing: read as it is each frame)
        scripts_.World().prefab = [this](const AssetRef& r) { return Cached(r); };  // rm::Spawn(prefab): the files as last read
        settingsBeforePlay_ = reflect::Save(settings_, &unknowns_);  // (changed in Play's inspector: back at ■)
        ResetGlobals(true);  // every global variable at its 初めの値; rm::Save into Library/Saves
        {
            // The player's bindings saved by a script (rm::Input::SaveBindings), as the game reads them.
            std::string why;
            if (!world.LoadBindings(&why) && !why.empty()) log::Warn("入力の割り当て (input.json) を読めません: {}", why);
        }
        kept_.clear();       // (この値を残す: from this Play only)
        keptLeft_.clear();
        StartServices();     // 音 ... (the plugins' PlayServices): until ■
        play_ = PlayState::Playing;
        lastTick_ = c.Time();
        EnsureViews();  // the game view sees through the game settings' 最初のカメラ
        log::Info("▶ Play (システム {} 個)。止めると Play の前のシーンに戻ります", runner_.Systems().size());
        c.RequestAnimation();
        host_.Redraw();
    }

    // One frame of the systems; a camera they switched to is told on the console.
    void AdvancePlay(double dt) {
        const uint64_t before = runner_.Camera();
        // The scripts first (input -> scripts' Tick -> fixed steps ...); they may switch the camera too.
        ScriptWorld& world = scripts_.World();
        world.camera = runner_.Camera();
        // The game's time: the real seconds times the time scale (rm::SetTimeScale); the game UI keeps the real ones.
        const double realDt = dt;
        dt *= world.timeScale;
        world.unscaledDt = static_cast<float>(realDt);
        world.keyboardPlayer = std::clamp(settings_.keyboardPlayer - 1, 0, kPlayers - 1);  // (edited while playing: read each frame)
        world.SetPointer(pointer_, GameScreen());
        // (the デバッグ panel shown: each part timed - the scripts by type, the systems one by one)
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
        debug_.BeginFrame(dt);  // last frame's lines go (and the ones whose seconds are up), then the scripts draw this one's
        scripts_.Runner().Frame(*playScene_, world, scripts_.Module(), settings_.scriptOrder, static_cast<float>(dt));
        lap(times.scripts);
        runner_.SetCamera(world.camera);
        runner_.Advance(*playScene_, dt, settings_.fixedTimestep);
        lap(times.systems);
        // What the systems' fixed steps tell the scripts (the physics' contacts ...).
        scripts_.Runner().Deliver(*playScene_, world, scripts_.Module(), settings_.scriptOrder, runner_.Hooks());
        lap(times.hooks);
        // The services (音, the game UI ...): the game screen as the game settings' 画面, the mouse over the
        // game view; the hooks they ask for (a button's OnClick) go to the scripts now.
        std::vector<HookCall> asked;
        PlayFrame pf{*playScene_, runner_.Camera(), static_cast<float>(realDt)};
        pf.screen = GameScreen();
        pf.pointer = pointer_;
        pf.pointerWentDown = pointerWentDown_;
        pf.pointerWentUp = pointerWentUp_;
        pf.input = &world.inputNow;
        pf.inputBefore = &world.inputBefore;
        pf.actions = &world.Actions();  // (the player's bindings)
        pf.hooks = &asked;
        pf.world = &world;
        services_.Frame(pf);
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
            // ゲームを終わる: the game would close - Play stops (as ■).
            log::Info("ゲームを終わる → Play を止めます (書き出したゲームではウィンドウが閉じます)");
            StopPlay();
            return;
        }
        // A scene the scripts asked for (Game::LoadScene): switched between frames, inside the copy
        // that plays - the scene being edited stays (■ comes back to it).
        const std::string from = world.sceneIndex >= 0 ? world.sceneNames[static_cast<size_t>(world.sceneIndex)] : std::string();
        std::string err;
        // この値を残す of the scene from before Play: their values as it leaves (its entities' ids are
        // another scene's after the switch).
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

    // A scene of the game read for Play (Game::LoadScene): the one open as it is now, the others from
    // their files (as saved).
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
        const auto scene = bytes ? ReadSceneFile(*bytes, &err) : std::nullopt;
        if (!scene) {
            if (error) *error = std::format("{}: {}", SceneTitle(ref), file.empty() ? TrU8("ファイルが Assets に見つかりません") : err);
            return false;
        }
        reflect::Warnings warnings;
        into.Load(*scene, &warnings);
        reflect::Warnings prefabWarnings;
        RefreshPrefabsIn(into, prefabWarnings);  // (its prefabs as their files are now, as when it is opened)
        for (const auto& w : warnings) log::Warn("シーン: {}", w);
        for (const auto& w : prefabWarnings) log::Warn("Prefab: {}", w);
        return true;
    }

    void StopPlay() {
        if (!Running()) return;
        log::Info("■ 停止 ({:.2f} 秒・{} コマ)。シーンは Play の前に戻りました", runner_.Time(), runner_.Frames());
        const auto kept = TakeKept();  // (as they were shown: before the scripts' EndPlay)
        scripts_.Runner().Stop(*playScene_, scripts_.World(), scripts_.Module(), settings_.scriptOrder);  // EndPlay
        scripts_.World().services = nullptr;
        scripts_.StopInput();  // (every pad still)
        scriptTimes_.clear();
        systemTimes_.clear();
        scripts_.World().playerActions.reset();
        scripts_.World().timeScale = 1;
        services_.Stop();    // every sound stops (■: nothing of the game is heard after it)
        playScene_.reset();  // (scripts built while playing are taken at the next frame: on the scene from before Play)
        // The game settings as they were before Play (the inspector changes them in place while playing).
        if (!settingsBeforePlay_.IsNull()) {
            if (reflect::Save(settings_, &unknowns_) != settingsBeforePlay_) {
                reflect::Load(settings_, settingsBeforePlay_, &unknowns_);
                log::Info("Play 中に変えたゲームの設定を、Play の前に戻しました");
            }
            settingsBeforePlay_ = json::Value();
        }
        debug_.Clear();      // Stop: every debug line goes
        ResetGlobals(false);  // as before Play (what was saved stays in Library/Saves)
        play_ = PlayState::Stopped;
        KeepValues(kept);  // この値を残す: into the scene from before Play (one undo entry)
        host_.SelectInTree({});  // (the selection may name entities of a scene switched to)
        host_.Redraw();
    }

    // The plugins' PlayServices for this Play: assets from their files (as they are now) with their
    // .meta's import settings.
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

    // The editor's saves (Library/Saves/save_*.json) to the Recycle Bin.
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

    // The game's global variables back to their 初めの値 (Play and Stop); a 初めの値 that does not fit
    // its type is told at Play.
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
        services_.SetPaused(play_ == PlayState::Paused);  // (sounds keep their place)
        lastTick_ = c.Time();  // the paused time does not count
        c.RequestAnimation();
    }

    // One frame (1 / 目標 FPS seconds), then paused.
    void StepFrame(ui::Context& c) {
        if (!Running() || !playScene_) return;
        play_ = PlayState::Paused;
        services_.SetPaused(true);
        AdvancePlay(1.0 / std::max(1, settings_.targetFps));
        lastTick_ = c.Time();
        host_.Redraw();
    }

    // The views come from the plugins that know a space (ゲーム制作 × 3D / × 2D): asked once. Each
    // space has a scene view and a game view of its own (each keeps its camera).
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

    // The state kept for a view; before there were spaces it was one "sceneView" (the default view's).
    json::Value ViewState(const View& v) const {
        const json::Value& states = view_.Get("sceneViews");
        if (states.IsObject() && !states.Get(v.info->space).IsNull()) return states.Get(v.info->space);
        if (!views_.empty() && &v == &views_.front()) return view_.Get("sceneView");
        return {};
    }

    // The game's space: the view the game chose, or the default (the first) when it chose none or one
    // whose plugin is not here now. Null when no plugin brings a view.
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

    // The strip above the scene view when more than one space is offered: switching is an edit of the
    // game (GameSettings::space - undo, unsaved): the game view and new entities follow it.
    void DrawSpaceStrip(ui::Context& c, const Rect& strip) {
        editor::w::DrawBar(c, strip, theme::Bg);
        std::vector<std::wstring> labels;
        for (const View& v : views_) labels.push_back(v.info->label ? Tr(v.info->label) : Utf8ToWide(v.info->space));
        const std::vector<std::wstring_view> views(labels.begin(), labels.end());
        const std::wstring tip = Running() ? Tr(L"Play 中は切り替えられません")
                                           : Tr(L"このゲームの空間。新しいエンティティに付く Transform と、ゲームビューもこれに合わせます");
        const std::vector<std::wstring_view> tips(labels.size(), tip);
        const View* now = Current();
        const int selected = static_cast<int>(now - views_.data());
        const float w = std::min(strip.w - 16, 64.f * static_cast<float>(labels.size()));
        const Rect at{strip.x + 8, strip.y + 4, w, strip.h - 8};
        const int i = editor::w::Segmented(c, "space", at, views, selected, tips);
        if (i >= 0 && i != selected && !Running()) ChooseSpace(views_[static_cast<size_t>(i)].info->space);
    }

    // Above the game view while it is stopped: that it is, and the game settings (最初のカメラ ...).
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
        // The scripts: build now, write them in Visual Studio.
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

    // The component types: the plugins' and the scripts' (a script may be attached several times).
    void UseScriptTypes() {
        std::vector<const reflect::TypeDesc*> types = baseTypes_, multiple = baseMultiple_;
        for (const reflect::TypeDesc* t : scripts_.Module().Types()) {
            types.push_back(t);
            multiple.push_back(t);
        }
        scene_.SetComponentTypes(std::move(types), std::move(multiple));
    }

    // Hot reload (a frame boundary): the values written out by name, the old DLL's objects gone, the
    // new DLL loaded, read back - a new field gets its default, a field that went is kept in the file,
    // a script that went stays as a missing one with its values (Scene: unknown component). While
    // playing (Play 中のビルド = すぐ入れ替える) the copy that plays the same way: its scripts that had
    // started stay started and get OnReloaded instead of BeginPlay.
    void ReloadScripts(const std::filesystem::path& dll) {
        std::unique_ptr<ScriptModule> next = scripts_.LoadModule(dll);
        if (!next) return;  // (told) the old DLL goes on
        // The coroutines' frames are the old DLL's: they stop before it goes (OnReloaded may start them again).
        if (!scripts_.World().coroutines.empty()) {
            log::Info("スクリプトを入れ替えたので、動いていたコルーチン {} 個を止めました", scripts_.World().coroutines.size());
            scripts_.World().ClearCoroutines();
        }
        const json::Value saved = scene_.Save();
        scene_.SetComponentTypes(baseTypes_, baseMultiple_);
        scene_.Load(saved);  // the old scripts' objects gone (kept as read)
        std::vector<ScriptRunner::RunState> running;
        json::Value playing;
        if (playScene_) {
            running = scripts_.Runner().Remember(*playScene_);
            playing = playScene_->Save();
            playScene_->SetComponentTypes(baseTypes_, baseMultiple_);
            playScene_->Load(playing);
        }
        // The history's edits point at the old types (their code is in the old DLL): they go too.
        if (host_.History().UndoCount() + host_.History().RedoCount() > 0) {
            host_.History().Clear();
            log::Info("スクリプトを読み直したので、元に戻す (Undo) の履歴を消しました");
        }
        scriptTimes_.clear();  // (they point at the old DLL's types)
        scripts_.Swap(std::move(next));
        UseScriptTypes();
        reflect::Warnings warnings;
        scene_.Load(saved, &warnings);
        heldSceneWarnings_.clear();  // (read again: what is told now replaces what was held at opening)
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
                                     [this] { return reflect::EditTarget{&settings_, &unknowns_}; }, "シーンの空間を切り替え"),
                                 false);
            log::Info("シーンの空間を「{}」にしました (新しいエンティティの Transform とゲームビューも)", space);
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
        h.camera = Running() ? runner_.Camera() : SceneCamera(scene_, cameraTypes_, settings_.startCamera.id);  // in Play: as the systems left it
        h.aspect = static_cast<float>(std::max(1, settings_.display.width)) / static_cast<float>(std::max(1, settings_.display.height));
        h.openSettings = [this] { InspectSettings(); };
        h.settings = &settings_;
        h.debug = Running() ? &debug_ : nullptr;
        h.editScene = [this](std::vector<int> touched, std::string label, std::function<std::vector<int>(Scene&)> change) {
            std::vector<int> select;
            EditTouching(std::move(touched), std::move(label), [&] { select = change(scene_); });
            if (!select.empty()) host_.SelectInTree(std::vector<tree::ItemId>(select.begin(), select.end()));
        };
        return h;
    }

    // The scene view comes from a plugin that knows a space (ゲーム制作 × 3D / × 2D); without one, a note.
    void DrawSceneView(ui::Context& c, const Rect& panel) {
        if (!Current()) {
            Placeholder(c, panel, Tr(L"シーンビュー"),
                        Tr(L"3D 空間 / 2D 空間のプラグインが入ると、ここにシーンが映り、エンティティを置いて動かせます。"));
            return;
        }
        Rect r = panel;
        if (views_.size() >= 2 && r.h > 120) DrawSpaceStrip(c, ui::TakeTop(r, kSpaceStripH));
        SceneViewHost h = ViewHost(false);
        Current()->scene->Draw(c, r, h);  // (the strip may have just switched it)
        if (play_ == PlayState::Playing) c.RequestAnimation();  // this panel may be in a window of its own
    }

    // The game as it sees itself: while playing the copy that plays, otherwise the scene as it is (a
    // strip on top says so, with the way to the settings).
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
        mouseInGame_ = c.IsHovered(r);  // (the game's mouse buttons: only over here - read by the next frame)
        SceneViewHost h = ViewHost(true);
        view->game->Draw(c, r, h);
        // The other spaces over it, in their order (2D over a 3D game, when it has a camera to show it).
        for (View& other : views_) {
            if (&other != view) other.game->DrawLayer(c, r, h);
        }
        // Over everything: the game UI ... - on the game screen's rectangle, as the game settings' 画面.
        const Rect game = GameScreenRect(r, h.aspect);
        const Vec2 screen = GameScreen();
        const ui::Input& in = c.In();
        pointer_.reset();
        if (mouseInGame_ && game.Contains(in.mouseX, in.mouseY) && game.w > 0 && game.h > 0) {
            pointer_ = Vec2{(in.mouseX - game.x) / game.w * screen.x, (in.mouseY - game.y) / game.h * screen.y};
        }
        DrawScreen(c, game, screen);
        if (play_ == PlayState::Playing) c.RequestAnimation();
    }

    // The game's screen as the game settings' 画面 (DIPs).
    Vec2 GameScreen() const {
        return {static_cast<float>(std::max(1, settings_.display.width)), static_cast<float>(std::max(1, settings_.display.height))};
    }

    // What the plugins draw over the whole game screen (ScreenDrawInfo: the game UI), into `game`.
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

    // --- Inspector -----------------------------------------------------------------------------
    // 外す in Play: the copy's component goes at the end of the next frame, a script's EndPlay first (as
    // rm::Entity::Remove asks). Gone with the copy at ■ anyway.
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

    // A note on top of the inspector while playing (its blocks are shown, not edited).
    static editor::InspectBlock PlayNote() {
        return {nullptr, {}, {}, Tr(L"Play 中です。ここで変えた値は動いているゲームにすぐ効きますが、保存されず、■ で止めると Play の前に戻ります (元に戻す の履歴にも入りません)。欄を右クリック →「この値を残す」で、止めたときの値を Play の前のシーンへ書き戻します。"), {}};
    }

    // Play: a right click on a field → この値を残す (design BluePrint 20-b 3). The field is remembered by
    // entity, component (its type and place; "" = the entity's own fields) and path; at Stop its value
    // then is written into the scene from before Play (KeepValues).
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

    // The scene playing is the one from before Play (not one Game::LoadScene switched to): only its
    // entities are the edited scene's.
    bool PlayingOwnScene() {
        const int i = scripts_.World().sceneIndex;
        return i < 0 || static_cast<size_t>(i) >= playScenes_.size() || playScenes_[static_cast<size_t>(i)] == AssetRef{slot_.guid};
    }
    // The kept values as they are now in the copy that plays.
    std::vector<std::pair<Kept, json::Value>> ReadKept() {
        std::vector<std::pair<Kept, json::Value>> out;
        for (const Kept& k : kept_) {
            if (auto v = ReadSceneField(*playScene_, k.field)) out.push_back({k, std::move(*v)});
        }
        return out;
    }
    // At Stop: the kept values - as they were when the scene from before Play was left, then as they
    // are in the copy that plays (before its EndPlay) when it is that scene ...
    std::vector<std::pair<Kept, json::Value>> TakeKept() {
        std::vector<std::pair<Kept, json::Value>> out = std::exchange(keptLeft_, {});
        if (PlayingOwnScene()) {
            for (auto& kv : ReadKept()) out.push_back(std::move(kv));
        }
        kept_.clear();
        return out;
    }
    // ... written into the scene from before Play: one undo entry (unsaved). One the scene does not have
    // (made while playing) is told.
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

    // The scene's own values (SceneSettingInfo: 物理's 重力, 3D's 空 ...), one block each - those of
    // another space than the game's left out. In Play: the copy that plays, shown, not edited.
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
                if (!c.Known()) continue;  // its plugin is not here: kept in the file as it was
                if (const auto it = settingSpaces_.find(c.type); it != settingSpaces_.end() && !space.empty() && it->second != space) continue;
                const std::string type = c.type;
                editor::InspectBlock b{c.value.Type(), [this, type, play] {
                                           Component* s = (play ? Shown() : scene_).FindSetting(type);
                                           return s && s->Known() ? reflect::EditTarget{s->value.Get(), &s->unknowns} : reflect::EditTarget{};
                                       },
                                       Utf8ToWide(TrU8(c.value.Type()->label)), {}, {}};
                b.direct = play;  // (Play: the copy that plays, changed in place)
                if (const SceneSettingLook* look = SettingLookOf(type); look && look->menu) {
                    SceneSettingMenuHost mh;
                    mh.host = &host_;
                    mh.play = play;
                    mh.edit = [this, type](std::string label, std::function<void(void*)> change) {
                        Edit(std::move(label), [&] {
                            if (Component* s = scene_.FindSetting(type); s && s->Known()) change(s->value.Get());
                        });
                    };
                    b.menu = look->menu(mh);
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

    void InspectSettings() {
        inspectingEntities_ = false;
        inspectingScene_ = false;
        editor::Inspectable in;
        in.title = Tr(L"ゲームの設定");
        in.blocks = [this] {
            std::vector<editor::InspectBlock> blocks;
            if (Running()) blocks.push_back(PlayNote());
            editor::InspectBlock b{&reflect::TypeOf<GameSettings>(), [this] { return reflect::EditTarget{&settings_, &unknowns_}; }, {}, {}, {}};
            // Play: changed in place (the game reads them at once); ■ puts them back as they were (settingsBeforePlay_).
            b.direct = Running();
            blocks.push_back(std::move(b));
            // Where rm::Save writes, and taking those saves away (to the Recycle Bin).
            editor::w::Action clear;
            clear.label = Tr(L"セーブを消す (Library/Saves)");
            clear.run = [this] { editor::PostToUi([this] { DeleteSaves(); }); };
            blocks.push_back({nullptr, {}, Tr(L"セーブ"),
                              Tr(L"Play 中の rm::Save は、このプロジェクトの Library/Saves に書きます (書き出したゲームは %LOCALAPPDATA% の「exe の名前/Saves」)。⋮ の「セーブを消す」で Play のセーブを全部ごみ箱へ移します"),
                              {clear}});
            return blocks;
        };
        host_.Inspect(std::move(in));
        host_.SelectInTree({});
    }

    // Where an entity is now (the list may have been rebuilt by undo since): found by id each time.
    // `play`: in the copy that plays (shown while playing; never edited).
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

    // The component `type` of the entity and of its parents that have it: the top parent's first.
    // Empty when the entity or its component is gone.
    static std::vector<const void*> Chain(const Scene& scene, int id, const std::string& type) {
        std::vector<const void*> chain;
        const SceneEntity* self = scene.Find(id);
        const Component* own = self ? self->FindComponent(type) : nullptr;
        if (!own || !own->Known()) return chain;
        size_t steps = 0;  // a broken file could make a ring of parents
        for (const SceneEntity* e = self; e && steps++ <= scene.Entities().size(); e = scene.Find(e->data.parent)) {
            const Component* c = e->FindComponent(type);
            if (c && c->Known()) chain.push_back(c->value.Get());
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    // The values a component's plugin works out through the parents: worked out again every time the
    // inspector asks, into a value of their own (edits of it go through EditDerived).
    std::function<reflect::EditTarget()> Derived(int id, std::string type, const ComponentLook* info, bool play) {
        auto value = std::make_shared<reflect::Object>(info->derivedType());
        return [this, id, type = std::move(type), info, value, play] {
            const std::vector<const void*> chain = Chain(play ? Shown() : scene_, id, type);
            if (chain.empty()) return reflect::EditTarget{};  // gone (undo)
            info->derive(chain, value->Get());
            return reflect::EditTarget{value->Get(), nullptr};
        };
    }

    // A derived value typed in the inspector: the component's plugin works out the component that
    // gives it; the fields that change are edits of the component (one undo entry; typing one field
    // joins into one entry).
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
        if (play) {  // (the copy that plays: in place, kept nowhere)
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

    // A field typed in the inspector of a component whose plugin keeps its fields together
    // (ComponentLook::adjust): the field as typed, then what the plugin changed with it - one undo entry
    // (typing into one field still joins into one entry while the plugin changes nothing else).
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
        if (play) {  // (the copy that plays: the field and what the plugin changed with it, in place)
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

    // How a child's component with derived values is shown: 0 = its own (as it starts), 1 = derived.
    // Once switched it stays (kept in UserSettings).
    int ModeOf(const SceneEntity& e) const {
        const auto it = modes_.find(e.data.id);
        return it != modes_.end() ? it->second : 0;
    }

    // A line under the entity's header when it is part of a placed prefab.
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
        // A prefab nested in another placed prefab: say which.
        if (const int around = r->prefab.outer > 0 ? scene_.PrefabRootOf(r->data.parent) : 0) {
            return TrF(L"Prefab「{}」を置いた物です ({})。Prefab「{}」の中に入っています。Prefab と違う所: {} 個 (名前と置き場所を含む)", name,
                       where, Utf8ToWide(PrefabTitle(scene_.Find(around)->prefab.asset)), n);
        }
        return TrF(L"Prefab「{}」を置いた物です ({})。Prefab と違う所: {} 個 (名前と置き場所を含む)", name, where, n);
    }

    // The folder a component type is listed in by 部品を足す: its plugin's (ComponentInfo::folder, each
    // part translated), "スクリプト" for a script, else "その他".
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

    // Where a type's folder comes in 部品を足す: 3D, 2D, UI, 物理, 音, エフェクト, then other plugins'
    // folders, スクリプト, その他 (the list is sorted by this; the folders come in the order of their first row).
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

    // The component types in 部品を足す's order (FolderRank; in a folder, as the plugins give them).
    std::vector<const reflect::TypeDesc*> TypesForAdding() const {
        std::vector<const reflect::TypeDesc*> types = scene_.ComponentTypes();
        std::stable_sort(types.begin(), types.end(), [this](const reflect::TypeDesc* a, const reflect::TypeDesc* b) { return FolderRank(*a) < FolderRank(*b); });
        return types;
    }

    // The inspector of one entity: the entity itself, then its components (each with ⋮ → 外す), and
    // "部品を足す" under them. Asked again every time it is drawn (undo may add or take components;
    // Play shows the copy that plays).
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
                // Several of one type: numbered (the block's title is also what keeps its fields apart).
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
                // Its place among the entity's components (the order they are shown - and run).
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
                    // A script: where it runs among the others (the game settings' スクリプトの実行順).
                    std::wstring note;
                    if (c.value.Type()->kind == reflect::TypeKind::Script) note = TrF(L"スクリプト ・ 実行順 {}", ScriptOrderOf(c.type, settings_.scriptOrder));
                    editor::InspectBlock b{c.value.Type(), ResolveComponent(id, c.type, nth, play), Utf8ToWide(TrU8(label)) + number, note, menu};
                    b.direct = play;
                    if (play) b.fieldMenu = KeepMenu(id, type, nth);
                    // A child: ローカル / ワールド (or whatever the plugin calls them), one block shown either way.
                    // At the top level both ways are the same numbers, so there is no switch.
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
            // (Play: added to the copy that plays - its script starts at the next frame; gone at ■)
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

    // Several entities at once: the entity's own values and the components every one of them has
    // (the first of each type), shown from the last selected; a change goes to all ("—" where they
    // differ). 外す / 部品を足す work on all of them.
    editor::Inspectable ManyInspectable(std::vector<int> ids) {
        editor::Inspectable in;
        in.title = TrF(L"{} 個のエンティティ", ids.size());
        in.blocks = [this, ids] {
            std::vector<editor::InspectBlock> blocks;
            const bool play = Running();
            const Scene& shown = Shown();
            std::vector<int> here;  // the ones that are still there (undo)
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
                // Offered while at least one of them lacks it (or may have more); added where it can be.
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

    // --- シーンの一覧 (the game's scenes) ---------------------------------------------------------
    // A scene's name for the screen and for Game::LoadScene: its file's (the new one not saved yet: the
    // file it will be).
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

    // Before the scene shown changes: what is not saved is saved (or not) as the user says, then `go`.
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

    // The scene shown becomes another: `ref` read from its file (empty: a new scene, `newPath`). The
    // history goes (its entries are edits of the scene that was shown).
    void ShowScene(const AssetRef& ref, const std::string& newPath = {}) {
        scene_.Load(json::Value());
        slot_ = {"", newPath.empty() ? NewSceneFile() : newPath, 0};
        sceneUnread_ = false;
        replaces_.clear();
        pending_.reset();
        reflect::Warnings warnings;
        if (!ref.Empty()) {
            sceneList_.open = ref;  // (written with the next save: the scene the project opens with)
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
        InspectSettings();
        host_.Redraw();
    }

    void OpenSceneAsked(const AssetRef& ref) {
        if (ref.Empty() || ref.guid == slot_.guid) return;
        AfterAskingToSave(Tr(L"別のシーンを開く"), [this, ref] { ShowScene(ref); });
    }

    // A new empty scene (Assets/Scenes/Scene.rmscene ...), saved at once: it is in the list and on disk.
    void NewSceneAsked() {
        AfterAskingToSave(Tr(L"新しいシーンを作る"), [this] {
            ShowScene({}, NewSceneFile("Scene"));
            if (host_.SaveProject()) sceneChosen_ = AssetRef{slot_.guid};
        });
    }

    void DrawScenesPanel(ui::Context& c, const Rect& r) {
        auto& p = c.P();
        const std::vector<AssetRef> rows = GameScenes(sceneList_, AssetRef{slot_.guid});
        const AssetRef first = FirstScene(sceneList_);
        const AssetRef open{slot_.guid};
        if (!sceneChosen_) sceneChosen_ = open;
        const AssetRef chosen = *sceneChosen_;
        const auto inList = [&](const AssetRef& ref) { return !ref.Empty() && std::find(sceneList_.scenes.begin(), sceneList_.scenes.end(), ref) != sceneList_.scenes.end(); };
        const auto at = std::find(sceneList_.scenes.begin(), sceneList_.scenes.end(), chosen);
        const size_t chosenAt = at == sceneList_.scenes.end() ? sceneList_.scenes.size() : static_cast<size_t>(at - sceneList_.scenes.begin());
        // The scenes Assets has and the list does not (to add).
        std::vector<AssetRef> others;
        for (const std::string& path : SceneFiles()) {
            const AssetRef ref = host_.AssetOf(host_.GetProject().Root() / PathFromUtf8(path));
            if (!ref.Empty() && ref != open && !inList(ref)) others.push_back(ref);
        }
        constexpr float kRow = 28;
        const float contentH = 40 + kRow * static_cast<float>(rows.size()) + 120 + (others.empty() ? 0 : 30 + kRow * static_cast<float>(others.size())) + 40;
        const float off = editor::w::BeginScroll(c, "scenes.scroll", r, contentH);
        const Rect area = Rect{r.x, r.y - off, r.w, std::max(r.h, contentH)}.Inset(10, 8, 10, 8);
        float y = area.y;
        ui::TextStyle ts;
        ts.size = 12;
        ts.color = theme::TextDim;
        p.Text({area.x, y, area.w, 20}, Tr(L"ゲームのシーン (この順。ゲームは「最初」から始まります)"), ts);
        y += 24;
        ui::TextStyle name;
        name.size = 13;
        name.color = theme::Text;
        for (size_t i = 0; i < rows.size(); ++i) {
            const AssetRef& ref = rows[i];
            const Rect row{area.x, y, area.w, kRow - 2};
            c.PushId(ref.Empty() ? std::string("new") : ref.guid);
            const auto b = editor::w::Selectable(c, "row", row, ref == chosen);
            c.PopId();
            if (b.clicked) {
                sceneChosen_ = ref;
                renamingScene_ = false;
                sceneProblem_.clear();
            }
            if (b.doubleClicked && !Running()) OpenSceneAsked(ref);
            std::wstring label = std::format(L"{}. {}", i, Utf8ToWide(SceneTitle(ref)));
            if (ref == open) label += Tr(L"  (開いている)");
            if (ref.Empty()) label += Tr(L"  (未保存)");
            p.Text(row.Inset(10, 0, 70, 0), label, name);
            if (!ref.Empty() && ref == first) p.Text(row.Inset(10, 0, 10, 0), Tr(L"最初"), [&] {
                ui::TextStyle s = ts;
                s.color = theme::Accent;
                s.h = ui::HAlign::Right;
                return s;
            }());
            y += kRow;
        }
        y += 6;
        // What to do with the chosen one.
        const bool chosenListed = inList(chosen);
        const bool playing = Running();
        float x = area.x;
        auto button = [&](const char* id, const std::wstring& text, bool enabled, float width) {
            const bool pressed = editor::w::Button(c, id, {x, y, width, 26}, text, editor::w::ButtonKind::Secondary, enabled);
            x += width + 6;
            return pressed;
        };
        if (button("scene_open", Tr(L"開く"), !playing && chosen != open, 70)) OpenSceneAsked(chosen);
        if (button("scene_first", Tr(L"最初にする"), chosenListed && chosen != first, 100)) {
            EditSceneList(TrU8("最初のシーンを変える"), [&](SceneList& l) { l.first = chosen; });
        }
        if (button("scene_up", L"↑", chosenListed && chosenAt > 0, 32)) {
            EditSceneList(TrU8("シーンの順を変える"), [&](SceneList& l) { std::swap(l.scenes[chosenAt], l.scenes[chosenAt - 1]); });
        }
        if (button("scene_down", L"↓", chosenListed && chosenAt + 1 < sceneList_.scenes.size(), 32)) {
            EditSceneList(TrU8("シーンの順を変える"), [&](SceneList& l) { std::swap(l.scenes[chosenAt], l.scenes[chosenAt + 1]); });
        }
        y += 32;
        x = area.x;
        if (button("scene_new", Tr(L"新しいシーン"), !playing, 120)) NewSceneAsked();
        if (button("scene_rename", Tr(L"名前を変える"), !playing && !chosen.Empty(), 110)) {
            renamingScene_ = true;
            sceneNewName_ = SceneTitle(chosen);
            sceneProblem_.clear();
        }
        // (the scene shown is always in the game: it joins the list when saved)
        if (button("scene_remove", Tr(L"一覧から外す"), chosenListed && chosen != open, 110)) {
            EditSceneList(TrU8("シーンを一覧から外す"), [&](SceneList& l) {
                std::erase(l.scenes, chosen);
                if (l.first == chosen) l.first = {};
            });
        }
        y += 32;
        if (renamingScene_) {
            editor::w::TextField(c, "scene_name", {area.x, y, std::max(60.f, area.w - 150), 26}, sceneNewName_);
            if (editor::w::Button(c, "scene_name_ok", {area.Right() - 140, y, 70, 26}, Tr(L"決める"), editor::w::ButtonKind::Primary) ||
                (c.KeyPressed(ui::Key::Enter) && !sceneNewName_.empty())) {
                RenameScene(chosen, sceneNewName_);
            }
            if (editor::w::Button(c, "scene_name_cancel", {area.Right() - 64, y, 64, 26}, Tr(L"やめる"))) renamingScene_ = false;
            y += 32;
        }
        if (!sceneProblem_.empty()) {
            ui::TextStyle es = ts;
            es.color = theme::Danger;
            p.Text({area.x, y, area.w, 22}, sceneProblem_, es);
            y += 26;
        }
        if (!others.empty()) {
            y += 6;
            p.Text({area.x, y, area.w, 20}, Tr(L"Assets にあって一覧に無いシーン"), ts);
            y += 24;
            for (const AssetRef& ref : others) {
                c.PushId(ref.guid);
                if (editor::w::Button(c, "add", {area.Right() - 60, y, 60, 24}, Tr(L"足す"))) {
                    EditSceneList(TrU8("シーンを一覧に足す"), [&](SceneList& l) { l.scenes.push_back(ref); });
                }
                c.PopId();
                p.Text({area.x + 10, y, area.w - 80, 24}, Utf8ToWide(SceneTitle(ref)), name);
                y += kRow;
            }
        }
        editor::w::EndScroll(c, "scenes.scroll", r, contentH);
    }

    // A scene's file renamed (its id stays: the list and Game::LoadScene use the new name). The new
    // scene not saved yet: the file it will be.
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
            if (ref.guid == slot_.guid) SlotNow();  // (its file is found by its id)
        }
        log::Info("シーンの名前を「{}」にしました", to);
        renamingScene_ = false;
        host_.Redraw();
    }

    // The scene files in Assets/ (from the project folder), looked at now and then.
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

    // --- Prefabs -------------------------------------------------------------------------------
    // A prefab file is an asset (a .rmprefab in Assets/) and is named by its id (AssetRef): moving or
    // renaming it keeps every placed one linked. Data from before assets had ids names it by a path
    // from the project folder - read the same way (SoftwareHost::AssetFile), made an id when opened.
    static constexpr size_t kMaxNesting = kMaxPrefabNesting;  // (runtime/Prefab.h)

    std::filesystem::path FullPath(const std::string& path) const { return host_.GetProject().Root() / PathFromUtf8(path); }
    // A file's path from the project folder, "/" between the parts.
    std::string RelativePath(const fs::path& file) const {
        return PathToUtf8(file.lexically_normal().lexically_relative(host_.GetProject().Root().lexically_normal()).generic_wstring());
    }

    // --- Scenes (runtime/SceneFile.h) --------------------------------------------------------
    // Where a new scene's file goes: Assets/Scenes/Main.rmscene ("Main 2" ... when taken).
    std::string NewSceneFile(std::string_view name = "Main") const {
        return NewScenePath(name, [this](const std::string& p) {
            std::error_code ec;
            return fs::exists(FullPath(p), ec);
        });
    }

    // Reads the scene the list shows (its file found by its id). One that cannot be read leaves the
    // scene empty and is never written over (an edited scene goes to a new file at the save).
    void ReadOpenScene(reflect::Warnings& warnings) {
        const AssetRef open = OpenScene(sceneList_);
        if (open.Empty()) return;  // no scene yet: a new file at the first save
        const fs::path file = host_.AssetFile(open);
        std::string err;
        std::optional<json::Value> scene;
        if (file.empty()) {
            err = TrU8("ファイルが Assets に見つかりません");
        } else if (auto bytes = platform::ReadFileBytes(file, &err)) {
            scene = ReadSceneFile(*bytes, &err);
            if (scene) slot_ = {open.guid, RelativePath(file), TextSign(*bytes)};
        }
        if (!scene) {
            sceneUnread_ = true;
            replaces_ = file.empty() ? open.guid : std::string();  // a missing file's place in the list goes to the new one
            log::Error("シーン ({}) を読めませんでした: {}。空のシーンで開きます。編集して保存すると新しいファイルに書きます (元のファイルは上書きしません)",
                       file.empty() ? open.guid : PathToUtf8(file), err);
            return;
        }
        scene_.Load(*scene, &warnings);
    }

    // The scene's slot as it is now: a file moved or renamed since is found by its id; one deleted
    // since is written again as a new scene (a new file and id, in its place in the list).
    SceneSlot SlotNow() {
        if (slot_.guid.empty()) {
            // Decided now, not when it was read: a file put there since (Main.rmscene back from the
            // recycle bin) is never written over.
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

    // The prefab's name (its file's, without .rmprefab) and where its file is, for the screen.
    std::string PrefabTitle(const AssetRef& ref) const {
        const std::filesystem::path f = host_.AssetFile(ref);
        return f.empty() ? PrefabName(ref.guid) : PathToUtf8(f.stem());
    }
    std::string PrefabWhere(const AssetRef& ref) const {
        const std::filesystem::path f = host_.AssetFile(ref);
        return f.empty() ? ref.guid : PathToUtf8(f.lexically_relative(host_.GetProject().Root()).generic_wstring());
    }

    // The prefab file, read now (and kept as the last known form of it). Nothing when it cannot be read.
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

    // What every placed prefab of `scene` changed, worked out now against its prefab as last read
    // (kept in the file and in a nested prefab's link, so a changed prefab can be put under it again).
    // `scene` is scene_ or a copy of it as it is now (SaveData).
    void UpdateOverrides(Scene& scene) const {
        for (const int root : scene.PrefabRoots()) {
            if (const json::Value* o = CachedOverrides(root)) scene.Find(root)->prefab.overrides = *o;
        }
    }

    // What the placed prefab `root` changed, against its prefab as last read - worked out once per
    // change of the scene (the history's change number), of a prefab file or of the loaded data, not
    // every time the inspector is drawn. Null: not a placed prefab, or its file is not read.
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

    // The prefabs `root` is inside (and its own): a prefab is never filled inside itself.
    std::vector<std::string> ChainOf(int root) const { return ChainOf(scene_, root); }
    static std::vector<std::string> ChainOf(const Scene& s, int root) {
        std::vector<std::string> chain;
        size_t steps = 0;
        for (const SceneEntity* e = s.Find(root); e && steps++ <= s.Entities().size(); e = s.Find(e->data.parent)) {
            if (e->prefab.Root()) chain.push_back(e->prefab.asset.guid);
        }
        return chain;
    }

    // True when `prefab` has `guid` inside it (nested, at any depth, through the files as last read).
    bool ContainsPrefab(const json::Value& prefab, const std::string& guid, size_t depth = 0) {
        if (depth > kMaxNesting) return true;  // deeper than anything sensible: a loop
        for (const std::string& a : NestedAssets(prefab)) {
            if (a == guid) return true;
            if (const json::Value* inner = Cached(AssetRef{a}); inner && ContainsPrefab(*inner, guid, depth + 1)) return true;
        }
        return false;
    }

    // Fills the prefabs nested in the placed prefab `root` from their own files (each with its own
    // overrides), and the ones nested in those ...
    void FillNested(int root, std::vector<std::string>& chain, reflect::Warnings& warnings) {
        FillNestedPrefabs(
            scene_, root, chain, [this](const AssetRef& r) { return Cached(r); }, warnings, [this](const AssetRef& r) { return PrefabTitle(r); });
    }

    // The placed prefab `root` made again from `prefab` with `overrides`, then what is nested in it.
    void Remake(int root, const json::Value& prefab, const json::Value& overrides, reflect::Warnings& warnings) {
        scene_.ApplyPrefab(root, prefab, overrides, &warnings);
        std::vector<std::string> chain = ChainOf(root);
        FillNested(root, chain, warnings);
    }

    // The overrides a prefab file keeps for the prefab nested in it as entity `id`.
    static json::Value FileOverrides(const json::Value& prefab, int id) {
        for (const auto& e : prefab.Get("entities").Items()) {
            if (e.Get("id").AsInt(0) == id) return e.Get("prefab").Get("overrides");
        }
        return {};
    }

    // Opening the project: every placed prefab is made from its file again (with what it changed),
    // outermost first, so a prefab changed since (by another scene, by hand) shows. Not an edit: the
    // project is as it was.
    void RefreshPrefabsOnOpen() {
        prefabs_.clear();
        ++dataVersion_;
        // Data from before assets had ids: the prefab named by a path gets its file's id (written so
        // at the next save).
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

    // Every placed prefab of `s` made from its file again (with what it changed), outermost first: the
    // scene opened, and the other scenes of the game as Play switches to them and as they are exported
    // (their files keep the prefabs as of their last save - a prefab changed since shows in them too).
    void RefreshPrefabsIn(Scene& s, reflect::Warnings& warnings) {
        for (const int root : s.PrefabRoots()) {
            const SceneEntity* r = s.Find(root);
            if (!r) continue;  // went with a prefab around it
            if (r->prefab.outer > 0 && s.PrefabRootOf(r->data.parent) != 0) continue;  // filled with the one around it
            const AssetRef ref = r->prefab.asset;
            const json::Value overrides = r->prefab.overrides;
            if (const json::Value* prefab = Cached(ref)) {
                const json::Value copy = *prefab;
                s.ApplyPrefab(root, copy, overrides, &warnings);
            }  // (else kept as saved; the ones inside still follow their files)
            std::vector<std::string> chain = ChainOf(s, root);
            FillNestedPrefabs(
                s, root, chain, [this](const AssetRef& a) { return Cached(a); }, warnings, [this](const AssetRef& a) { return PrefabTitle(a); });
        }
    }

    // Every placed prefab of `ref` made again from `next`, keeping what each changed from `before`
    // (except `skip`). A prefab nested in one that was not changed there takes what `next` has for it.
    // Inside an Edit; UpdateOverrides has been called while the cache still had `before`.
    void ReapplyPrefab(const AssetRef& ref, const json::Value& before, const json::Value& next, int skip) {
        reflect::Warnings warnings;
        for (const int root : scene_.PrefabRoots(ref.guid)) {
            if (root == skip || !scene_.Find(root)) continue;
            const json::Value overrides = Overrides(scene_, root, before, placementTypes_);
            std::vector<std::pair<int, int>> untouched;  // (nested root, its id in the file)
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

    // The prefab files in Assets/ (looked for again at most every 2 seconds: the menu is asked often).
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

    // A file name in Assets/ for a new prefab named after the entity (not one that is there).
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
        prefabFilesTime_ = 0;  // look again
        return true;
    }

    // Writes a prefab file's text (wherever the asset is now) and keeps it as the last known form.
    bool WriteBytes(const AssetRef& ref, const std::string& text) {
        const std::filesystem::path file = host_.AssetFile(ref);
        std::string err;
        if (file.empty() || !platform::WriteFileAtomic(file, text, &err)) {
            log::Error("Prefab「{}」のファイルを書けませんでした: {}", PrefabTitle(ref), file.empty() ? std::string("見つかりません") : err);
            return false;
        }
        if (auto v = json::Parse(text); v && IsPrefab(*v)) {
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
        // Placing one: after the selected entity, like 貼り付け.
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
        UpdateOverrides(scene_);  // the prefabs placed inside are kept with what was changed on them
        std::unordered_map<int, int> sources;
        const json::Value prefab = MakePrefab(scene_, id, &sources);
        if (!WriteFile(FullPath(path), prefab)) return;
        const AssetRef ref = host_.AssetOf(FullPath(path));  // the new file's id (its .meta is written now)
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
        // The file as it is now: kept whole for undo, and never written over when it is not a prefab
        // this version can read (a newer version's, a broken one).
        std::string err;
        const auto onDisk = platform::ReadFileBytes(file, &err);
        const auto parsed = onDisk ? json::Parse(*onDisk) : std::nullopt;
        if (!parsed || !IsPrefab(*parsed) || NewerPrefab(*parsed)) {
            log::Warn("Prefab「{}」のファイルが読めない (新しい版で作られたか、壊れている) ので、反映しません", PrefabTitle(ref));
            return;
        }
        // The prefab as it was (what the other placed ones changed is worked out against it).
        json::Value before = *parsed;
        if (const json::Value* old = Cached(ref)) before = *old;
        UpdateOverrides(scene_);  // while the cache still has the prefabs as they were
        std::unordered_map<int, int> sources;
        const json::Value next = MakePrefab(scene_, root, &sources, &*parsed);  // (the file's other keys stay)
        if (ContainsPrefab(next, ref.guid)) {
            log::Warn("Prefab「{}」の中に「{}」自身が入ることになるので、反映しません", PrefabTitle(ref), PrefabTitle(ref));
            return;
        }
        const std::string nextText = json::Write(next);
        if (!WriteBytes(ref, nextText)) return;
        // Undo writes the file back as it was (and the placed ones with it); redo writes it again.
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

    // The file as it is now (changed by hand, by another project): every placed one follows it.
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
            FillNested(made, chain, warnings);  // the prefabs inside it, from their own files
        });
        for (const auto& w : warnings) log::Warn("Prefab「{}」: {}", path, w);
        if (made) host_.SelectInTree({static_cast<tree::ItemId>(made)});
    }

    // --- Editing -------------------------------------------------------------------------------
    std::string NameOf(int id) const {
        const SceneEntity* e = scene_.Find(id);
        return e ? e->data.name : std::string();
    }

    // Runs a change of the scene's shape and puts it in the history (edit = undo entry = unsaved).
    // A change that changes nothing leaves no entry. Never while playing (the copy plays).
    // THE way a change of the scene's shape goes into the history (edit = undo entry = unsaved):
    // `touched` names the entities the change alters (with everything below them - SceneRecord.h);
    // the ones it makes are found by themselves. Only those are kept, before and after. `also`:
    // something done with it outside the scene (a file written), undone with it in one entry.
    // A change that changes nothing leaves no entry. Never while playing (the copy plays).
    // Debug build: every edit is checked against the whole scene before and after (a touched entity
    // left out would make undo wrong - this finds it at once, while the record is still small).
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

    // (The whole scene before and after: no edit uses it now - every one names what it touches.
    // Kept for a change whose touched entities cannot be named.)
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

    // 追加 ▸ ... (a plugin's CreateItemInfo): under the selected one or after the selection.
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
                return;
            }
            made = MakeEntity(WideToUtf8(Tr(info.label)), parent, index);  // (its name in the language in use)
            info.fill(scene_, made);
        });
        if (made) host_.SelectInTree({static_cast<tree::ItemId>(made)}, true);
    }

    // An entity as 空のエンティティ makes it: the components new entities get (e.g. the Transform of the
    // game's space).
    int MakeEntity(std::string name, int parent, size_t index) {
        const int made = scene_.Create(std::move(name), parent, index);
        const std::string space = CurrentSpace();
        for (const ComponentInfo* info : newEntityInfos_) {
            if (!info->space || info->space == space) scene_.AddComponent(made, info->type());
        }
        return made;
    }

    // The order of 追加 ▸'s folders (the key as the plugin wrote it, before it is translated); others
    // after these, in the order of the plugins.
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
            made = MakeEntity(TrU8("エンティティ"), parent, index);  // the name it starts with, in the language in use
        });
        if (made) host_.SelectInTree({static_cast<tree::ItemId>(made)}, true);
    }

    editor::SoftwareHost& host_;
    GameSettings settings_;
    reflect::Unknowns unknowns_;  // keys / enum names of a newer version: written back unchanged
    Scene scene_;
    std::vector<int> selection_;  // the 階層's selection (shown in the scene view)
    std::vector<View> views_;  // one per space offered, the default (smallest order) first
    bool viewsAsked_ = false;
    std::vector<const ComponentInfo*> newEntityInfos_;  // components new entities get (of the game's space)
    std::vector<const ComponentLook*> componentLooks_;      // how the editor shows them (editor parts)
    std::vector<std::string> placementTypes_;
    std::unordered_map<std::string, const char*> componentFolders_;  // TypeDesc::name -> ComponentInfo::folder               // where a placed prefab is (Transform)
    json::Value view_;  // UserSettings: how the user looks (the scene view's camera ...)
    std::unordered_map<int, int> modes_;  // entity id -> how its component with derived values is shown
    bool hadData_ = false;  // the .rmpf has the game's data (read from it, or written by a save)
    bool gave_ = false;     // the last SaveData gave data (Saved: it is in the .rmpf now)
    // The scenes (runtime/SceneFile.h): the list in the .rmpf, and the file of the one shown.
    SceneList sceneList_;
    reflect::Unknowns sceneListUnknowns_;  // keys of a newer version: written back unchanged
    SceneSlot slot_;
    std::string replaces_;      // the id of a scene whose file was gone: the new file takes its place in the list
    bool oldForm_ = false;      // read from inside the .rmpf (a project of before): split off at the next save
    bool sceneUnread_ = false;  // the list's scene could not be read: never written over
    struct Pending {
        SceneSlot slot;
        SceneList list;
    };
    std::optional<Pending> pending_;  // what SaveData gave, taken on by Saved()
    std::vector<std::string> sceneFiles_;  // SceneFiles()
    double sceneFilesTime_ = 0;
    // 書き出し (GameExport.h): the project's part, the panel, the export running on a worker.
    ExportSettings exportSettings_;
    reflect::Unknowns exportUnknowns_;
    editor::inspector::State exportInspector_;
    struct ExportRun {
        std::mutex mtx;
        float progress = 0;
        std::vector<std::string> lines;
        bool running = false;
        std::optional<bool> ok;  // set when it ended
    };
    std::shared_ptr<ExportRun> exportRun_ = std::make_shared<ExportRun>();
    std::jthread exportThread_;
    bool inspectingEntities_ = false;  // the inspector shows the 階層's selection (not the settings)
    bool inspectingScene_ = false;     // ... the scene's own values (the 階層's top row is drawn chosen)
    std::vector<const SceneSettingLook*> settingLooks_;              // how the editor shows them (editor parts)
    std::unordered_map<std::string, std::string> settingSpaces_;    // setting type -> the game's space it is for

    // Play
    PlayState play_ = PlayState::Stopped;
    GameScripts scripts_;  // the game's C++ scripts (built, loaded, swapped; run while playing)
    reflect::Warnings heldSceneWarnings_;  // told after the first build (see LoadData)
    DebugDraw debug_;      // what the scripts draw with rm::Debug while playing (shown by the views; gone at Stop)
    std::vector<const reflect::TypeDesc*> baseTypes_, baseMultiple_;  // the plugins' component types
    std::unique_ptr<Scene> playScene_;  // the copy that plays
    std::vector<Kept> kept_;            // この値を残す: written back at Stop
    std::vector<std::pair<Kept, json::Value>> keptLeft_;  // ... their values when the scene from before Play was left (Game::LoadScene)
    bool mouseInGame_ = false;          // the mouse over the game view (its last drawing)
    // The デバッグ panel and what it shows: how often the views are drawn and Play's frames run, the mouse
    // over the scene view.
    DebugPanel debugPanel_;
    std::unique_ptr<editor::ToolWindow> debugWindow_;  // the デバッグ window while it is open
    FrameMeter sceneMeter_, gameMeter_, updateMeter_;
    json::Value settingsBeforePlay_;  // the game settings when Play started (null: not playing)
    double debugShownAt_ = -1e9;  // the window was drawn then (Play's parts are timed while it is shown)
    FrameGraph playGraph_, sceneGraph_;
    PlayTimes playTimes_;
    std::vector<ScriptRunner::ScriptTime> scriptTimes_;
    std::vector<SystemRunner::SystemTime> systemTimes_;
    std::optional<Vec2> sceneMouse_;
    std::optional<Vec2> pointer_;       // ... on the game screen (DIPs of the game settings' 画面); nullopt: not over it
    bool pointerWentDown_ = false, pointerWentUp_ = false;  // this frame's left button presses (PlayFrame)
    std::vector<std::unique_ptr<ScreenDrawer>> screenDrawers_;  // over the game view (the game UI)
    std::map<std::string, std::unique_ptr<SceneView>> viewPanels_;  // the plugins' panels (ViewPanelInfo), by id
    bool screensAsked_ = false;
    std::vector<AssetRef> playScenes_;  // the scenes Play can switch to (GameScenes when it started)
    std::vector<std::string> cameraTypes_;  // the components the game sees through (SceneCamera)
    // シーンの一覧 panel: the row chosen (a scene's id; "" = the new scene not saved yet), renaming it.
    std::optional<AssetRef> sceneChosen_;
    bool renamingScene_ = false;
    std::string sceneNewName_;
    std::wstring sceneProblem_;
    SystemRunner runner_;
    PlayServices services_;  // the plugins' (音 ...): from ▶ to ■
    double lastTick_ = 0;

    // Prefabs: the files as last read or written (path -> content)
    std::unordered_map<std::string, json::Value> prefabs_;
    uint64_t dataVersion_ = 0;  // up at every change of prefabs_ or of a scene made without the history
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
