#include "rm_engine/game_production/runtime/Scene.h"

#include <algorithm>
#include <format>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace rm::game_production {

namespace {

constexpr size_t npos = static_cast<size_t>(-1);


}  // namespace

Component* SceneEntity::FindComponent(std::string_view type, size_t nth) {
    for (auto& c : components) {
        if (c.type == type && nth-- == 0) return &c;
    }
    return nullptr;
}

const Component* SceneEntity::FindComponent(std::string_view type, size_t nth) const {
    for (const auto& c : components) {
        if (c.type == type && nth-- == 0) return &c;
    }
    return nullptr;
}

size_t SceneEntity::CountComponents(std::string_view type) const {
    return static_cast<size_t>(std::count_if(components.begin(), components.end(), [&](const Component& c) { return c.type == type; }));
}

size_t SceneEntity::NthOf(const Component& c) const {
    size_t n = 0;
    for (const auto& o : components) {
        if (&o == &c) return n;
        if (o.type == c.type) ++n;
    }
    return n;
}

bool Scene::AllowsMultiple(std::string_view type) const {
    return std::any_of(multipleTypes_.begin(), multipleTypes_.end(), [&](const reflect::TypeDesc* t) { return t->name == type; });
}

bool Scene::AddComponent(int id, const reflect::TypeDesc& type) {
    if (const SceneEntity* e = Find(id); !e || (e->FindComponent(type.name) && !AllowsMultiple(type.name))) return false;
    Component c;
    c.type = type.name;
    c.value = reflect::Object(type);  // (a script's default value may make entities: looked up again after)
    SceneEntity* e = Find(id);
    if (!e) return false;
    e->components.push_back(std::move(c));
    return true;
}

bool Scene::RemoveComponent(int id, std::string_view type, size_t nth) {
    SceneEntity* e = Find(id);
    const Component* c = e ? e->FindComponent(type, nth) : nullptr;
    if (!c) return false;
    e->components.erase(e->components.begin() + (c - e->components.data()));
    return true;
}

bool Scene::MoveComponent(int id, std::string_view type, int by, size_t nth) {
    SceneEntity* e = Find(id);
    if (!e || (by != -1 && by != 1)) return false;
    auto& list = e->components;
    const Component* c = e->FindComponent(type, nth);
    if (!c) return false;
    const auto i = c - list.data();
    const auto j = i + by;
    if (j < 0 || j >= static_cast<std::ptrdiff_t>(list.size())) return false;
    std::swap(list[static_cast<size_t>(i)], list[static_cast<size_t>(j)]);
    return true;
}

void Scene::Reindex() const {
    index_.clear();
    index_.reserve(entities_.size());
    for (size_t i = 0; i < entities_.size(); ++i) index_.emplace(entities_[i].data.id, i);  // (the first of a repeated id)
    indexStale_ = false;
}

size_t Scene::IndexOf(int id) const {
    if (id <= 0) return npos;
    if (indexStale_) Reindex();
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto it = index_.find(id);
        if (it != index_.end() && it->second < entities_.size() && entities_[it->second].data.id == id) return it->second;
        // Out of date (a change that did not Touch, or a list of another size): once more, freshly.
        const bool stale = it != index_.end() || index_.size() != entities_.size();
        if (!stale || attempt == 1) break;
        Reindex();
    }
    return npos;
}

SceneEntity* Scene::Find(int id) {
    const size_t i = IndexOf(id);
    return i == npos ? nullptr : &entities_[i];
}

const SceneEntity* Scene::Find(int id) const {
    const size_t i = IndexOf(id);
    return i == npos ? nullptr : &entities_[i];
}

std::vector<int> Scene::Children(int parent) const {
    std::vector<int> out;
    for (const auto& e : entities_) {
        if (e.data.parent == parent) out.push_back(e.data.id);
    }
    return out;
}

std::unordered_set<int> Scene::Subtree(std::span<const int> roots) const {
    std::unordered_map<int, std::vector<int>> children;  // parent id -> ids
    for (const auto& e : entities_) children[e.data.parent].push_back(e.data.id);
    std::unordered_set<int> out;
    std::vector<int> todo;
    for (const int r : roots) {
        if (Find(r) && out.insert(r).second) todo.push_back(r);
    }
    while (!todo.empty()) {
        const int id = todo.back();
        todo.pop_back();
        const auto it = children.find(id);
        if (it == children.end()) continue;
        for (const int c : it->second) {
            if (out.insert(c).second) todo.push_back(c);
        }
    }
    return out;
}

