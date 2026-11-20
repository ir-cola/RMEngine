#include "rm_engine/game_production/runtime/Prefab.h"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace rm::game_production {

namespace {

// Prefab に残すエンティティ自身のフィールド (id と親は残さない: シーンのもの)。
bool PrefabField(const reflect::FieldDesc& f) {
    const std::string_view n = f.name;
    return !f.transient && n != "id" && n != "parent";
}

const reflect::FieldDesc* FieldNamed(const reflect::TypeDesc& t, std::string_view name) {
    for (const auto& f : t.fields) {
        if (f.name == name && !f.transient) return &f;
    }
    return nullptr;
}

// 置いた Prefab `root` のファイルでのエンティティの id: その source。入れ子の Prefab の中なら、
// その外側の id (source は、その Prefab 自身のファイルでの id)。
int KeyOf(const SceneEntity& e, int root) { return e.data.id != root && e.prefab.Root() ? e.prefab.outer : e.prefab.source; }

// `e` が `root` の中に入れ子になった Prefab の内側のときは true (置いた Prefab のルートが間にある)。
bool InsideNested(const Scene& scene, const SceneEntity& e, int root) {
    size_t steps = 0;
    for (const SceneEntity* p = scene.Find(e.data.parent); p && p->data.id != root && steps++ <= scene.Entities().size();
         p = scene.Find(p->data.parent)) {
        if (p->prefab.Root()) return true;
    }
    return false;
}

json::Value FieldOverride(int source, const std::string& component, size_t index, const char* field, json::Value value) {
    json::Value o = json::Value::MakeObject();
    o.Set("source", source);
    o.Set("component", component);
    o.Set("index", static_cast<int64_t>(index));
    o.Set("field", field);
    o.Set("value", std::move(value));
    return o;
}

}  // namespace

bool IsPrefab(const json::Value& v) { return v.IsObject() && v.Get("kind").AsString() == kPrefabKind; }
bool NewerPrefab(const json::Value& v) { return IsPrefab(v) && v.Get("format").AsInt(1) > kPrefabFormat; }

void FillNestedPrefabs(Scene& scene, int root, std::vector<std::string>& chain, const PrefabReader& read, reflect::Warnings& warnings,
                       const std::function<std::string(const AssetRef&)>& title) {
    if (chain.size() > kMaxPrefabNesting || !read) return;
    for (const int n : scene.NestedPrefabs(root)) {
        const SceneEntity* e = scene.Find(n);
        if (!e) continue;
        const AssetRef ref = e->prefab.asset;
        if (std::find(chain.begin(), chain.end(), ref.guid) != chain.end()) {
            warnings.push_back(std::format("Prefab「{}」が自分の中に入っているので、中を作りません", title ? title(ref) : ref.guid));
            continue;
        }
        const json::Value* prefab = read(ref);
        if (!prefab) continue;  // 保存されたとおり
        const json::Value copy = *prefab;
        const json::Value overrides = e->prefab.overrides;
        scene.ApplyPrefab(n, copy, overrides, &warnings);
        chain.push_back(ref.guid);
        FillNestedPrefabs(scene, n, chain, read, warnings, title);
        chain.pop_back();
    }
}

int SpawnPrefab(Scene& scene, const AssetRef& asset, int parent, const PrefabReader& read, reflect::Warnings& warnings) {
    const json::Value* prefab = read ? read(asset) : nullptr;
    if (!prefab) return 0;
    const json::Value copy = *prefab;
    const int made = scene.PlacePrefab(copy, asset, parent, Scene::kEnd, &warnings);
    if (!made) return 0;
    std::vector<std::string> chain{asset.guid};
    FillNestedPrefabs(scene, made, chain, read, warnings, {});
    return made;
}

std::string PrefabName(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::string_view ext = kPrefabExtension;
    if (name.size() > ext.size() && name.substr(name.size() - ext.size()) == ext) name.remove_suffix(ext.size());
    return std::string(name);
}

