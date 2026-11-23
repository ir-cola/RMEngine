#include "rm_engine/game_production/runtime/WorldSave.h"

#include "rm_engine/game_production/runtime/Components.h"

#include <algorithm>
#include <format>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rm::game_production {

namespace {

const std::string& SaveableName() {
    static const std::string kName = reflect::TypeOf<Saveable>().name;
    return kName;
}

const Saveable* MarkOf(const SceneEntity& e) {
    const Component* c = e.FindComponent(SaveableName());
    return c && c->Known() ? static_cast<const Saveable*>(c->value.Get()) : nullptr;
}

bool Contains(std::span<const std::string> list, const std::string& s) { return std::find(list.begin(), list.end(), s) != list.end(); }

int IntOf(const json::Value& v) {
    const int64_t i = v.AsInt(0);
    return i > 0 && i <= Scene::kMaxId ? static_cast<int>(i) : 0;
}

// ゲームの中で作った物 `e` の作り方 (docs/SAVE.md 3 章の "spawn")。
json::Value SpawnOf(const Scene& scene, const SceneEntity& e, int base) {
    json::Value s = json::Value::MakeObject();
    s.Set("name", e.data.name);
    if (e.prefab.Root()) {
        // ゲームの中で作った Prefab の中に入れ子になった Prefab: 外の Prefab を置き直すと一緒に来る。
        if (e.prefab.outer > 0) {
            const int outer = scene.PrefabRootOf(e.data.parent);
            if (outer >= base && outer != 0) {
                s.Set("of", outer);
                s.Set("key", e.prefab.outer);
                return s;
            }
        }
        s.Set("prefab", e.prefab.asset.guid);
        return s;
    }
    if (e.prefab.Linked()) {
        const int root = scene.PrefabRootOf(e.data.id);
        if (root != 0 && root != e.data.id && root >= base) {
            s.Set("of", root);
            s.Set("key", e.prefab.source);
            return s;
        }
    }
    return s;  // ふつうのエンティティ (rm::Spawn("名前")): 名前だけで作り直す
}

json::Value StateOf(const SceneEntity& e, const Saveable& mark, std::span<const std::string> placement) {
    json::Value state = json::Value::MakeObject();
    if (mark.placement || mark.everything) state.Set("parent", e.data.parent);
    if (mark.active || mark.everything) state.Set("active", e.data.active);
    if (mark.everything) {
        json::Value fields = reflect::Save(e.data, &e.unknowns);
        fields.Erase("id");
        fields.Erase("parent");
        state.Set("entity", std::move(fields));
    }
    json::Value comps = json::Value::MakeArray();
    for (const Component& c : e.components) {
        // 「セーブする」自身はいつも入れる (作り直した物が次のセーブにも入るように)。
        const bool take = c.type == SaveableName() || mark.everything || (mark.placement && Contains(placement, c.type));
        if (take) comps.Push(Scene::SaveComponent(c));
    }
    state.Set("components", std::move(comps));
    return state;
}

}  // namespace

std::vector<std::string> PlacementTypes(const RuntimeRegistry& registry) {
    std::vector<std::string> out;
    for (const auto& c : registry.Contributions<ComponentInfo>(kComponentPoint)) {
        if (!c.item->placement) continue;
        std::string name = c.item->type().name;
        if (!Contains(out, name)) out.push_back(std::move(name));
    }
    return out;
}

json::Value CaptureWorld(const Scene& scene, int base, std::span<const std::string> placement, const WorldScene& sceneName,
                         std::span<const int> leaving) {
    const std::unordered_set<int> gone = scene.Subtree(leaving);
    const std::vector<TreeEntry> tree = scene.TreeOrder();
    // 残す物と、ゲームの中で作った物の親 (それを作り直すために要る。値は当てない)。
    std::unordered_map<int, const Saveable*> marked;
    std::unordered_set<int> needed;
    for (const TreeEntry& t : tree) {
        if (gone.contains(t.entity->data.id)) continue;
        if (const Saveable* m = MarkOf(*t.entity)) {
            marked[t.entity->data.id] = m;
            needed.insert(t.entity->data.id);
        }
    }
    if (base > 0) {
        for (const auto& kv : marked) {
            size_t steps = 0;
            for (const SceneEntity* p = scene.Find(kv.first); p && steps++ <= scene.Entities().size(); p = scene.Find(p->data.parent)) {
                if (p->data.parent == 0) break;
                const int up = p->data.parent;
                if (up < base) break;
                needed.insert(up);
            }
        }
    }

    json::Value world = json::Value::MakeObject();
    world.Set("format", kWorldFormat);
    json::Value sc = json::Value::MakeObject();
    sc.Set("name", sceneName.name);
    sc.Set("id", sceneName.id);
    world.Set("scene", std::move(sc));
    world.Set("base", std::max(0, base));
    json::Value list = json::Value::MakeArray();
    for (const TreeEntry& t : tree) {
        const SceneEntity& e = *t.entity;
        if (!needed.contains(e.data.id)) continue;
        json::Value rec = json::Value::MakeObject();
        rec.Set("id", e.data.id);
        rec.Set("parent", e.data.parent);
        if (base > 0 && e.data.id >= base) rec.Set("spawn", SpawnOf(scene, e, base));
        if (const auto it = marked.find(e.data.id); it != marked.end()) rec.Set("state", StateOf(e, *it->second, placement));
        list.Push(std::move(rec));
    }
    world.Set("entities", std::move(list));
    return world;
}

