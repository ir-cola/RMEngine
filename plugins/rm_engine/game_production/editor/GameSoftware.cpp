#include "rm_engine/game_production/editor/GameSoftware.h"

#include "base/FileName.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/reflect/Data.h"
#include "editor/lang/Lang.h"
#include "editor/project/Project.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/editor/ComponentLook.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_production/runtime/SceneRecord.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "core/asset/AssetFiles.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <format>
#include <memory>
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
    explicit GameSoftware(editor::SoftwareHost& host) : host_(host) {
        // Component types come from other plugins (the connectors: "ゲーム制作 × 3D" offers Transform).
        std::vector<const reflect::TypeDesc*> types, multiple;
        for (const auto& c : host.Registry().Contributions<ComponentInfo>(kComponentPoint)) {
            const reflect::TypeDesc* t = &c.item->type();
            if (std::find(types.begin(), types.end(), t) != types.end()) continue;
            types.push_back(t);
            if (c.item->onNewEntities) newEntityInfos_.push_back(c.item);
            if (c.item->multiple) multiple.push_back(t);
            if (c.item->placement) placementTypes_.push_back(t->name);
        }
        scene_.SetComponentTypes(std::move(types), std::move(multiple));
        // How the editor shows them (the 階層's pictures, ローカル / ワールド): their plugins' editor parts.
        for (const auto& l : host.Registry().Contributions<ComponentLook>(kComponentLookPoint)) componentLooks_.push_back(l.item);
        // ... and so do the systems that move them while the game plays.
        runner_.SetSystems(RegisteredSystems(host.Registry()));
    }

    // --- Software ---------------------------------------------------------------------------
    std::vector<editor::dock::PanelType> Panels() override {
        return {
            {kSceneView, TrKey(L"シーン"), [this](ui::Context& c, const Rect& r) { DrawSceneView(c, r); }},
            {kGameView, TrKey(L"ゲーム"), [this](ui::Context& c, const Rect& r) { DrawGameView(c, r); }},
        };
    }

    editor::dock::Layout DefaultLayout() const override {
        using editor::dock::Axis;
        namespace cp = editor::core_panel;
        editor::dock::Layout l;
        auto top = l.MakeSplit(Axis::Horizontal, 0.17f, l.MakeLeaf({cp::kHierarchy}),
                               l.MakeSplit(Axis::Horizontal, 0.78f, l.MakeLeaf({kSceneView, kGameView}),
                                           l.MakeLeaf({cp::kInspector})));
        l.main = l.MakeSplit(Axis::Vertical, 0.72f, std::move(top), l.MakeLeaf({cp::kConsole, cp::kAssets}));
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
        hadData_ = data.IsObject();
        slot_ = {"", NewSceneFile(), 0};
        if (hadData_) {
            reflect::Warnings warnings;
            reflect::Load(settings_, data.Get("settings"), &unknowns_, &warnings);
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
            for (const auto& w : warnings) log::Warn("シーン: {}", w);
            RefreshPrefabsOnOpen();
            ++dataVersion_;  // (the scene was made again: no history entry tells it)
        }
        InspectSettings();
    }

    bool SaveData(json::Value& data, std::vector<editor::ProjectFileWrite>& files) override {
        pending_.reset();
        // Nothing is written (and the plugin is not recorded as used) while everything is as a new
        // project has it - opening a project once must not tie it to this plugin.
        const GameSettings defaults;
        const bool untouched = reflect::Equals(reflect::TypeOf<GameSettings>(), &settings_, &defaults) && unknowns_.Empty();
        if (!hadData_ && untouched && scene_.Empty()) return false;
        data.Set("settings", reflect::Save(settings_, &unknowns_));
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
                if (auto bytes = platform::ReadFileBytes(rmpf, &err)) files.push_back({PathToUtf8(backup.filename()), std::move(*bytes)});
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

    // The plugins offering the components of the scene and of the prefabs it places (recorded in the
    // .rmpf: an exported game links them).
    std::vector<PluginId> UsedPlugins() const override {
        std::vector<std::string> types;
        ComponentTypesIn(scene_.Save(), types);
        for (const auto& [guid, prefab] : prefabs_) ComponentTypesIn(prefab, types);
        return PluginsOffering(types, host_.Registry().Contributions<ComponentInfo>(kComponentPoint));
    }

    void Saved() override {
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
        if (c.Focused() == 0) {
            if (c.KeyPressed(ui::Key::P, true)) Running() ? StopPlay() : StartPlay(c);
            if (c.KeyPressed(ui::Key::P, true, true) && Running()) TogglePause(c);
        }
        if (play_ == PlayState::Playing && playScene_) {
            // The time since the last frame (at most a quarter of a second: a frame that took long -
            // a window dragged, the software switched - does not make the game jump).
            const double dt = std::clamp(c.Time() - lastTick_, 0.0, 0.25);
            lastTick_ = c.Time();
            AdvancePlay(dt);
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
        if (running && r.Right() - x > 80) {
            ui::TextStyle ts;
            ts.size = 12;
            ts.color = theme::Text;
            const std::wstring text = play_ == PlayState::Paused ? Tr(L"一時停止中 — ■ で止めると Play の前に戻ります")
                                                                  : Tr(L"Play 中 — 編集はできません。■ で止めると Play の前に戻ります");
            c.P().Text({x + 8, r.y, r.Right() - x - 8, r.h}, text, ts);
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

    std::wstring EmptyText() const override {
        return Tr(L"エンティティはまだありません。\nここを右クリックして「エンティティを作る」で作れます。");
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

        tree::Action create;
        create.label = Tr(L"エンティティを作る");
        create.key = ui::Key::N;
        create.ctrl = create.shift = true;
        create.run = [this, sel] {
            const auto [parent, index] = After(sel);
            CreateEntity(parent, index);
        };
        list.push_back(std::move(create));

        tree::Action child;
        child.label = Tr(L"子エンティティを作る");
        child.enabled = sel.size() == 1;
        if (child.enabled) child.run = [this, sel] { CreateEntity(sel[0], Scene::kEnd); };
        list.push_back(std::move(child));

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
        runner_.SetCamera(settings_.startCamera.id);  // the systems may switch it (SystemContext::camera)
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
        runner_.Advance(*playScene_, dt, settings_.fixedTimestep);
        if (runner_.Camera() != before) {
            const SceneEntity* e = playScene_->Find(static_cast<int>(runner_.Camera()));
            log::Info("ゲームのカメラを「{}」に切り替えました", e ? e->data.name : std::string("(なし)"));
        }
    }

    void StopPlay() {
        if (!Running()) return;
        log::Info("■ 停止 ({:.2f} 秒・{} コマ)。シーンは Play の前に戻りました", runner_.Time(), runner_.Frames());
        playScene_.reset();
        play_ = PlayState::Stopped;
        host_.Redraw();
    }

    void TogglePause(ui::Context& c) {
        if (!Running()) return;
        play_ = play_ == PlayState::Paused ? PlayState::Playing : PlayState::Paused;
        lastTick_ = c.Time();  // the paused time does not count
        c.RequestAnimation();
    }

    // One frame (1 / 目標 FPS seconds), then paused.
    void StepFrame(ui::Context& c) {
        if (!Running() || !playScene_) return;
        play_ = PlayState::Paused;
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
        h.game = game;
        h.background = settings_.clearColor;
        h.camera = Running() ? runner_.Camera() : settings_.startCamera.id;  // in Play: as the systems left it
        h.aspect = static_cast<float>(std::max(1, settings_.display.width)) / static_cast<float>(std::max(1, settings_.display.height));
        h.openSettings = [this] { InspectSettings(); };
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
        SceneViewHost h = ViewHost(true);
        view->game->Draw(c, r, h);
        // The other spaces over it, in their order (2D over a 3D game, when it has a camera to show it).
        for (View& other : views_) {
            if (&other != view) other.game->DrawLayer(c, r, h);
        }
        if (play_ == PlayState::Playing) c.RequestAnimation();
    }

    // --- Inspector -----------------------------------------------------------------------------
    // A note on top of the inspector while playing (its blocks are shown, not edited).
    static editor::InspectBlock PlayNote() {
        return {nullptr, {}, {}, Tr(L"Play 中です。ここに出ているのは動いているコピーで、変えられません。■ で止めると Play の前に戻ります。"), {}};
    }

    void InspectSettings() {
        inspectingEntities_ = false;
        editor::Inspectable in;
        in.title = Tr(L"ゲームの設定");
        in.blocks = [this] {
            std::vector<editor::InspectBlock> blocks;
            if (Running()) blocks.push_back(PlayNote());
            editor::InspectBlock b{&reflect::TypeOf<GameSettings>(), [this] { return reflect::EditTarget{&settings_, &unknowns_}; }, {}, {}, {}};
            b.readOnly = Running();
            blocks.push_back(std::move(b));
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
    void EditDerived(int id, const std::string& type, const ComponentLook* info, editor::InspectBlock::Change ch) {
        const std::vector<const void*> chain = Chain(scene_, id, type);
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
        const auto resolve = ResolveComponent(id, type);
        std::vector<std::unique_ptr<ICommand>> cmds;
        for (size_t i = 0; i < ct.fields.size() && i < 64; ++i) {
            if (!(diff >> i & 1)) continue;
            const reflect::FieldDesc& f = ct.fields[i];
            cmds.push_back(std::make_unique<reflect::SetFieldCommand>(ct, reflect::FieldPath{i}, reflect::SaveField(f, chain.back(), nullptr),
                                                                      reflect::SaveField(f, next.Get(), nullptr), resolve, ch.label, ch.merge));
        }
        if (cmds.size() == 1) {
            host_.History().Push(std::move(cmds.front()), false);
            return;
        }
        auto group = std::make_unique<CommandGroup>(ch.label);
        for (auto& cmd : cmds) group->Add(std::move(cmd));
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
            entity.readOnly = play;
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
                    EditTouching({id}, std::format("「{}」を外す", label), [&] { scene_.RemoveComponent(id, type, nth); });
                };
                // Its place among the entity's components (the order they are shown - and run).
                const size_t at = static_cast<size_t>(&c - e->components.data());
                editor::w::Action up;
                up.label = Tr(L"上へ移動");
                up.enabled = at > 0;
                up.run = [this, id, type, nth, label] {
                    EditTouching({id}, std::format("「{}」を上へ移動", label), [&] { scene_.MoveComponent(id, type, -1, nth); });
                };
                editor::w::Action down;
                down.label = Tr(L"下へ移動");
                down.enabled = at + 1 < e->components.size();
                down.run = [this, id, type, nth, label] {
                    EditTouching({id}, std::format("「{}」を下へ移動", label), [&] { scene_.MoveComponent(id, type, 1, nth); });
                };
                editor::w::Action sep;
                sep.separator = true;
                std::vector<editor::w::Action> menu{up, down, sep, remove};
                if (play) {
                    for (auto& a : menu) a.enabled = false;
                }
                if (c.Known()) {
                    editor::InspectBlock b{c.value.Type(), ResolveComponent(id, c.type, nth, play), Utf8ToWide(TrU8(label)) + number, {}, menu};
                    b.readOnly = play;
                    // A child: ローカル / ワールド (or whatever the plugin calls them), one block shown either way.
                    // At the top level both ways are the same numbers, so there is no switch.
                    if (const ComponentLook* info = LookOf(c.type); info && info->HasDerived() && nth == 0 && e->data.parent != 0) {
                        b.modes = {Tr(info->ownLabel), Tr(info->derivedLabel)};
                        b.mode = ModeOf(*e);
                        b.setMode = [this, id](int m) { modes_[id] = m; };
                        if (b.mode == 1) {
                            b.type = &info->derivedType();
                            b.resolve = Derived(id, c.type, info, play);
                            b.onEdit = [this, id, type, info](editor::InspectBlock::Change ch) { EditDerived(id, type, info, std::move(ch)); };
                        }
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
            if (Running()) {
                editor::w::Action none;
                none.label = Tr(L"Play 中は部品を足せません");
                none.enabled = false;
                list.push_back(std::move(none));
                return list;
            }
            const SceneEntity* e = scene_.Find(id);
            for (const reflect::TypeDesc* t : scene_.ComponentTypes()) {
                if (!e || (e->FindComponent(t->name) && !scene_.AllowsMultiple(t->name))) continue;
                editor::w::Action add;
                add.label = Utf8ToWide(TrU8(t->label));
                if (e->FindComponent(t->name)) add.label += Tr(L" (もう 1 つ)");
                add.run = [this, id, t] {
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
            entity.readOnly = play;
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
                remove.enabled = !play;
                remove.run = [this, here, type, label] {
                    EditTouching(here, std::format("{} 個のエンティティから「{}」を外す", here.size(), label), [&] {
                        for (const int id : here) scene_.RemoveComponent(id, type);
                    });
                };
                editor::InspectBlock b{c.value.Type(), ResolveComponent(main, type, 0, play), Utf8ToWide(TrU8(label)), {}, {remove}};
                for (const int id : rest) b.also.push_back(ResolveComponent(id, type, 0, play));
                b.readOnly = play;
                blocks.push_back(std::move(b));
            }
            return blocks;
        };
        in.addLabel = Tr(L"＋ 部品を足す");
        in.addMenu = [this, ids] {
            std::vector<editor::w::Action> list;
            if (Running()) {
                editor::w::Action none;
                none.label = Tr(L"Play 中は部品を足せません");
                none.enabled = false;
                list.push_back(std::move(none));
                return list;
            }
            for (const reflect::TypeDesc* t : scene_.ComponentTypes()) {
                // Offered while at least one of them lacks it (or may have more); added where it can be.
                const bool someCan = scene_.AllowsMultiple(t->name) || std::any_of(ids.begin(), ids.end(), [&](int id) {
                                         const SceneEntity* e = scene_.Find(id);
                                         return e && !e->FindComponent(t->name);
                                     });
                if (!someCan) continue;
                editor::w::Action add;
                add.label = Utf8ToWide(TrU8(t->label));
                add.run = [this, ids, t] {
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

    // --- Prefabs -------------------------------------------------------------------------------
    // A prefab file is an asset (a .rmprefab in Assets/) and is named by its id (AssetRef): moving or
    // renaming it keeps every placed one linked. Data from before assets had ids names it by a path
    // from the project folder - read the same way (SoftwareHost::AssetFile), made an id when opened.
    static constexpr size_t kMaxNesting = 16;  // prefabs inside prefabs inside ...

    std::filesystem::path FullPath(const std::string& path) const { return host_.GetProject().Root() / PathFromUtf8(path); }
    // A file's path from the project folder, "/" between the parts.
    std::string RelativePath(const fs::path& file) const {
        return PathToUtf8(file.lexically_normal().lexically_relative(host_.GetProject().Root().lexically_normal()).generic_wstring());
    }

    // --- Scenes (runtime/SceneFile.h) --------------------------------------------------------
    // Where a new scene's file goes: Assets/Scenes/Main.rmscene ("Main 2" ... when taken).
    std::string NewSceneFile() const {
        return NewScenePath("Main", [this](const std::string& p) {
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
        if (slot_.guid.empty()) return slot_;
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
    std::vector<std::string> ChainOf(int root) const {
        std::vector<std::string> chain;
        size_t steps = 0;
        for (const SceneEntity* e = scene_.Find(root); e && steps++ <= scene_.Entities().size(); e = scene_.Find(e->data.parent)) {
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
        if (chain.size() > kMaxNesting) return;
        for (const int n : scene_.NestedPrefabs(root)) {
            const SceneEntity* e = scene_.Find(n);
            if (!e) continue;
            const AssetRef ref = e->prefab.asset;
            if (std::find(chain.begin(), chain.end(), ref.guid) != chain.end()) {
                warnings.push_back(std::format("Prefab「{}」が自分の中に入っているので、中を作りません", PrefabTitle(ref)));
                continue;
            }
            const json::Value* prefab = Cached(ref);
            if (!prefab) continue;  // as it was saved
            const json::Value copy = *prefab;
            const json::Value overrides = e->prefab.overrides;
            scene_.ApplyPrefab(n, copy, overrides, &warnings);
            chain.push_back(ref.guid);
            FillNested(n, chain, warnings);
            chain.pop_back();
        }
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
        for (const int root : scene_.PrefabRoots()) {
            const SceneEntity* r = scene_.Find(root);
            if (!r) continue;  // went with a prefab around it
            if (r->prefab.outer > 0 && scene_.PrefabRootOf(r->data.parent) != 0) continue;  // filled with the one around it
            const AssetRef ref = r->prefab.asset;
            const json::Value overrides = r->prefab.overrides;
            if (const json::Value* prefab = Cached(ref)) {
                const json::Value copy = *prefab;
                Remake(root, copy, overrides, warnings);
            } else {
                std::vector<std::string> chain = ChainOf(root);
                FillNested(root, chain, warnings);  // kept as saved; the ones inside still follow their files
            }
        }
        for (const auto& w : warnings) log::Warn("Prefab: {}", w);
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

    void CreateEntity(int parent, size_t index) {
        int made = 0;
        EditTouching({}, "エンティティを作成", [&] {
            made = scene_.Create(TrU8("エンティティ"), parent, index);  // the name it starts with, in the language in use
            // e.g. Transform - the one of the game's space
            const std::string space = CurrentSpace();
            for (const ComponentInfo* info : newEntityInfos_) {
                if (!info->space || info->space == space) scene_.AddComponent(made, info->type());
            }
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
    std::vector<std::string> placementTypes_;               // where a placed prefab is (Transform)
    json::Value view_;  // UserSettings: how the user looks (the scene view's camera ...)
    std::unordered_map<int, int> modes_;  // entity id -> how its component with derived values is shown
    bool hadData_ = false;
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
    bool inspectingEntities_ = false;  // the inspector shows the 階層's selection (not the settings)

    // Play
    PlayState play_ = PlayState::Stopped;
    std::unique_ptr<Scene> playScene_;  // the copy that plays
    SystemRunner runner_;
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