bool Scene::IsUnder(int id, int ancestor) const {
    if (id <= 0 || ancestor <= 0) return false;
    int cur = id;
    for (size_t steps = 0; cur > 0 && steps <= entities_.size(); ++steps) {
        if (cur == ancestor) return true;
        const SceneEntity* e = Find(cur);
        if (!e) return false;
        cur = e->data.parent;
    }
    return false;
}

std::vector<TreeEntry> Scene::TreeOrder() const {
    std::unordered_map<int, std::vector<size_t>> children;  // parent id -> indices, in order
    for (size_t i = 0; i < entities_.size(); ++i) children[entities_[i].data.parent].push_back(i);
    std::vector<TreeEntry> out;
    out.reserve(entities_.size());
    struct Frame {
        size_t index;
        int depth;
        bool active;
    };
    std::vector<Frame> stack;
    auto pushChildren = [&](int parent, int depth, bool active) {
        const auto it = children.find(parent);
        if (it == children.end()) return;
        for (size_t k = it->second.size(); k-- > 0;) stack.push_back({it->second[k], depth, active});
    };
    pushChildren(0, 0, true);
    while (!stack.empty()) {
        const Frame f = stack.back();
        stack.pop_back();
        const SceneEntity& e = entities_[f.index];
        const bool active = f.active && e.data.active;
        out.push_back({&e, f.depth, active});
        pushChildren(e.data.id, f.depth + 1, active);
    }
    return out;
}

std::vector<int> Scene::Roots(std::span<const int> ids) const {
    // One pass in tree order (parents come first): an entity is covered when its parent is given or
    // covered.
    const std::unordered_set<int> given(ids.begin(), ids.end());
    std::unordered_set<int> covered;  // given, or below a given one
    std::vector<int> out;
    for (const TreeEntry& t : TreeOrder()) {
        const int id = t.entity->data.id;
        const bool under = covered.contains(t.entity->data.parent);
        const bool mine = given.contains(id);
        if (mine && !under) out.push_back(id);
        if (mine || under) covered.insert(id);
    }
    return out;
}

size_t Scene::InsertPosition(int parent, size_t index) const {
    // Before the index-th child of `parent`; after the last one (= the end of the list) otherwise.
    size_t seen = 0;
    for (size_t i = 0; i < entities_.size(); ++i) {
        if (entities_[i].data.parent != parent) continue;
        if (seen == index) return i;
        ++seen;
    }
    return entities_.size();
}

int Scene::Create(std::string name, int parent, size_t index) {
    if (parent != 0 && !Find(parent)) parent = 0;
    SceneEntity e;
    e.data.id = nextId_++;
    e.data.parent = parent;
    e.data.name = std::move(name);
    const size_t pos = InsertPosition(parent, index);
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(pos), std::move(e));
    Touch();
    return entities_[pos].data.id;
}

void SceneEntity::MapEntityRefs(const std::unordered_map<int, int>& to, bool clearOthers) {
    const auto map = [&](uint64_t id) -> uint64_t {
        const auto it = id <= static_cast<uint64_t>(std::numeric_limits<int>::max()) ? to.find(static_cast<int>(id)) : to.end();
        if (it != to.end()) return static_cast<uint64_t>(it->second);
        return clearOthers ? 0 : id;
    };
    reflect::MapEntityRefs(reflect::TypeOf<Entity>(), &data, map);
    for (Component& c : components) {
        if (c.Known()) reflect::MapEntityRefs(*c.value.Type(), c.value.Get(), map);  // (one not offered now stays as read)
    }
}

std::vector<int> Scene::Duplicate(std::span<const int> ids) {
    std::vector<int> made;
    for (const int root : Roots(ids)) {
        // The root and everything below it, in list order (so siblings keep their order).
        std::vector<SceneEntity> copies;
        std::unordered_map<int, int> newId;
        const std::unordered_set<int> inside = Subtree(std::span<const int>(&root, 1));
        for (const auto& e : entities_) {
            if (!inside.contains(e.data.id)) continue;
            SceneEntity c = e;
            newId[e.data.id] = c.data.id = nextId_++;
            copies.push_back(std::move(c));
        }
        for (auto& c : copies) {
            if (const auto it = newId.find(c.data.parent); it != newId.end()) c.data.parent = it->second;  // the root keeps its parent
            c.MapEntityRefs(newId, false);  // a reference inside the copied ones points at the copy
        }
        made.push_back(newId[root]);
        const size_t pos = IndexOf(root) + 1;
        entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(pos), std::make_move_iterator(copies.begin()),
                         std::make_move_iterator(copies.end()));
        Touch();
        std::vector<int> all;
        for (const auto& [from, to] : newId) all.push_back(to);
        DetachCopies(all);
    }
    return made;
}

