#include "rm_engine/game_production/editor/GameSoftware.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/reflect/Data.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <algorithm>
#include <format>

namespace rm::game_production {

namespace {

namespace theme = editor::theme;
namespace tree = editor::tree;
using ui::Rect;

constexpr char kSceneView[] = "scene_view";
constexpr char kGameView[] = "game_view";

void Placeholder(ui::Context& c, const Rect& r, const wchar_t* title, const wchar_t* body) {
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
            if (c.item->onNewEntities) newEntityTypes_.push_back(t);
        }
        scene_.SetComponentTypes(std::move(types));
    }

    // --- Software ---------------------------------------------------------------------------
    std::vector<editor::dock::PanelType> Panels() override {
        return {
            {kSceneView, L"シーン",
             [](ui::Context& c, const Rect& r) {
                 Placeholder(c, r, L"シーンビュー",
                             L"3D 空間 / 2D 空間のプラグインが入ると、ここにシーンが映り、エンティティを置いて動かせます。");
             }},
            {kGameView, L"ゲーム",
             [this](ui::Context& c, const Rect& r) {
                 Placeholder(c, r, L"ゲームビュー", L"Play を押すと、ゲームのカメラから見た画面がここに映ります。");
                 const Rect button{r.x + (r.w - 200) * 0.5f, r.y + r.h * 0.5f + 70, 200, 36};
                 if (editor::w::Button(c, "settings", button, L"ゲームの設定を開く")) InspectSettings();
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
        return L"エンティティはまだありません。\nここを右クリックして「エンティティを作る」で作れます。";
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
        if (ids.size() == 1) {
            host_.Inspect(EntityInspectable(static_cast<int>(ids[0])));
            inspectingEntities_ = true;
        } else if (ids.size() > 1) {
            editor::Inspectable many;
            many.title = std::format(L"{} 個のエンティティを選んでいます。\nまとめての編集はこれからです。", ids.size());
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
        create.label = L"エンティティを作る";
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
        child.label = L"子エンティティを作る";
        child.enabled = sel.size() == 1;
        if (child.enabled) child.run = [this, sel] { CreateEntity(sel[0], Scene::kEnd); };
        list.push_back(std::move(child));

        if (sel.empty()) return list;
        tree::Action sep;
        sep.separator = true;
        list.push_back(sep);

        tree::Action dup;
        dup.label = L"複製";
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
        unparent.label = L"親から外す (一番上に出す)";
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
        del.label = L"削除";
        del.key = ui::Key::Delete;
        del.run = [this, sel] {
            Edit(sel.size() == 1 ? std::format("「{}」を削除", NameOf(sel[0])) : std::format("{} 個のエンティティを削除", sel.size()),
                 [&] { scene_.Delete(sel); });
        };
        list.push_back(std::move(del));
        return list;
    }

private:
    void InspectSettings() {
        inspectingEntities_ = false;
        host_.Inspect(editor::Inspectable::Single(
            reflect::TypeOf<GameSettings>(), [this] { return reflect::EditTarget{&settings_, &unknowns_}; }, L"ゲームの設定"));
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

    // The inspector of one entity: the entity itself, then its components (each with ⋮ → 外す), and
    // "部品を足す" under them. Asked again every time it is drawn (undo may add or take components).
    editor::Inspectable EntityInspectable(int id) {
        editor::Inspectable in;
        in.title = L"エンティティ";
        in.blocks = [this, id] {
            std::vector<editor::InspectBlock> blocks;
            const SceneEntity* e = scene_.Find(id);
            if (!e) return blocks;
            blocks.push_back({&reflect::TypeOf<Entity>(), ResolveEntity(id), {}, {}, {}});
            for (const Component& c : e->components) {
                editor::w::Action remove;
                const std::string type = c.type;
                const std::string label = c.Known() ? c.value.Type()->label : c.type;
                remove.label = L"外す";
                remove.run = [this, id, type, label] {
                    Edit(std::format("「{}」を外す", label), [&] { scene_.RemoveComponent(id, type); });
                };
                if (c.Known()) {
                    blocks.push_back({c.value.Type(), ResolveComponent(id, c.type), Utf8ToWide(label), {}, {remove}});
                } else {
                    blocks.push_back({nullptr, {}, Utf8ToWide(label) + L" (使えない部品)",
                                      L"この部品のプラグインが入っていないか、形が読めません。データはそのまま残り、保存しても消えません。", {remove}});
                }
            }
            return blocks;
        };
        in.addLabel = L"＋ 部品を足す";
        in.addMenu = [this, id] {
            std::vector<editor::w::Action> list;
            const SceneEntity* e = scene_.Find(id);
            for (const reflect::TypeDesc* t : scene_.ComponentTypes()) {
                if (!e || e->FindComponent(t->name)) continue;
                editor::w::Action add;
                add.label = Utf8ToWide(t->label);
                add.run = [this, id, t] {
                    Edit(std::format("「{}」を足す", t->label), [&] { scene_.AddComponent(id, *t); });
                };
                list.push_back(std::move(add));
            }
            if (list.empty()) {
                editor::w::Action none;
                none.label = scene_.ComponentTypes().empty() ? L"足せる部品がありません (3D 空間 などのプラグインで増えます)"
                                                             : L"足せる部品は全部付いています";
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
            made = scene_.Create("エンティティ", parent, index);
            for (const reflect::TypeDesc* t : newEntityTypes_) scene_.AddComponent(made, *t);  // e.g. Transform
        });
        host_.SelectInTree({static_cast<tree::ItemId>(made)}, true);
    }

    editor::SoftwareHost& host_;
    GameSettings settings_;
    reflect::Unknowns unknowns_;  // keys / enum names of a newer version: written back unchanged
    Scene scene_;
    std::vector<const reflect::TypeDesc*> newEntityTypes_;  // components every new entity gets
    bool hadData_ = false;
    bool inspectingEntities_ = false;  // the inspector shows the 階層's selection (not the settings)
};

}  // namespace

void RegisterEditor(editor::PluginRegistry& registry) {
    editor::SoftwareType t;
    t.id = "game";
    t.title = L"ゲーム";
    t.create = [](editor::SoftwareHost& host) -> std::unique_ptr<editor::Software> {
        return std::make_unique<GameSoftware>(host);
    };
    registry.AddSoftware(std::move(t));
}

}  // namespace rm::game_production