WorldScene SavedScene(const json::Value& world) {
    const json::Value& s = world.Get("scene");
    return {s.Get("name").AsString(), s.Get("id").AsString()};
}

std::optional<size_t> FindSavedScene(const json::Value& world, std::span<const std::string> names, std::span<const std::string> ids) {
    const WorldScene s = SavedScene(world);
    if (!s.id.empty()) {
        for (size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == s.id) return i;
        }
    }
    if (!s.name.empty()) {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == s.name) return i;
        }
    }
    return std::nullopt;
}

WorldApplied ApplyWorld(Scene& scene, const json::Value& world, const PrefabReader& prefab, reflect::Warnings& warnings) {
    WorldApplied done;
    if (!world.IsObject()) {
        warnings.push_back("世界のセーブではありません");
        return done;
    }
    if (world.Get("format").AsInt(0) > kWorldFormat) warnings.push_back("新しい版のエンジンで書いた世界のセーブです (読める所だけ読みます)");
    const int base = IntOf(world.Get("base"));
    // ここで作る物の id は、セーブの id (base より小さいのはシーンの物) と重ならないように。
    scene.KeepNextId(base);

    std::unordered_map<int, int> now;            // セーブの id -> 今の id
    std::unordered_set<int> made;                // ここで作った物
    std::unordered_map<int, std::unordered_map<int, int>> members;  // 作り直した Prefab の一番上 -> (ファイルの中の id -> 今の id)
    auto nowOf = [&](int saved) -> std::optional<int> {
        if (saved == 0) return 0;
        if (const auto it = now.find(saved); it != now.end()) return it->second;
        if ((base == 0 || saved < base) && scene.Find(saved)) return saved;
        return std::nullopt;
    };
    auto membersOf = [&](int root) -> const std::unordered_map<int, int>& {
        auto [it, fresh] = members.try_emplace(root);
        if (fresh) {
            for (const int id : scene.PrefabMembers(root)) {
                const SceneEntity* e = scene.Find(id);
                if (!e || id == root) continue;
                it->second[e->prefab.Root() ? e->prefab.outer : e->prefab.source] = id;
            }
        }
        return it->second;
    };

    // 1. どの物か決める (シーンの物はその id、ゲームの中で作った物は作り直す)。木の順なので親が先。
    std::vector<std::pair<const json::Value*, int>> states;
    for (const json::Value& rec : world.Get("entities").Items()) {
        const int saved = IntOf(rec.Get("id"));
        if (saved == 0) continue;
        const json::Value* spawn = rec.Find("spawn");
        int id = 0;
        if (!spawn || !spawn->IsObject() || base == 0) {
            if (!scene.Find(saved)) {
                warnings.push_back(std::format("セーブにあるエンティティ (id {}) がシーンにありません。飛ばします", saved));
                continue;
            }
            id = saved;
        } else {
            const std::string name = spawn->Get("name").AsString();
            const std::optional<int> parent = nowOf(IntOf(rec.Get("parent")));
            if (!parent) warnings.push_back(std::format("ゲームの中で作った「{}」の親がありません。一番上に作ります", name));
            if (const json::Value* of = spawn->Find("of")) {
                const auto root = nowOf(IntOf(*of));
                static const std::unordered_map<int, int> kNone;
                const std::unordered_map<int, int>& m = root ? membersOf(*root) : kNone;
                const auto it = m.find(IntOf(spawn->Get("key")));
                if (!root || it == m.end()) {
                    warnings.push_back(std::format("ゲームの中で作った「{}」は、Prefab の中に見つからないので飛ばします", name));
                    continue;
                }
                id = it->second;
            } else if (const std::string& asset = spawn->Get("prefab").AsString(); !asset.empty()) {
                id = SpawnPrefab(scene, AssetRef{asset}, parent.value_or(0), prefab, warnings);
                if (!id) {
                    warnings.push_back(std::format("ゲームの中で作った「{}」の Prefab ({}) がゲームに無いので作れません", name, asset));
                    continue;
                }
                const int roots[] = {id};
                for (const int x : scene.Subtree(roots)) made.insert(x);
            } else {
                id = scene.Create(name.empty() ? "エンティティ" : name, parent.value_or(0));
                made.insert(id);
            }
        }
        now[saved] = id;
        if (const json::Value* state = rec.Find("state"); state && state->IsObject()) states.emplace_back(state, id);
    }
    done.made = static_cast<int>(made.size());

    // 部品の中のエンティティの参照: 作り直した物は新しい id、作り直さなかったゲームの中の物は 0。
    const auto refs = [&](uint64_t old) -> uint64_t {
        if (old == 0 || old > static_cast<uint64_t>(std::numeric_limits<int>::max())) return 0;
        const int o = static_cast<int>(old);
        if (const auto it = now.find(o); it != now.end()) return static_cast<uint64_t>(it->second);
        return base == 0 || o < base ? old : 0;
    };

    // 2. 値を当てる。
    for (const auto& [state, id] : states) {
        if (!scene.Find(id)) continue;
        const std::string name = scene.Find(id)->data.name;
        if (const json::Value* fields = state->Find("entity"); fields && fields->IsObject()) {
            json::Value f = *fields;
            f.Erase("id");
            f.Erase("parent");
            SceneEntity* e = scene.Find(id);
            reflect::Load(e->data, f, &e->unknowns, &warnings);
        }
        if (const json::Value* active = state->Find("active"); active && active->IsBool()) scene.Find(id)->data.active = active->AsBool();
        if (const json::Value* p = state->Find("parent")) {
            const std::optional<int> to = nowOf(IntOf(*p));
            if (!to) {
                warnings.push_back(std::format("「{}」の親 (id {}) がないので、親は変えません", name, IntOf(*p)));
            } else if (*to != scene.Find(id)->data.parent) {
                if (*to != 0 && scene.IsUnder(*to, id)) {
                    warnings.push_back(std::format("「{}」の親を自分の下にはできないので、親は変えません", name));
                } else {
                    const int ids[] = {id};
                    scene.Move(ids, *to, Scene::kEnd);
                }
            }
        }
        std::unordered_map<std::string, size_t> seen;  // 型 -> その型の何番目
        for (const json::Value& cj : state->Get("components").Items()) {
            const std::string type = cj.Get("type").AsString();
            const size_t nth = seen[type]++;
            const reflect::TypeDesc* t = reflect::FindTypeNamed(scene.ComponentTypes(), type);
            if (!t) {
                warnings.push_back(std::format("「{}」の部品「{}」は、このゲームに無いので飛ばします", name, type));
                continue;
            }
            Component c = scene.LoadComponent(cj, true, name, &warnings);
            if (!c.Known()) {
                warnings.push_back(std::format("「{}」の部品「{}」の値を読めないので飛ばします", name, type));
                continue;
            }
            reflect::MapEntityRefs(*c.value.Type(), c.value.Get(), refs);
            SceneEntity* e = scene.Find(id);
            Component* there = e->FindComponent(type, nth);
            if (!there) {
                // ゲームの中で足した部品 (か、作り直したふつうのエンティティ): 足して当てる。
                if (nth > 0 && !scene.AllowsMultiple(type)) {
                    warnings.push_back(std::format("「{}」の部品「{}」は 2 つ持てないので飛ばします", name, type));
                    continue;
                }
                while (e->CountComponents(type) <= nth && scene.AddComponent(id, *t)) {
                }
                there = e->FindComponent(type, nth);
            }
            if (!there || !there->Known()) {
                warnings.push_back(std::format("「{}」の部品「{}」に値を当てられないので飛ばします", name, type));
                continue;
            }
            *there = std::move(c);
        }
        ++done.restored;
    }

    // 3. ゲームの中で消した物: 「セーブする」が付いていて、セーブに無い物 (読んだシーンの物と、ここで
    // 作り直した Prefab の中の物)。セーブの後でシーンに足した物 (id が base 以上) は消さない。
    std::unordered_set<int> kept;
    for (const auto& [saved, id] : now) kept.insert(id);
    std::vector<int> drop;
    for (const SceneEntity& e : scene.Entities()) {
        const int id = e.data.id;
        if (kept.contains(id) || !MarkOf(e)) continue;
        if (made.contains(id) || (base > 0 && id < base)) drop.push_back(id);
    }
    if (!drop.empty()) done.removed = static_cast<int>(scene.Delete(drop));
    return done;
}

}  // namespace rm::game_production