size_t Scene::Delete(std::span<const int> ids) {
    const size_t before = entities_.size();
    const std::unordered_set<int> gone = Subtree(Roots(ids));
    std::erase_if(entities_, [&](const SceneEntity& e) { return gone.contains(e.data.id); });
    Touch();
    return before - entities_.size();
}

bool Scene::Move(std::span<const int> ids, int parent, size_t index) {
    if (parent != 0 && !Find(parent)) return false;
    const std::vector<int> roots = Roots(ids);
    if (roots.empty()) return false;
    for (const int r : roots) {
        if (IsUnder(parent, r)) return false;  // into itself
    }
    std::vector<std::pair<int, int>> was;  // (id, parent) in list order, to tell whether anything changed
    for (const auto& e : entities_) was.emplace_back(e.data.id, e.data.parent);
    // Parts of placed prefabs that go to another parent leave their prefab (worked out before anything moves).
    std::vector<int> leaving;
    for (const int r : roots) {
        const SceneEntity* e = Find(r);
        if (e->data.parent != parent && e->prefab.Linked() && (!e->prefab.Root() || e->prefab.outer > 0)) leaving.push_back(r);
    }
    for (const int r : leaving) DetachFromPrefab(r);

    std::vector<SceneEntity> taken;
    for (const int r : roots) {
        const size_t i = IndexOf(r);
        taken.push_back(std::move(entities_[i]));
        entities_.erase(entities_.begin() + static_cast<std::ptrdiff_t>(i));
        Touch();
        taken.back().data.parent = parent;
    }
    const size_t pos = InsertPosition(parent, index);
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(pos), std::make_move_iterator(taken.begin()),
                     std::make_move_iterator(taken.end()));
    Touch();

    std::vector<std::pair<int, int>> now;
    for (const auto& e : entities_) now.emplace_back(e.data.id, e.data.parent);
    // Only the order among siblings matters: compare per parent.
    auto siblings = [](const std::vector<std::pair<int, int>>& list) {
        std::unordered_map<int, std::vector<int>> m;
        for (const auto& [id, p] : list) m[p].push_back(id);
        return m;
    };
    return siblings(was) != siblings(now);
}

json::Value Scene::SaveComponent(const Component& c) {
    if (!c.Known()) return c.raw;  // a type not offered now: exactly as it was read
    json::Value cj = c.raw.IsObject() ? c.raw : json::Value::MakeObject();
    cj.Set("type", c.type);
    cj.Set("data", reflect::Save(*c.value.Type(), c.value.Get(), &c.unknowns));
    return cj;
}

Component Scene::LoadComponent(const json::Value& cj, bool allowed, const std::string& entityName, reflect::Warnings* warnings) const {
    auto warn = [&](std::string msg) {
        if (warnings) warnings->push_back(std::move(msg));
    };
    Component c = ReadComponent(cj, componentTypes_, allowed, warnings);
    if (c.Known()) return c;
    if (!reflect::FindTypeNamed(componentTypes_, c.type)) {
        warn(std::format("エンティティ「{}」の部品「{}」は、いま入っているプラグインでは使えないので、そのまま残します", entityName, c.type));
    } else {
        warn(std::format("エンティティ「{}」の部品「{}」は読めない (2 つ目か形が違う) ので、そのまま残します", entityName, c.type));
    }
    return c;
}

Component Scene::ReadComponent(const json::Value& cj, std::span<const reflect::TypeDesc* const> types, bool allowed, reflect::Warnings* warnings) {
    Component c;
    c.type = cj.Get("type").AsString();
    c.raw = cj;
    const reflect::TypeDesc* t = reflect::FindTypeNamed(types, c.type);
    const json::Value& data = cj.Get("data");
    if (t && data.IsObject() && allowed) {
        c.value = reflect::Object(*t);
        reflect::Load(*t, c.value.Get(), data, &c.unknowns, warnings);
        c.raw.Erase("data");  // written again from the value
    }
    return c;
}

// --- The scene's own values -------------------------------------------------------------------

