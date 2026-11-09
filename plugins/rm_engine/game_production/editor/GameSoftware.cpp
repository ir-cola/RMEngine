#include "rm_engine/game_production/editor/GameSoftware.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <algorithm>
#include <format>
#include <memory>
#include <unordered_map>

namespace rm::game_production {

namespace {

namespace theme = editor::theme;
namespace tree = editor::tree;
using editor::Tr;
using editor::TrF;
using editor::TrKey;
using editor::TrU8;
using ui::Rect;

constexpr char kSceneView[] = "scene_view";
constexpr char kGameView[] = "game_view";

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
        : scene_(scene), before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {}
    void Do() override { scene_.Load(after_); }
    void Undo() override { scene_.Load(before_); }
    std::string Label() const override { return label_; }

private:
    Scene& scene_;
    json::Value before_, after_;
    std::string label_;
};

std::vector<int> ToEntityIds(std::span<const tree::ItemId> ids) {
    std::vector<int> out;
    for (const tree::ItemId id : ids) out.push_back(static_cast<int>(id));
    return out;
}

class GameSoftware final : public editor::Software, public tree::Source {
public:
    explicit GameSoftware(editor::SoftwareHost& host) : host_(host) {
        // Component types come from other plugins (the connectors: "ゲーム制作 × 3D" offers Transform).
        std::vector<const reflect::TypeDesc*> types;
        for (const auto& c : host.Registry().Contributions<ComponentInfo>(kComponentPoint)) {
            const reflect::TypeDesc* t = &c.item->type();
            if (std::find(types.begin(), types.end(), t) != types.end()) continue;
            types.push_back(t);
            componentInfos_.push_back(c.item);
            if (c.item->onNewEntities) newEntityTypes_.push_back(t);
        }
        scene_.SetComponentTypes(std::move(types));
    }