json::Value MakePrefab(const Scene& scene, int root, std::unordered_map<int, int>* sources, const json::Value* previous) {
    const SceneEntity* r = scene.Find(root);
    if (!r) return {};
    // ファイル内の id: 置いた Prefab の source はそのまま、ほかは そのあとに振る。
    std::unordered_map<int, int> kept;
    int next = 1;
    if (r->prefab.Root()) {
        for (const int m : scene.PrefabMembers(root)) {
            const int s = KeyOf(*scene.Find(m), root);
            kept[m] = s;
            next = std::max(next, s + 1);
        }
    }
    std::unordered_map<int, int> ids;  // シーンの id -> ファイル内の id
    std::unordered_set<int> used;
    std::vector<const SceneEntity*> inside;
    const std::unordered_set<int> under = scene.Subtree(std::span<const int>(&root, 1));
    for (const TreeEntry& t : scene.TreeOrder()) {  // 親が先、兄弟は順番どおり
        const SceneEntity& e = *t.entity;
        if (!under.contains(e.data.id) || InsideNested(scene, e, root)) continue;
        // 同じ source が 2 つ (壊れたファイル): 2 つ目には別の id を与える。
        const auto k = kept.find(e.data.id);
        const int id = k != kept.end() && !used.contains(k->second) ? k->second : next++;
        used.insert(id);
        ids[e.data.id] = id;
        inside.push_back(&e);
    }
    json::Value list = json::Value::MakeArray();
    for (const SceneEntity* ep : inside) {
        // ファイル内の id で持つエンティティ参照。Prefab の外のエンティティへの参照は消す (Prefab は
        // ほかのシーンに置かれる - 置いた側は上書きとして持ち続ける)。
        SceneEntity e = *ep;
        e.MapEntityRefs(ids, true);
        const int id = ids[e.data.id];
        json::Value ej = scene.SaveEntity(e);
        if (e.data.id != root && e.prefab.Root()) {
            // 中に置いた Prefab: ルートとして残し、自分のファイルにつなぎ、変えたもの (呼んだ側の
            // 最後の Overrides の時点) を持たせる。その中身はそのファイルから来る。
            json::Value link = e.prefab.extra.IsObject() ? e.prefab.extra : json::Value::MakeObject();  // (新しい版のキーは残る)
            link.Erase("outer");  // シーンのもの (このファイルでの id は "id")
            link.Erase("path");
            link.Set("source", e.prefab.source);
            link.Set("asset", e.prefab.asset.guid);
            link.Set("overrides", e.prefab.overrides.IsObject() ? e.prefab.overrides : json::Value::MakeObject());
            ej.Set("prefab", std::move(link));
        } else {
            ej.Erase("prefab");
        }
        ej.Set("id", id);
        ej.Set("parent", e.data.id == root ? 0 : ids[e.data.parent]);
        list.Push(std::move(ej));
    }
    if (sources) *sources = std::move(ids);
    json::Value o = previous && previous->IsObject() ? *previous : json::Value::MakeObject();  // ほかのキーは残る
    o.Set("kind", kPrefabKind);
    o.Set("format", kPrefabFormat);
    o.Set("entities", std::move(list));
    return o;
}