void Scene::SetSettingTypes(std::vector<const reflect::TypeDesc*> types) {
    settingTypes_ = std::move(types);
    // Read again with the new list: a setting kept as it was read may be known now (and one known
    // before but not offered now goes back to being kept).
    std::vector<Component> was = std::move(settings_);
    settings_.clear();
    for (const Component& c : was) {
        const json::Value saved = SaveComponent(c);
        if (std::none_of(settings_.begin(), settings_.end(), [&](const Component& s) { return s.type == c.type; })) {
            settings_.push_back(ReadComponent(saved, settingTypes_, true, nullptr));
        }
    }
    AddMissingSettings();
}

void Scene::AddMissingSettings() {
    for (const reflect::TypeDesc* t : settingTypes_) {
        if (FindSetting(t->name)) continue;
        Component c;
        c.type = t->name;
        c.value = reflect::Object(*t);
        settings_.push_back(std::move(c));
    }
}

Component* Scene::FindSetting(std::string_view type) {
    for (Component& c : settings_) {
        if (c.type == type) return &c;
    }
    return nullptr;
}

const Component* Scene::FindSetting(std::string_view type) const { return const_cast<Scene*>(this)->FindSetting(type); }

json::Value Scene::SaveEntity(const SceneEntity& e) const {
    json::Value ej = reflect::Save(e.data, &e.unknowns);
    json::Value comps = json::Value::MakeArray();
    for (const Component& c : e.components) comps.Push(SaveComponent(c));
    ej.Set("components", std::move(comps));
    if (e.prefab.Linked()) {
        json::Value link = e.prefab.extra.IsObject() ? e.prefab.extra : json::Value::MakeObject();
        link.Set("source", e.prefab.source);
        link.Erase("path");  // the form from before assets had ids: written as "asset" now
        if (e.prefab.Root()) {
            link.Set("asset", e.prefab.asset.guid);
            if (e.prefab.overrides.IsObject()) link.Set("overrides", e.prefab.overrides);
        } else {
            link.Erase("asset");
            link.Erase("overrides");
        }
        if (e.prefab.Root() && e.prefab.outer > 0) {
            link.Set("outer", e.prefab.outer);
        } else {
            link.Erase("outer");
        }
        ej.Set("prefab", std::move(link));
    }
    return ej;
}

std::vector<Scene::EntityState> Scene::CaptureStates(std::span<const int> ids) const {
    std::vector<EntityState> out;
    out.reserve(ids.size());
    for (const int id : ids) {
        EntityState s;
        s.id = id;
        if (const size_t i = IndexOf(id); i != npos) {
            s.present = true;
            s.index = i;
            s.data = SaveEntity(entities_[i]);
        }
        out.push_back(std::move(s));
    }
    return out;
}

void Scene::RestoreStates(std::span<const int> ids, std::span<const EntityState> states) {
    const std::unordered_set<int> taken(ids.begin(), ids.end());
    std::erase_if(entities_, [&](const SceneEntity& e) { return taken.contains(e.data.id); });
    std::vector<const EntityState*> back;
    for (const EntityState& s : states) {
        if (s.present) back.push_back(&s);
    }
    // In the order of their places: each goes where it was, the ones before it being back already.
    std::sort(back.begin(), back.end(), [](const EntityState* a, const EntityState* b) { return a->index < b->index; });
    for (const EntityState* s : back) {
        SceneEntity e = LoadEntity(s->data, nullptr);
        e.data.id = s->id;
        entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(std::min(s->index, entities_.size())), std::move(e));
    }
    Touch();
}

SceneEntity Scene::LoadEntity(const json::Value& item, reflect::Warnings* warnings) const {
    SceneEntity e;
    // "components" and "prefab" are the scene's to read (not unknown keys of the entity).
    json::Value fields = item;
    json::Value comps;
    if (json::Value* cs = fields.Find("components")) comps = std::move(*cs);
    fields.Erase("components");
    if (const json::Value* link = fields.Find("prefab"); link && link->IsObject() && link->Get("source").AsInt(0) > 0) {
        e.prefab.extra = *link;
        e.prefab.source = static_cast<int>(link->Get("source").AsInt(0));
        // "asset": the prefab file's id; "path": a file from before assets had ids (read the same way).
        e.prefab.asset.guid = link->Find("asset") ? link->Get("asset").AsString() : link->Get("path").AsString();
        if (e.prefab.Root()) {
            e.prefab.overrides = link->Get("overrides");
            e.prefab.outer = std::max(0, static_cast<int>(link->Get("outer").AsInt(0)));
        }
        fields.Erase("prefab");
    }
    reflect::Load(e.data, fields, &e.unknowns, warnings);
    for (const auto& cj : comps.Items()) {
        const std::string type = cj.Get("type").AsString();
        const bool allowed = !e.FindComponent(type) || AllowsMultiple(type);
        e.components.push_back(LoadComponent(cj, allowed, e.data.name, warnings));
    }
    return e;
}

