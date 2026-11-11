#include "rm_engine/game_production/runtime/Prefab.h"

#include <algorithm>
#include <format>
#include <unordered_set>

namespace rm::game_production {

namespace {

// The entity's own fields that are kept in a prefab (not the id and the parent: the scene's).
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

std::string PrefabName(std::string_view path) {
    const size_t slash = path.find_last_of("/\\");
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::string_view ext = kPrefabExtension;
    if (name.size() > ext.size() && name.substr(name.size() - ext.size()) == ext) name.remove_suffix(ext.size());
    return std::string(name);
}

json::Value MakePrefab(const Scene& scene, int root, std::unordered_map<int, int>* sources) {
    const SceneEntity* r = scene.Find(root);
    if (!r) return {};
    // Ids in the file: the sources of a placed prefab stay; everything else after them.
    std::unordered_map<int, int> kept;
    int next = 1;
    if (r->prefab.Root()) {
        for (const int m : scene.PrefabMembers(root)) {
            const int s = scene.Find(m)->prefab.source;
            kept[m] = s;
            next = std::max(next, s + 1);
        }
    }
    std::unordered_map<int, int> ids;  // scene id -> id in the file
    std::unordered_set<int> used;
    json::Value list = json::Value::MakeArray();
    for (const TreeEntry& t : scene.TreeOrder()) {  // parents first, siblings in order
        const SceneEntity& e = *t.entity;
        if (!scene.IsUnder(e.data.id, root)) continue;
        // Two of the same source (a broken file): the second gets its own.
        const auto k = kept.find(e.data.id);
        const int id = k != kept.end() && !used.contains(k->second) ? k->second : next++;
        used.insert(id);
        ids[e.data.id] = id;
        json::Value ej = scene.SaveEntity(e);
        ej.Erase("prefab");
        ej.Set("id", id);
        ej.Set("parent", e.data.id == root ? 0 : ids[e.data.parent]);
        list.Push(std::move(ej));
    }
    if (sources) *sources = std::move(ids);
    json::Value o = json::Value::MakeObject();
    o.Set("kind", kPrefabKind);
    o.Set("format", 1);
    o.Set("entities", std::move(list));
    return o;
}

json::Value Overrides(const Scene& scene, int root, const json::Value& prefab, std::span<const std::string> placement) {
    json::Value fields = json::Value::MakeArray(), added = json::Value::MakeArray(), removedComps = json::Value::MakeArray(),
                removedEntities = json::Value::MakeArray();
    std::unordered_map<int, const SceneEntity*> bySource;
    for (const int m : scene.PrefabMembers(root)) {
        const SceneEntity* e = scene.Find(m);
        bySource.try_emplace(e->prefab.source, e);
    }
    const reflect::TypeDesc& entityType = reflect::TypeOf<Entity>();
    const auto& list = prefab.Get("entities").Items();
    for (size_t i = 0; i < list.size(); ++i) {
        const json::Value& p = list[i];
        const int src = static_cast<int>(p.Get("id").AsInt(0));
        if (src <= 0) continue;
        // The prefab's first entity is the placed root, whatever its source says.
        const auto found = bySource.find(src);
        const SceneEntity* placed = i == 0 ? scene.Find(root) : found != bySource.end() && found->second->data.id != root ? found->second : nullptr;
        if (!placed) {
            removedEntities.Push(src);
            continue;
        }
        const SceneEntity& e = *placed;
        const bool isRoot = i == 0;
        const SceneEntity pe = scene.LoadEntity(p, nullptr);
        // The entity's own fields.
        for (const auto& f : entityType.fields) {
            if (!PrefabField(f)) continue;
            json::Value now = reflect::SaveField(f, &e.data, &e.unknowns);
            if (isRoot || now != reflect::SaveField(f, &pe.data, nullptr)) fields.Push(FieldOverride(src, "", 0, f.name, std::move(now)));
        }
        // The prefab's components: changed fields, or gone from the placed one.
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
            if (!ec->Known() || !pc.Known()) continue;  // a type not offered now: follows the prefab
            const bool always = isRoot && std::find(placement.begin(), placement.end(), pc.type) != placement.end();
            for (const auto& f : pc.value.Type()->fields) {
                if (f.transient) continue;
                json::Value now = reflect::SaveField(f, ec->value.Get(), &ec->unknowns);
                if (always || now != reflect::SaveField(f, pc.value.Get(), &pc.unknowns)) {
                    fields.Push(FieldOverride(src, pc.type, nth, f.name, std::move(now)));
                }
            }
        }
        // Components the placed one has and the prefab does not.
        for (const Component& ec : e.components) {
            if (pe.FindComponent(ec.type, e.NthOf(ec))) continue;
            json::Value o = json::Value::MakeObject();
            o.Set("source", src);
            o.Set("component", Scene::SaveComponent(ec));
            added.Push(std::move(o));
        }
    }
    json::Value o = json::Value::MakeObject();
    o.Set("fields", std::move(fields));
    o.Set("addedComponents", std::move(added));
    o.Set("removedComponents", std::move(removedComps));
    o.Set("removedEntities", std::move(removedEntities));
    return o;
}

size_t CountOverrides(const json::Value& overrides) {
    size_t n = 0;
    for (const char* key : {"fields", "addedComponents", "removedComponents", "removedEntities"}) n += overrides.Get(key).Items().size();
    return n;
}

// --- The scene's side ---------------------------------------------------------------------------

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
    for (const TreeEntry& t : TreeOrder()) {
        if (IsUnder(t.entity->data.id, root) && PrefabRootOf(t.entity->data.id) == root) out.push_back(t.entity->data.id);
    }
    return out;
}