json::Value Overrides(const Scene& scene, int root, const json::Value& prefab, std::span<const std::string> placement) {
    json::Value fields = json::Value::MakeArray(), added = json::Value::MakeArray(), removedComps = json::Value::MakeArray(),
                removedEntities = json::Value::MakeArray(), componentOrder = json::Value::MakeArray(), childOrder = json::Value::MakeArray();
    std::unordered_map<int, const SceneEntity*> bySource;
    for (const int m : scene.PrefabMembers(root)) {
        const SceneEntity* e = scene.Find(m);
        bySource.try_emplace(KeyOf(*e, root), e);
    }
    const reflect::TypeDesc& entityType = reflect::TypeOf<Entity>();
    const auto& list = prefab.Get("entities").Items();
    std::unordered_map<int, int> toScene;  // ファイル内の id -> 置いたエンティティの id
    for (size_t i = 0; i < list.size(); ++i) {
        const int src = static_cast<int>(list[i].Get("id").AsInt(0));
        const auto found = bySource.find(src);
        if (i == 0 && src > 0) toScene[src] = root;
        else if (src > 0 && found != bySource.end() && found->second->data.id != root) toScene.try_emplace(src, found->second->data.id);
    }
    for (size_t i = 0; i < list.size(); ++i) {
        const json::Value& p = list[i];
        const int src = static_cast<int>(p.Get("id").AsInt(0));
        if (src <= 0) continue;
        // Prefab の最初のエンティティが、source に何と書いてあっても、置いたルートになる。
        const auto found = bySource.find(src);
        const SceneEntity* placed = i == 0 ? scene.Find(root) : found != bySource.end() && found->second->data.id != root ? found->second : nullptr;
        if (!placed) {
            removedEntities.Push(src);
            continue;
        }
        const SceneEntity& e = *placed;
        const bool isRoot = i == 0;
        SceneEntity pe = scene.LoadEntity(p, nullptr);
        pe.MapEntityRefs(toScene, true);  // (ファイル内の id での参照 -> 置いたエンティティの参照)
        // エンティティ自身のフィールド。
        for (const auto& f : entityType.fields) {
            if (!PrefabField(f)) continue;
            json::Value now = reflect::SaveField(f, &e.data, &e.unknowns);
            if (isRoot || now != reflect::SaveField(f, &pe.data, nullptr)) fields.Push(FieldOverride(src, "", 0, f.name, std::move(now)));
        }
        // Prefab の部品: 変えたフィールド、または置いた側では無くなったもの。
        for (const Component& pc : pe.components) {
            const size_t nth = pe.NthOf(pc);
            const Component* ec = e.FindComponent(pc.type, nth);
            if (!ec) {
                json::Value o = json::Value::MakeObject();
                o.Set("source", src);
                o.Set("component", pc.type);
                o.Set("index", static_cast<int64_t>(nth));
                removedComps.Push(std::move(o));
                continue;
            }
            if (!ec->Known() || !pc.Known()) continue;  // 今は出ていない型: Prefab に従う
            const bool always = isRoot && std::find(placement.begin(), placement.end(), pc.type) != placement.end();
            for (const auto& f : pc.value.Type()->fields) {
                if (f.transient) continue;
                json::Value now = reflect::SaveField(f, ec->value.Get(), &ec->unknowns);
                if (always || now != reflect::SaveField(f, pc.value.Get(), &pc.unknowns)) {
                    fields.Push(FieldOverride(src, pc.type, nth, f.name, std::move(now)));
                }
            }
        }
        // 置いた側にあって、Prefab に無い部品。
        for (const Component& ec : e.components) {
            if (pe.FindComponent(ec.type, e.NthOf(ec))) continue;
            json::Value o = json::Value::MakeObject();
            o.Set("source", src);
            o.Set("component", Scene::SaveComponent(ec));
            added.Push(std::move(o));
        }
        // 両方にある部品の順番。置いた側の順番が違うとき。
        auto key = [](const std::string& type, size_t nth) { return type + "#" + std::to_string(nth); };
        std::vector<std::string> mine, theirs;
        json::Value order = json::Value::MakeArray();
        for (const Component& ec : e.components) {
            const size_t nth = e.NthOf(ec);
            if (!pe.FindComponent(ec.type, nth)) continue;
            mine.push_back(key(ec.type, nth));
            json::Value k = json::Value::MakeObject();
            k.Set("component", ec.type);
            k.Set("index", static_cast<int64_t>(nth));
            order.Push(std::move(k));
        }
        for (const Component& pc : pe.components) {
            if (e.FindComponent(pc.type, pe.NthOf(pc))) theirs.push_back(key(pc.type, pe.NthOf(pc)));
        }
        if (mine != theirs) {
            json::Value o = json::Value::MakeObject();
            o.Set("source", src);
            o.Set("order", std::move(order));
            componentOrder.Push(std::move(o));
        }
        // Prefab の子の順番 (兄弟の順番)。違うとき。
        std::vector<int> placedKids, fileKids;
        for (const int c : scene.Children(e.data.id)) {
            const SceneEntity* ce = scene.Find(c);
            if (!ce->prefab.Linked() || (ce->prefab.Root() && ce->prefab.outer == 0)) continue;  // Prefab のものではない
            const int k = KeyOf(*ce, root);
            if (bySource.contains(k) && bySource.at(k) == ce) placedKids.push_back(k);
        }
        for (const json::Value& c : list) {
            const int k = static_cast<int>(c.Get("id").AsInt(0));
            if (&c != &list[0] && c.Get("parent").AsInt(0) == src && std::find(placedKids.begin(), placedKids.end(), k) != placedKids.end()) {
                fileKids.push_back(k);
            }
        }
        if (placedKids != fileKids) {
            json::Value o = json::Value::MakeObject();
            o.Set("source", src);
            json::Value kids = json::Value::MakeArray();
            for (const int k : placedKids) kids.Push(k);
            o.Set("children", std::move(kids));
            childOrder.Push(std::move(o));
        }
    }
    json::Value o = json::Value::MakeObject();
    o.Set("fields", std::move(fields));
    o.Set("addedComponents", std::move(added));
    o.Set("removedComponents", std::move(removedComps));
    o.Set("removedEntities", std::move(removedEntities));
    o.Set("componentOrder", std::move(componentOrder));
    o.Set("childOrder", std::move(childOrder));
    return o;
}