json::Value Scene::Save() const {
    json::Value o = extra_;
    o.Set("nextId", nextId_);
    json::Value list = json::Value::MakeArray();
    for (const auto& e : entities_) list.Push(SaveEntity(e));
    o.Set("entities", std::move(list));
    if (settings_.empty()) {
        o.Erase("settings");
    } else {
        json::Value settings = json::Value::MakeArray();
        for (const Component& c : settings_) settings.Push(SaveComponent(c));
        o.Set("settings", std::move(settings));
    }
    return o;
}

namespace {
constexpr char kClipKind[] = "rm_engine.game_production.entities";
}  // namespace

bool Scene::IsClip(const json::Value& clip) { return clip.IsObject() && clip.Get("kind").AsString() == kClipKind; }

json::Value Scene::Copy(std::span<const int> ids) const {
    const std::unordered_set<int> inside = Subtree(Roots(ids));
    json::Value list = json::Value::MakeArray();
    for (const auto& e : entities_) {  // list order: siblings keep their order
        if (inside.contains(e.data.id)) list.Push(SaveEntity(e));
    }
    json::Value o = json::Value::MakeObject();
    o.Set("kind", kClipKind);
    o.Set("format", 1);
    o.Set("entities", std::move(list));
    return o;
}

std::vector<int> Scene::Paste(const json::Value& clip, int parent, size_t index, reflect::Warnings* warnings) {
    if (!IsClip(clip)) return {};
    std::unordered_map<int, int> idMap;
    std::vector<int> roots = PasteList(clip.Get("entities"), parent, index, warnings, &idMap);
    std::vector<int> all;
    for (const auto& [from, to] : idMap) all.push_back(to);
    for (const int r : roots) all.push_back(r);  // (an entity without an id of its own is in no map)
    DetachCopies(all);
    return roots;
}

std::vector<int> Scene::PasteList(const json::Value& entities, int parent, size_t index, reflect::Warnings* warnings,
                                  std::unordered_map<int, int>* idMap) {
    std::vector<int> roots;
    if (parent != 0 && !Find(parent)) parent = 0;
    // New ids; a parent inside the clip is followed only when it came before (so there is never a
    // loop) - anything else is a root, placed at `parent`.
    std::unordered_map<int, int> newId;
    std::vector<SceneEntity> copies;
    for (const auto& item : entities.Items()) {
        if (!item.IsObject()) continue;
        SceneEntity e = LoadEntity(item, warnings);
        const int oldId = e.data.id;
        const auto p = newId.find(e.data.parent);
        e.data.id = nextId_++;
        if (oldId > 0 && !newId.contains(oldId)) newId[oldId] = e.data.id;
        if (p != newId.end() && e.data.parent != oldId) {
            e.data.parent = p->second;
        } else {
            e.data.parent = parent;
            roots.push_back(e.data.id);
        }
        copies.push_back(std::move(e));
    }
    // References to what was copied point at the copies; to anything else they stay (pasted in the
    // same scene: still that entity) - a prefab's point only inside it (the others are 0 in its file).
    for (SceneEntity& e : copies) e.MapEntityRefs(newId, false);
    const size_t pos = InsertPosition(parent, index);
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(std::min(pos, entities_.size())), std::make_move_iterator(copies.begin()),
                     std::make_move_iterator(copies.end()));
    Touch();
    if (idMap) *idMap = std::move(newId);
    return roots;
}