std::vector<int> Scene::PrefabRoots(std::string_view path) const {
    std::vector<int> out;
    for (const TreeEntry& t : TreeOrder()) {
        const PrefabLink& l = t.entity->prefab;
        if (l.Root() && (path.empty() || l.path == path)) out.push_back(t.entity->data.id);
    }
    return out;
}

bool Scene::LinkPrefab(int root, const std::string& path, const std::unordered_map<int, int>& sources) {
    if (!Find(root) || path.empty()) return false;
    for (auto& e : entities_) {
        if (!IsUnder(e.data.id, root)) continue;
        const auto it = sources.find(e.data.id);
        if (it == sources.end() || it->second <= 0) continue;
        e.prefab = {};
        e.prefab.source = it->second;
        if (e.data.id == root) {
            e.prefab.path = path;
            e.prefab.overrides = json::Value::MakeObject();
        }
    }
    return Find(root)->prefab.Root();
}

bool Scene::UnlinkPrefab(int root) {
    const std::vector<int> members = PrefabMembers(root);
    if (members.empty()) return false;
    for (const int m : members) Find(m)->prefab = {};
    return true;
}

int Scene::PlacePrefab(const json::Value& prefab, const std::string& path, int parent, size_t index, reflect::Warnings* warnings) {
    if (!IsPrefab(prefab) || prefab.Get("entities").Items().empty() || path.empty()) return 0;
    std::unordered_map<int, int> idMap;
    const std::vector<int> roots = PasteList(prefab.Get("entities"), parent, index, warnings, &idMap);
    if (roots.empty()) return 0;
    for (const auto& [source, id] : idMap) {
        if (SceneEntity* e = Find(id)) {
            e->prefab = {};
            e->prefab.source = source;
        }
    }
    SceneEntity* r = Find(roots[0]);
    if (!r->prefab.Linked()) r->prefab.source = 1;  // a hand-written file without ids
    r->prefab.path = path;
    r->prefab.overrides = json::Value::MakeObject();
    return roots[0];
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
    const std::vector<int> members = PrefabMembers(root);
    const std::unordered_set<int> memberSet(members.begin(), members.end());
    std::unordered_map<int, int> idOf;  // source -> the scene id it had
    for (const int m : members) idOf.try_emplace(Find(m)->prefab.source, m);

    std::unordered_set<int> removedEntities;
    for (const auto& v : overrides.Get("removedEntities").Items()) removedEntities.insert(static_cast<int>(v.AsInt(0)));

    // Every entity of the prefab, parents first; one whose parent is not there (removed, or later in
    // a broken file) is left out with everything below it.
    std::vector<SceneEntity> built;
    std::unordered_map<int, int> builtId;  // source -> scene id
    for (size_t i = 0; i < list.size(); ++i) {
        const json::Value& p = list[i];
        const int src = static_cast<int>(p.Get("id").AsInt(0));
        const bool isRoot = i == 0;
        if (!isRoot && (src <= 0 || builtId.contains(src) || removedEntities.contains(src))) continue;
        const int parentSrc = static_cast<int>(p.Get("parent").AsInt(0));
        if (!isRoot && !builtId.contains(parentSrc)) continue;
        json::Value fields = p;
        fields.Erase("prefab");
        SceneEntity e = LoadEntity(fields, warnings);
        const auto had = idOf.find(src);
        e.data.id = isRoot ? root : had != idOf.end() && had->second != root ? had->second : nextId_++;
        e.data.parent = isRoot ? parent : builtId[parentSrc];
        e.prefab = {};
        e.prefab.source = src > 0 ? src : rootLink.source;
        if (isRoot) {
            e.prefab.path = rootLink.path;
            e.prefab.extra = rootLink.extra;
            e.prefab.overrides = overrides.IsObject() ? overrides : json::Value::MakeObject();
        }

        // What the placed one changed.
        const int key = e.prefab.source;
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
        std::sort(gone.begin(), gone.end(), [](const auto& a, const auto& b) { return a.second > b.second; });  // the later ones first
        for (const auto& [type, nth] : gone) {
            if (const Component* c = e.FindComponent(type, nth)) e.components.erase(e.components.begin() + (c - e.components.data()));
        }
        for (const auto& o : overrides.Get("addedComponents").Items()) {
            if (o.Get("source").AsInt(0) != key) continue;
            const json::Value& cj = o.Get("component");
            const std::string type = cj.Get("type").AsString();
            e.components.push_back(LoadComponent(cj, !e.FindComponent(type) || AllowsMultiple(type), e.data.name, warnings));
        }
        builtId[key] = e.data.id;
        built.push_back(std::move(e));
    }

    // Plain entities under an entity that went: under the root.
    std::unordered_set<int> builtIds;
    for (const auto& e : built) builtIds.insert(e.data.id);
    for (auto& e : entities_) {
        if (memberSet.contains(e.data.id)) continue;
        if (memberSet.contains(e.data.parent) && !builtIds.contains(e.data.parent)) {
            if (warnings) warnings->push_back(std::format("エンティティ「{}」の親が Prefab から無くなったので、Prefab の一番上の物の下に置きました", e.data.name));
            e.data.parent = root;
        }
    }
    std::erase_if(entities_, [&](const SceneEntity& e) { return memberSet.contains(e.data.id); });
    const size_t pos = std::min(InsertPosition(parent, siblingIndex), entities_.size());
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(pos), std::make_move_iterator(built.begin()),
                     std::make_move_iterator(built.end()));
    return true;
}

}  // namespace rm::game_production