std::vector<std::string> NestedAssets(const json::Value& prefab) {
    std::vector<std::string> out;
    const auto& list = prefab.Get("entities").Items();
    for (size_t i = 1; i < list.size(); ++i) {
        const std::string& a = list[i].Get("prefab").Get("asset").AsString();
        if (!a.empty() && std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
    }
    return out;
}

size_t CountOverrides(const json::Value& overrides) {
    size_t n = 0;
    for (const char* key : {"fields", "addedComponents", "removedComponents", "removedEntities", "componentOrder", "childOrder"}) {
        n += overrides.Get(key).Items().size();
    }
    return n;
}

// --- シーン側 -----------------------------------------------------------------------------------

int Scene::PrefabRootOf(int id) const {
    const SceneEntity* e = Find(id);
    for (size_t steps = 0; e && e->prefab.Linked() && steps <= entities_.size(); ++steps) {
        if (e->prefab.Root()) return e->data.id;
        e = Find(e->data.parent);
    }
    return 0;
}

std::vector<int> Scene::PrefabMembers(int root) const {
    std::vector<int> out;
    const SceneEntity* r = Find(root);
    if (!r || !r->prefab.Root()) return out;
    const std::unordered_set<int> under = Subtree(std::span<const int>(&root, 1));
    for (const TreeEntry& t : TreeOrder()) {
        const SceneEntity& e = *t.entity;
        if (!under.contains(e.data.id)) continue;
        const bool own = PrefabRootOf(e.data.id) == root;
        const bool nested = e.data.id != root && e.prefab.Root() && e.prefab.outer > 0 && PrefabRootOf(e.data.parent) == root;
        if (own || nested) out.push_back(e.data.id);
    }
    return out;
}

std::vector<int> Scene::NestedPrefabs(int root) const {
    std::vector<int> out;
    for (const int m : PrefabMembers(root)) {
        if (m != root && Find(m)->prefab.Root()) out.push_back(m);
    }
    return out;
}

std::vector<int> Scene::PrefabRoots(std::string_view asset) const {
    std::vector<int> out;
    for (const TreeEntry& t : TreeOrder()) {
        const PrefabLink& l = t.entity->prefab;
        if (l.Root() && (asset.empty() || l.asset.guid == asset)) out.push_back(t.entity->data.id);
    }
    return out;
}

bool Scene::LinkPrefab(int root, const AssetRef& asset, const std::unordered_map<int, int>& sources) {
    SceneEntity* r = Find(root);
    if (!r || asset.Empty()) return false;
    // 反映 (同じ Prefab をもう一度): 入れ子の Prefab は、まわりの Prefab の中に残る。
    const int outer = r->prefab.Root() && r->prefab.asset == asset ? r->prefab.outer : 0;
    const std::unordered_set<int> under = Subtree(std::span<const int>(&root, 1));
    for (auto& e : entities_) {
        if (!under.contains(e.data.id)) continue;
        const auto it = sources.find(e.data.id);
        if (it == sources.end() || it->second <= 0) continue;
        if (e.data.id != root && e.prefab.Root()) {
            e.prefab.outer = it->second;  // 中に置いた Prefab: これで入れ子になる
            continue;
        }
        json::Value extra = std::move(e.prefab.extra);  // (新しい版のキーは残る)
        e.prefab = {};
        e.prefab.source = it->second;
        e.prefab.extra = std::move(extra);
    }
    r = Find(root);
    r->prefab.asset = asset;
    r->prefab.overrides = json::Value::MakeObject();
    // 別の Prefab の中に入れ子になっていた Prefab が、自分自身の Prefab になるとき、外側から抜ける
    // (手で置いたことになる。外へ動かしたときと同じ): 外側の Prefab のファイルだと、またもとの
    // Prefab にしてしまうため。すでにその Prefab であるものに反映したときは、そのまま残る。
    r->prefab.outer = outer;
    return r->prefab.Root();
}

bool Scene::UnlinkPrefab(int root) {
    const std::vector<int> members = PrefabMembers(root);
    if (members.empty()) return false;
    for (const int m : members) {
        SceneEntity* e = Find(m);
        if (m != root && e->prefab.Root()) {
            e->prefab.outer = 0;  // 入れ子の Prefab: 手で置いたことになる
        } else {
            e->prefab = {};
        }
    }
    return true;
}

int Scene::PlacePrefab(const json::Value& prefab, const AssetRef& asset, int parent, size_t index, reflect::Warnings* warnings) {
    if (!IsPrefab(prefab) || prefab.Get("entities").Items().empty() || asset.Empty()) return 0;
    std::unordered_map<int, int> idMap;
    const std::vector<int> roots = PasteList(prefab.Get("entities"), parent, index, warnings, &idMap);
    if (roots.empty()) return 0;
    for (const auto& [source, id] : idMap) {
        SceneEntity* e = Find(id);
        if (!e) continue;
        if (id != roots[0] && e->prefab.Root()) {
            e->prefab.outer = source;  // この中に入れ子になった Prefab (エディターが自分のファイルから埋める)
        } else {
            e->prefab = {};
            e->prefab.source = source;
        }
    }
    SceneEntity* r = Find(roots[0]);
    if (!r->prefab.Linked()) r->prefab.source = 1;  // id の無い手書きのファイル
    r->prefab.asset = asset;
    r->prefab.overrides = json::Value::MakeObject();
    r->prefab.outer = 0;
    return roots[0];
}

void Scene::DetachFromPrefab(int id) {
    const SceneEntity* e = Find(id);
    if (!e || !e->prefab.Linked()) return;
    if (e->prefab.Root()) {
        Find(id)->prefab.outer = 0;  // 入れ子の Prefab: 手で置いたことになる (自分自身の Prefab のまま)
        return;
    }
    // 置いた同じ Prefab のその下の部分と、その中に入れ子になった Prefab (先に求めておく)。
    const int around = PrefabRootOf(id);
    std::vector<int> plain, byHand;
    const std::unordered_set<int> under = Subtree(std::span<const int>(&id, 1));
    for (const auto& d : entities_) {
        if (!under.contains(d.data.id) || !d.prefab.Linked()) continue;
        if (!d.prefab.Root()) {
            if (PrefabRootOf(d.data.id) == around) plain.push_back(d.data.id);
        } else if (d.prefab.outer > 0 && PrefabRootOf(d.data.parent) == around) {
            byHand.push_back(d.data.id);
        }
    }
    for (const int p : plain) Find(p)->prefab = {};
    for (const int p : byHand) Find(p)->prefab.outer = 0;
}

void Scene::DetachCopies(const std::vector<int>& made) {
    const std::unordered_set<int> copied(made.begin(), made.end());
    std::vector<int> plain, byHand;
    for (const int id : made) {
        const SceneEntity* e = Find(id);
        if (!e || !e->prefab.Linked()) continue;
        if (e->prefab.Root()) {
            // まわりの Prefab を持たずにコピーした入れ子のルート。
            const int around = e->prefab.outer > 0 ? PrefabRootOf(e->data.parent) : 0;
            if (e->prefab.outer > 0 && !copied.contains(around)) byHand.push_back(id);
        } else if (!copied.contains(PrefabRootOf(id))) {
            plain.push_back(id);  // ルートを持たずにコピーした、置いた Prefab の一部
        }
    }
    for (const int id : plain) Find(id)->prefab = {};
    for (const int id : byHand) Find(id)->prefab.outer = 0;
}

bool Scene::ApplyPrefab(int root, const json::Value& prefab, const json::Value& overrides, reflect::Warnings* warnings) {
    SceneEntity* r = Find(root);
    if (!r || !r->prefab.Root() || !IsPrefab(prefab)) return false;
    const auto& list = prefab.Get("entities").Items();
    if (list.empty()) return false;
    const PrefabLink rootLink = r->prefab;
    const int parent = r->data.parent;
    size_t siblingIndex = 0;
    {
        const std::vector<int> siblings = Children(parent);
        siblingIndex = static_cast<size_t>(std::find(siblings.begin(), siblings.end(), root) - siblings.begin());
    }
    // ファイル内に同じ id の部分が 2 つ (部分が動かすと Prefab を抜けるようになる前のデータや、
    // 手で編集したファイル): 後のものは普通のエンティティとして Prefab を抜ける - 何も失われない。
    {
        std::unordered_set<int> keys;
        std::vector<int> clash;
        for (const int m : PrefabMembers(root)) {
            if (!keys.insert(KeyOf(*Find(m), root)).second) clash.push_back(m);
        }
        for (const int c : clash) {
            if (warnings) {
                warnings->push_back(std::format("エンティティ「{}」は Prefab の中の ID がほかと重なっているので、Prefab から外してふつうのエンティティにしました",
                                                Find(c)->data.name));
            }
            DetachFromPrefab(c);
        }
    }
    const std::vector<int> members = PrefabMembers(root);
    const std::unordered_set<int> memberSet(members.begin(), members.end());
    std::unordered_map<int, int> idOf;  // ファイル内の id -> それが持っていたシーンの id
    for (const int m : members) idOf.try_emplace(KeyOf(*Find(m), root), m);

    std::unordered_set<int> removedEntities;
    for (const auto& v : overrides.Get("removedEntities").Items()) removedEntities.insert(static_cast<int>(v.AsInt(0)));

    // Prefab のすべてのエンティティ。親が先。親がそこに無いもの (消された、または壊れたファイルで
    // あとにある) は、その下の全部といっしょに外す。
    // Prefab のすべてのエンティティのシーンの id を、どれも作る前に決めておく: それらの間の参照
    // (ファイル内の id) はこれになる。(使われずに終わる新しい id は、ただ飛ばす。)
    std::unordered_map<int, int> planned;  // source -> シーンの id
    for (size_t i = 0; i < list.size(); ++i) {
        const int src = static_cast<int>(list[i].Get("id").AsInt(0));
        if (src <= 0 || planned.contains(src)) continue;
        const auto had = idOf.find(src);
        planned[src] = i == 0 ? root : had != idOf.end() && had->second != root ? had->second : nextId_++;
    }

    std::vector<SceneEntity> built;
    std::unordered_map<int, int> builtId;  // ファイル内の id -> シーンの id
    std::vector<int> builtKeys;            // built[i] のファイル内の id
    for (size_t i = 0; i < list.size(); ++i) {
        const json::Value& p = list[i];
        const int src = static_cast<int>(p.Get("id").AsInt(0));
        const bool isRoot = i == 0;
        if (!isRoot && (src <= 0 || builtId.contains(src) || removedEntities.contains(src))) continue;
        const int parentSrc = static_cast<int>(p.Get("parent").AsInt(0));
        if (!isRoot && !builtId.contains(parentSrc)) continue;
        SceneEntity e = LoadEntity(p, warnings);  // ("prefab" つき: 入れ子のリンク)
        e.MapEntityRefs(planned, true);  // Prefab 自身のもの。下の上書きはもうシーンの id
        const auto had = idOf.find(src);
        const SceneEntity* before = had != idOf.end() ? Find(had->second) : nullptr;
        e.data.id = isRoot ? root : planned[src];
        e.data.parent = isRoot ? parent : builtId[parentSrc];
        const bool nested = !isRoot && e.prefab.Root();
        if (nested) {
            // この中に入れ子になった Prefab: 自分のリンク (ファイルから) - ただし前からあったものは、
            // ここで変えた内容を持ち続ける。
            if (before && before->prefab.Root() && before->prefab.asset == e.prefab.asset) {
                e.prefab.overrides = before->prefab.overrides;
                e.prefab.extra = before->prefab.extra;
            }
            e.prefab.outer = src;
        } else {
            e.prefab = {};
            e.prefab.source = src > 0 ? src : rootLink.source;
        }
        if (isRoot) {
            e.prefab.asset = rootLink.asset;
            e.prefab.extra = rootLink.extra;
            e.prefab.overrides = overrides.IsObject() ? overrides : json::Value::MakeObject();
            e.prefab.outer = rootLink.outer;
        }

        // 置いた側が変えたもの (ファイル内のエンティティの id で)。
        const int key = src > 0 ? src : rootLink.source;
        for (const auto& o : overrides.Get("fields").Items()) {
            if (o.Get("source").AsInt(0) != key) continue;
            const std::string& comp = o.Get("component").AsString();
            const std::string& name = o.Get("field").AsString();
            if (comp.empty()) {
                const reflect::FieldDesc* f = FieldNamed(reflect::TypeOf<Entity>(), name);
                if (f && PrefabField(*f)) reflect::LoadField(*f, &e.data, o.Get("value"), &e.unknowns, warnings);
            } else if (Component* c = e.FindComponent(comp, static_cast<size_t>(o.Get("index").AsInt(0))); c && c->Known()) {
                if (const reflect::FieldDesc* f = FieldNamed(*c->value.Type(), name)) {
                    reflect::LoadField(*f, c->value.Get(), o.Get("value"), &c->unknowns, warnings);
                }
            }
        }
        std::vector<std::pair<std::string, size_t>> gone;
        for (const auto& o : overrides.Get("removedComponents").Items()) {
            if (o.Get("source").AsInt(0) == key) gone.emplace_back(o.Get("component").AsString(), static_cast<size_t>(o.Get("index").AsInt(0)));
        }
        std::sort(gone.begin(), gone.end(), [](const auto& a, const auto& b) { return a.second > b.second; });  // あとのものを先に
        for (const auto& [type, nth] : gone) {
            if (const Component* c = e.FindComponent(type, nth)) e.components.erase(e.components.begin() + (c - e.components.data()));
        }
        for (const auto& o : overrides.Get("addedComponents").Items()) {
            if (o.Get("source").AsInt(0) != key) continue;
            const json::Value& cj = o.Get("component");
            const std::string type = cj.Get("type").AsString();
            e.components.push_back(LoadComponent(cj, !e.FindComponent(type) || AllowsMultiple(type), e.data.name, warnings));
        }
        // 置いた側の部品の順番 (持っている枠を、その順番で埋める)。
        for (const auto& o : overrides.Get("componentOrder").Items()) {
            if (o.Get("source").AsInt(0) != key) continue;
            std::vector<size_t> slots;
            std::vector<Component*> inOrder;
            for (const auto& k : o.Get("order").Items()) {
                Component* c = e.FindComponent(k.Get("component").AsString(), static_cast<size_t>(k.Get("index").AsInt(0)));
                if (!c || std::find(inOrder.begin(), inOrder.end(), c) != inOrder.end()) continue;
                inOrder.push_back(c);
                slots.push_back(static_cast<size_t>(c - e.components.data()));
            }
            std::vector<Component> moved;
            for (Component* c : inOrder) moved.push_back(std::move(*c));
            std::sort(slots.begin(), slots.end());
            for (size_t k = 0; k < slots.size(); ++k) e.components[slots[k]] = std::move(moved[k]);
        }
        builtId[key] = e.data.id;
        builtKeys.push_back(key);
        built.push_back(std::move(e));
    }
    // 置いた側の兄弟の順番 (リストの中で持っている場所を、その順番で埋める)。
    for (const auto& o : overrides.Get("childOrder").Items()) {
        const auto parentIt = builtId.find(static_cast<int>(o.Get("source").AsInt(0)));
        if (parentIt == builtId.end()) continue;
        std::vector<size_t> slots;
        std::vector<size_t> inOrder;
        for (const auto& k : o.Get("children").Items()) {
            for (size_t b = 0; b < built.size(); ++b) {
                if (builtKeys[b] == k.AsInt(0) && built[b].data.parent == parentIt->second &&
                    std::find(inOrder.begin(), inOrder.end(), b) == inOrder.end()) {
                    inOrder.push_back(b);
                    slots.push_back(b);
                    break;
                }
            }
        }
        std::vector<SceneEntity> moved;
        for (const size_t b : inOrder) moved.push_back(std::move(built[b]));
        std::vector<int> movedKeys;
        for (const size_t b : inOrder) movedKeys.push_back(builtKeys[b]);
        std::sort(slots.begin(), slots.end());
        for (size_t k = 0; k < slots.size(); ++k) {
            built[slots[k]] = std::move(moved[k]);
            builtKeys[slots[k]] = movedKeys[k];
        }
    }

    // 消えるもの: もう一度作られなかったメンバー - 中の全部といっしょに消えた入れ子の Prefab。
    std::unordered_set<int> builtIds;
    for (const auto& e : built) builtIds.insert(e.data.id);
    std::unordered_set<int> erase = memberSet;
    std::vector<int> goneNested;
    for (const int m : members) {
        if (!builtIds.contains(m) && m != root && Find(m)->prefab.Root()) goneNested.push_back(m);
    }
    if (!goneNested.empty()) {
        const std::unordered_set<int> inside = Subtree(goneNested);
        for (const auto& e : entities_) {
            if (e.prefab.Linked() && inside.contains(e.data.id)) erase.insert(e.data.id);
        }
    }
    // 消えたエンティティの下にいた普通のエンティティ: ルートの下へ。
    for (auto& e : entities_) {
        if (erase.contains(e.data.id)) continue;
        if (erase.contains(e.data.parent) && !builtIds.contains(e.data.parent)) {
            if (warnings) warnings->push_back(std::format("エンティティ「{}」の親が Prefab から無くなったので、Prefab の一番上の物の下に置きました", e.data.name));
            e.data.parent = root;
        }
    }
    std::erase_if(entities_, [&](const SceneEntity& e) { return erase.contains(e.data.id); });
    const size_t pos = std::min(InsertPosition(parent, siblingIndex), entities_.size());
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(pos), std::make_move_iterator(built.begin()),
                     std::make_move_iterator(built.end()));
    Touch();
    return true;
}

}  // namespace rm::game_production