    // --- Software ---------------------------------------------------------------------------
    std::vector<editor::dock::PanelType> Panels() override {
        return {
            {kSceneView, TrKey(L"シーン"), [this](ui::Context& c, const Rect& r) { DrawSceneView(c, r); }},
            {kGameView, TrKey(L"ゲーム"),
             [this](ui::Context& c, const Rect& r) {
                 c.KeepClear(r);  // the game's picture: no theme ornament over it
                 Placeholder(c, r, Tr(L"ゲームビュー"), Tr(L"Play を押すと、ゲームのカメラから見た画面がここに映ります。"));
                 const Rect button{r.x + (r.w - 200) * 0.5f, r.y + r.h * 0.5f + 70, 200, 36};
                 if (editor::w::Button(c, "settings", button, Tr(L"ゲームの設定を開く"))) InspectSettings();
             }},
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
        if (sceneView_) sceneView_->LoadState(view_.Get("sceneView"));
        modes_.clear();
        for (const auto& pair : view_.Get("inspectorModes").Items()) {
            if (pair.Items().size() == 2) modes_[static_cast<int>(pair.Items()[0].AsInt())] = pair.Items()[1].AsInt() != 0 ? 1 : 0;
        }
    }
    json::Value SaveView() const override {
        json::Value v = view_.IsObject() ? view_ : json::Value::MakeObject();
        if (sceneView_) {
            if (json::Value s = sceneView_->SaveState(); !s.IsNull()) v.Set("sceneView", std::move(s));
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

    void LoadData(const json::Value& data) override {
        hadData_ = data.IsObject();
        if (hadData_) {
            reflect::Warnings warnings;
            reflect::Load(settings_, data.Get("settings"), &unknowns_, &warnings);
            for (const auto& w : warnings) log::Warn("ゲームの設定: {}", w);
            warnings.clear();
            scene_.Load(data.Get("scene"), &warnings);
            for (const auto& w : warnings) log::Warn("シーン: {}", w);
        }
        InspectSettings();
    }

    bool SaveData(json::Value& data) const override {
        // Nothing is written (and the plugin is not recorded as used) while everything is as a new
        // project has it - opening a project once must not tie it to this plugin.
        const GameSettings defaults;
        const bool untouched = reflect::Equals(reflect::TypeOf<GameSettings>(), &settings_, &defaults) && unknowns_.Empty();
        if (!hadData_ && untouched && scene_.Empty()) return false;
        data.Set("settings", reflect::Save(settings_, &unknowns_));
        data.Set("scene", scene_.Save());
        return true;
    }

    // --- tree::Source (the 階層) ----------------------------------------------------------------
    void Items(std::vector<tree::Item>& out) override {
        for (const TreeEntry& t : scene_.TreeOrder()) {
            tree::Item it;
            it.id = static_cast<tree::ItemId>(t.entity->data.id);
            it.parent = static_cast<tree::ItemId>(t.entity->data.parent);
            it.name = t.entity->data.name;
            it.dimmed = !t.activeInHierarchy;
            out.push_back(std::move(it));
        }
    }

    std::wstring EmptyText() const override {
        return Tr(L"エンティティはまだありません。\nここを右クリックして「エンティティを作る」で作れます。");
    }

    bool CanRename(tree::ItemId id) const override { return scene_.Find(static_cast<int>(id)) != nullptr; }

    void Rename(tree::ItemId id, const std::string& name) override {
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

    bool CanMove() const override { return true; }

    void Move(std::span<const tree::ItemId> items, tree::ItemId parent, size_t index) override {
        const std::vector<int> ids = ToEntityIds(items);
        Edit(ids.size() == 1 ? std::format("「{}」を移動", NameOf(ids[0])) : std::format("{} 個のエンティティを移動", ids.size()),
             [&] { scene_.Move(ids, static_cast<int>(parent), index); });
    }

    void SelectionChanged(std::span<const tree::ItemId> ids) override {
        selection_.clear();
        for (const tree::ItemId id : ids) selection_.push_back(static_cast<int>(id));
        if (ids.size() == 1) {
            host_.Inspect(EntityInspectable(static_cast<int>(ids[0])));
            inspectingEntities_ = true;
        } else if (ids.size() > 1) {
            editor::Inspectable many;
            many.title = TrF(L"{} 個のエンティティを選んでいます。\nまとめての編集はこれからです。", ids.size());
            host_.Inspect(std::move(many));
            inspectingEntities_ = true;
        } else if (inspectingEntities_) {
            host_.Inspect({});
            inspectingEntities_ = false;
        }
    }

    std::vector<tree::Action> Actions(std::span<const tree::ItemId> selection) override {
        const std::vector<int> sel = ToEntityIds(selection);
        std::vector<tree::Action> list;

        tree::Action create;
        create.label = Tr(L"エンティティを作る");
        create.key = ui::Key::N;
        create.ctrl = create.shift = true;
        create.run = [this, sel] {
            // Next to the (last) selected entity, or at the end of the top level.
            int parent = 0;
            size_t index = Scene::kEnd;
            if (!sel.empty()) {
                if (const SceneEntity* e = scene_.Find(sel.back())) {
                    parent = e->data.parent;
                    const std::vector<int> siblings = scene_.Children(parent);
                    index = static_cast<size_t>(std::find(siblings.begin(), siblings.end(), sel.back()) - siblings.begin()) + 1;
                }
            }
            CreateEntity(parent, index);
        };
        list.push_back(std::move(create));

        tree::Action child;
        child.label = Tr(L"子エンティティを作る");
        child.enabled = sel.size() == 1;
        if (child.enabled) child.run = [this, sel] { CreateEntity(sel[0], Scene::kEnd); };
        list.push_back(std::move(child));

        if (sel.empty()) return list;
        tree::Action sep;
        sep.separator = true;
        list.push_back(sep);

        tree::Action dup;
        dup.label = Tr(L"複製");
        dup.key = ui::Key::D;
        dup.ctrl = true;
        dup.run = [this, sel] {
            std::vector<int> made;
            Edit(sel.size() == 1 ? std::format("「{}」を複製", NameOf(sel[0])) : std::format("{} 個のエンティティを複製", sel.size()),
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
                Edit(std::format("{} 個のエンティティを一番上に出す", scene_.Roots(sel).size()),
                     [&] { scene_.Move(sel, 0, Scene::kEnd); });
            };
        }
        list.push_back(std::move(unparent));

        list.push_back(sep);
        tree::Action del;
        del.label = Tr(L"削除");
        del.key = ui::Key::Delete;
        del.run = [this, sel] {
            Edit(sel.size() == 1 ? std::format("「{}」を削除", NameOf(sel[0])) : std::format("{} 個のエンティティを削除", sel.size()),
                 [&] { scene_.Delete(sel); });
        };
        list.push_back(std::move(del));
        return list;
    }

private:
    // The scene view comes from a plugin that knows a space (ゲーム制作 × 3D); without one, a note.
    void DrawSceneView(ui::Context& c, const Rect& r) {
        if (!sceneView_ && !sceneViewAsked_) {
            sceneViewAsked_ = true;
            const auto views = host_.Registry().Contributions<SceneViewInfo>(kSceneViewPoint);
            if (!views.empty() && views[0].item->create) sceneView_ = views[0].item->create();
            if (sceneView_) sceneView_->LoadState(view_.Get("sceneView"));
        }
        if (!sceneView_) {
            Placeholder(c, r, Tr(L"シーンビュー"),
                        Tr(L"3D 空間 / 2D 空間のプラグインが入ると、ここにシーンが映り、エンティティを置いて動かせます。"));
            return;
        }
        c.KeepClear(r);  // the picture of the scene: no theme ornament over it
        SceneViewHost h{host_, scene_, selection_,
                        [this](std::vector<int> ids) { host_.SelectInTree(std::vector<tree::ItemId>(ids.begin(), ids.end())); },
                        [this](int id, const std::string& type) { return ResolveComponent(id, type); }};
        sceneView_->Draw(c, r, h);
    }

    void InspectSettings() {
        inspectingEntities_ = false;
        host_.Inspect(editor::Inspectable::Single(
            reflect::TypeOf<GameSettings>(), [this] { return reflect::EditTarget{&settings_, &unknowns_}; }, Tr(L"ゲームの設定")));
        host_.SelectInTree({});
    }

    // Where an entity is now (the list may have been rebuilt by undo since): found by id each time.
    std::function<reflect::EditTarget()> ResolveEntity(int id) {
        return [this, id] {
            SceneEntity* e = scene_.Find(id);
            return e ? reflect::EditTarget{&e->data, &e->unknowns} : reflect::EditTarget{};
        };
    }

    std::function<reflect::EditTarget()> ResolveComponent(int id, std::string type) {
        return [this, id, type] {
            SceneEntity* e = scene_.Find(id);
            Component* c = e ? e->FindComponent(type) : nullptr;
            return c && c->Known() ? reflect::EditTarget{c->value.Get(), &c->unknowns} : reflect::EditTarget{};
        };
    }

    const ComponentInfo* InfoOf(const std::string& type) const {
        for (const ComponentInfo* i : componentInfos_) {
            if (i->type().name == type) return i;
        }
        return nullptr;
    }

    // The component `type` of the entity and of its parents that have it: the top parent's first.
    // Empty when the entity or its component is gone.
    std::vector<const void*> Chain(int id, const std::string& type) const {
        std::vector<const void*> chain;
        const SceneEntity* self = scene_.Find(id);
        const Component* own = self ? self->FindComponent(type) : nullptr;
        if (!own || !own->Known()) return chain;
        size_t steps = 0;  // a broken file could make a ring of parents
        for (const SceneEntity* e = self; e && steps++ <= scene_.Entities().size(); e = scene_.Find(e->data.parent)) {
            const Component* c = e->FindComponent(type);
            if (c && c->Known()) chain.push_back(c->value.Get());
        }
        std::reverse(chain.begin(), chain.end());
        return chain;
    }

    // The values a component's plugin works out through the parents: worked out again every time the
    // inspector asks, into a value of their own (edits of it go through EditDerived).
    std::function<reflect::EditTarget()> Derived(int id, std::string type, const ComponentInfo* info) {
        auto value = std::make_shared<reflect::Object>(info->derivedType());
        return [this, id, type = std::move(type), info, value] {
            const std::vector<const void*> chain = Chain(id, type);
            if (chain.empty()) return reflect::EditTarget{};  // gone (undo)
            info->derive(chain, value->Get());
            return reflect::EditTarget{value->Get(), nullptr};
        };
    }

    // A derived value typed in the inspector: the component's plugin works out the component that
    // gives it; the fields that change are edits of the component (one undo entry; typing one field
    // joins into one entry).
    void EditDerived(int id, const std::string& type, const ComponentInfo* info, editor::InspectBlock::Change ch) {
        const std::vector<const void*> chain = Chain(id, type);
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

    // The inspector of one entity: the entity itself, then its components (each with ⋮ → 外す), and
    // "部品を足す" under them. Asked again every time it is drawn (undo may add or take components).
    editor::Inspectable EntityInspectable(int id) {
        editor::Inspectable in;
        in.title = Tr(L"エンティティ");
        in.blocks = [this, id] {
            std::vector<editor::InspectBlock> blocks;
            const SceneEntity* e = scene_.Find(id);
            if (!e) return blocks;
            blocks.push_back({&reflect::TypeOf<Entity>(), ResolveEntity(id), {}, {}, {}});
            for (const Component& c : e->components) {
                editor::w::Action remove;
                const std::string type = c.type;
                const std::string label = c.Known() ? c.value.Type()->label : c.type;
                remove.label = Tr(L"外す");
                remove.run = [this, id, type, label] {
                    Edit(std::format("「{}」を外す", label), [&] { scene_.RemoveComponent(id, type); });
                };
                if (c.Known()) {
                    editor::InspectBlock b{c.value.Type(), ResolveComponent(id, c.type), Utf8ToWide(TrU8(label)), {}, {remove}};
                    // A child: ローカル / ワールド (or whatever the plugin calls them), one block shown either way.
                    // At the top level both ways are the same numbers, so there is no switch.
                    if (const ComponentInfo* info = InfoOf(c.type); info && info->HasDerived() && e->data.parent != 0) {
                        b.modes = {Tr(info->ownLabel), Tr(info->derivedLabel)};
                        b.mode = ModeOf(*e);
                        b.setMode = [this, id](int m) { modes_[id] = m; };
                        if (b.mode == 1) {
                            b.type = &info->derivedType();
                            b.resolve = Derived(id, c.type, info);
                            b.onEdit = [this, id, type, info](editor::InspectBlock::Change ch) { EditDerived(id, type, info, std::move(ch)); };
                        }
                    }
                    blocks.push_back(std::move(b));
                } else {
                    blocks.push_back({nullptr, {}, Utf8ToWide(label) + Tr(L" (使えない部品)"),
                                      Tr(L"この部品のプラグインが入っていないか、形が読めません。データはそのまま残り、保存しても消えません。"), {remove}});
                }
            }
            return blocks;
        };
        in.addLabel = Tr(L"＋ 部品を足す");
        in.addMenu = [this, id] {
            std::vector<editor::w::Action> list;
            const SceneEntity* e = scene_.Find(id);
            for (const reflect::TypeDesc* t : scene_.ComponentTypes()) {
                if (!e || e->FindComponent(t->name)) continue;
                editor::w::Action add;
                add.label = Utf8ToWide(TrU8(t->label));
                add.run = [this, id, t] {
                    Edit(std::format("「{}」を足す", t->label), [&] { scene_.AddComponent(id, *t); });
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

    std::string NameOf(int id) const {
        const SceneEntity* e = scene_.Find(id);
        return e ? e->data.name : std::string();
    }

    // Runs a change of the scene's shape and puts it in the history (edit = undo entry = unsaved).
    // A change that changes nothing leaves no entry.
    template <class Fn>
    void Edit(std::string label, Fn&& change) {
        json::Value before = scene_.Save();
        change();
        json::Value after = scene_.Save();
        if (before == after) return;
        host_.History().Push(std::make_unique<SceneCommand>(scene_, std::move(before), std::move(after), std::move(label)));
    }

    void CreateEntity(int parent, size_t index) {
        int made = 0;
        Edit("エンティティを作成", [&] {
            made = scene_.Create(TrU8("エンティティ"), parent, index);  // the name it starts with, in the language in use
            for (const reflect::TypeDesc* t : newEntityTypes_) scene_.AddComponent(made, *t);  // e.g. Transform
        });
        host_.SelectInTree({static_cast<tree::ItemId>(made)}, true);
    }

    editor::SoftwareHost& host_;
    GameSettings settings_;
    reflect::Unknowns unknowns_;  // keys / enum names of a newer version: written back unchanged
    Scene scene_;
    std::vector<int> selection_;  // the 階層's selection (shown in the scene view)
    std::unique_ptr<SceneView> sceneView_;
    bool sceneViewAsked_ = false;
    std::vector<const reflect::TypeDesc*> newEntityTypes_;  // components every new entity gets
    std::vector<const ComponentInfo*> componentInfos_;      // one per component type, as offered
    json::Value view_;  // UserSettings: how the user looks (the scene view's camera ...)
    std::unordered_map<int, int> modes_;  // entity id -> how its component with derived values is shown
    bool hadData_ = false;
    bool inspectingEntities_ = false;  // the inspector shows the 階層's selection (not the settings)
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