void Scene::Load(const json::Value& v, reflect::Warnings* warnings) {
    auto warn = [&](std::string msg) {
        if (warnings) warnings->push_back(std::move(msg));
    };
    entities_.clear();
    Touch();
    extra_ = json::Value::MakeObject();
    nextId_ = 1;
    settings_.clear();
    if (!v.IsObject()) {
        AddMissingSettings();
        return;
    }
    extra_ = v;  // every key in its place; Save replaces "nextId", "entities" and "settings" in place
    for (const auto& item : v.Get("entities").Items()) entities_.push_back(LoadEntity(item, warnings));
    Touch();
    // The scene's own values: the first of each type (a type not offered now is kept as read, quietly:
    // a 2D game's scene keeps the 3D sky it does not use).
    for (const auto& item : v.Get("settings").Items()) {
        if (!item.IsObject() || FindSetting(item.Get("type").AsString())) continue;
        settings_.push_back(ReadComponent(item, settingTypes_, true, warnings));
    }
    AddMissingSettings();

    // Ids: positive and unique. Repeated or missing ones get new ids (never one in use).
    // (An id or a nextId too big to count on from - a broken file - is not used: no overflow.)
    int maxId = 0;
    for (const auto& e : entities_) {
        if (e.data.id <= kMaxId) maxId = std::max(maxId, e.data.id);
    }
    const int64_t fileNext = v.Get("nextId").AsInt(1);
    nextId_ = std::max(fileNext >= 1 && fileNext <= kMaxId ? static_cast<int>(fileNext) : 1, maxId + 1);
    std::unordered_set<int> seen;
    for (auto& e : entities_) {
        if (e.data.id > 0 && e.data.id <= kMaxId && seen.insert(e.data.id).second) continue;
        const int fresh = nextId_++;
        warn(std::format("エンティティ「{}」の ID ({}) が使えないので {} にしました", e.data.name, e.data.id, fresh));
        e.data.id = fresh;
        seen.insert(fresh);
    }
    Touch();  // the ids repaired
    // Parents: existing, and no loops.
    for (auto& e : entities_) {
        if (e.data.parent == 0) continue;
        if (e.data.parent == e.data.id || !seen.contains(e.data.parent)) {
            warn(std::format("エンティティ「{}」の親 ({}) が見つからないので、一番上に置きました", e.data.name, e.data.parent));
            e.data.parent = 0;
        }
    }
    for (auto& e : entities_) {
        // Walk up; coming back to itself (or walking too long) is a loop.
        int cur = e.data.parent;
        bool loop = false;
        for (size_t steps = 0; cur > 0; ++steps) {
            if (cur == e.data.id || steps > entities_.size()) {
                loop = true;
                break;
            }
            const SceneEntity* p = Find(cur);
            cur = p ? p->data.parent : 0;
        }
        if (loop) {
            warn(std::format("エンティティ「{}」の親が輪になっていたので、一番上に置きました", e.data.name));
            e.data.parent = 0;
        }
    }
}

// --- What a project uses (Components.h) -------------------------------------------------------

void ComponentTypesIn(const json::Value& sceneOrPrefab, std::vector<std::string>& out) {
    auto add = [&](const json::Value& component) {
        const std::string& type = component.Get("type").AsString();
        if (!type.empty() && std::find(out.begin(), out.end(), type) == out.end()) out.push_back(type);
    };
    for (const auto& e : sceneOrPrefab.Get("entities").Items()) {
        for (const auto& c : e.Get("components").Items()) add(c);
        for (const auto& a : e.Get("prefab").Get("overrides").Get("addedComponents").Items()) add(a.Get("component"));
    }
}

std::vector<PluginId> PluginsOffering(const std::vector<std::string>& used, const std::vector<Contributed<ComponentInfo>>& offered) {
    std::vector<PluginId> out;
    for (const auto& c : offered) {
        if (std::find(used.begin(), used.end(), c.item->type().name) == used.end()) continue;
        if (std::find(out.begin(), out.end(), c.plugin) == out.end()) out.push_back(c.plugin);
    }
    return out;
}

namespace {

// The field `f` names: its description and the struct holding it (field null: not there).
reflect::PathTarget SceneFieldTarget(Scene& scene, const SceneField& f) {
    SceneEntity* e = scene.Find(f.entity);
    if (!e) return {};
    if (f.component.empty()) return reflect::Resolve(reflect::TypeOf<Entity>(), &e->data, &e->unknowns, f.path);
    Component* c = e->FindComponent(f.component, f.nth);
    if (!c || !c->Known()) return {};
    return reflect::Resolve(*c->value.Type(), c->value.Get(), &c->unknowns, f.path);
}

}  // namespace

std::optional<json::Value> ReadSceneField(Scene& scene, const SceneField& f) {
    const reflect::PathTarget t = SceneFieldTarget(scene, f);
    if (!t.field) return std::nullopt;
    return reflect::SaveField(*t.field, t.owner, t.unknowns);
}

bool WriteSceneField(Scene& scene, const SceneField& f, const json::Value& value, reflect::Warnings* warnings) {
    const reflect::PathTarget t = SceneFieldTarget(scene, f);
    if (!t.field) return false;
    reflect::LoadField(*t.field, t.owner, value, t.unknowns, warnings);
    return true;
}

}  // namespace rm::game_production

