#include "rm_engine/game_production/runtime/Scene.h"

#include <algorithm>
#include <format>
#include <unordered_map>
#include <unordered_set>

namespace rm::game_production {

namespace {

constexpr size_t npos = static_cast<size_t>(-1);

bool Has(std::span<const int> ids, int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

}  // namespace

Component* SceneEntity::FindComponent(std::string_view type) {
    for (auto& c : components) {
        if (c.type == type) return &c;
    }
    return nullptr;
}

const Component* SceneEntity::FindComponent(std::string_view type) const {
    for (const auto& c : components) {
        if (c.type == type) return &c;
    }
    return nullptr;
}

bool Scene::AddComponent(int id, const reflect::TypeDesc& type) {
    SceneEntity* e = Find(id);
    if (!e || e->FindComponent(type.name)) return false;
    Component c;
    c.type = type.name;
    c.value = reflect::Object(type);
    e->components.push_back(std::move(c));
    return true;
}

bool Scene::RemoveComponent(int id, std::string_view type) {
    SceneEntity* e = Find(id);
    if (!e) return false;
    const size_t before = e->components.size();
    std::erase_if(e->components, [&](const Component& c) { return c.type == type; });
    return e->components.size() != before;
}

bool Scene::MoveComponent(int id, std::string_view type, int by) {
    SceneEntity* e = Find(id);
    if (!e || (by != -1 && by != 1)) return false;
    auto& list = e->components;
    const auto it = std::find_if(list.begin(), list.end(), [&](const Component& c) { return c.type == type; });
    if (it == list.end()) return false;
    const auto i = it - list.begin();
    const auto j = i + by;
    if (j < 0 || j >= static_cast<std::ptrdiff_t>(list.size())) return false;
    std::swap(list[static_cast<size_t>(i)], list[static_cast<size_t>(j)]);
    return true;
}

size_t Scene::IndexOf(int id) const {
    if (id <= 0) return npos;
    for (size_t i = 0; i < entities_.size(); ++i) {
        if (entities_[i].data.id == id) return i;
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
    std::vector<int> out;
    for (const TreeEntry& t : TreeOrder()) {
        const int id = t.entity->data.id;
        if (!Has(ids, id)) continue;
        bool covered = false;
        for (int p = t.entity->data.parent; p > 0 && !covered;) {
            covered = Has(ids, p);
            const SceneEntity* pe = Find(p);
            p = pe ? pe->data.parent : 0;
        }
        if (!covered) out.push_back(id);
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
    return entities_[pos].data.id;
}

std::vector<int> Scene::Duplicate(std::span<const int> ids) {
    std::vector<int> made;
    for (const int root : Roots(ids)) {
        // The root and everything below it, in list order (so siblings keep their order).
        std::vector<SceneEntity> copies;
        std::unordered_map<int, int> newId;
        for (const auto& e : entities_) {
            if (!IsUnder(e.data.id, root)) continue;
            SceneEntity c = e;
            newId[e.data.id] = c.data.id = nextId_++;
            copies.push_back(std::move(c));
        }
        for (auto& c : copies) {
            if (const auto it = newId.find(c.data.parent); it != newId.end()) c.data.parent = it->second;  // the root keeps its parent
        }
        made.push_back(newId[root]);
        const size_t pos = IndexOf(root) + 1;
        entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(pos), std::make_move_iterator(copies.begin()),
                         std::make_move_iterator(copies.end()));
    }
    return made;
}

size_t Scene::Delete(std::span<const int> ids) {
    const size_t before = entities_.size();
    const std::vector<int> roots = Roots(ids);
    std::vector<int> gone;
    for (const auto& e : entities_) {
        for (const int r : roots) {
            if (IsUnder(e.data.id, r)) {
                gone.push_back(e.data.id);
                break;
            }
        }
    }
    std::erase_if(entities_, [&](const SceneEntity& e) { return Has(gone, e.data.id); });
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

    std::vector<SceneEntity> taken;
    for (const int r : roots) {
        const size_t i = IndexOf(r);
        taken.push_back(std::move(entities_[i]));
        entities_.erase(entities_.begin() + static_cast<std::ptrdiff_t>(i));
        taken.back().data.parent = parent;
    }
    const size_t pos = InsertPosition(parent, index);
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(pos), std::make_move_iterator(taken.begin()),
                     std::make_move_iterator(taken.end()));

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

json::Value Scene::SaveEntity(const SceneEntity& e) const {
    json::Value ej = reflect::Save(e.data, &e.unknowns);
    json::Value comps = json::Value::MakeArray();
    for (const Component& c : e.components) {
        if (!c.Known()) {
            comps.Push(c.raw);  // a type not offered now: exactly as it was read
            continue;
        }
        json::Value cj = c.raw.IsObject() ? c.raw : json::Value::MakeObject();
        cj.Set("type", c.type);
        cj.Set("data", reflect::Save(*c.value.Type(), c.value.Get(), &c.unknowns));
        comps.Push(std::move(cj));
    }
    ej.Set("components", std::move(comps));
    return ej;
}

SceneEntity Scene::LoadEntity(const json::Value& item, reflect::Warnings* warnings) const {
    auto warn = [&](std::string msg) {
        if (warnings) warnings->push_back(std::move(msg));
    };
    SceneEntity e;
    // "components" is the scene's to read (not an unknown key of the entity).
    json::Value fields = item;
    json::Value comps;
    if (json::Value* cs = fields.Find("components")) comps = std::move(*cs);
    fields.Erase("components");
    reflect::Load(e.data, fields, &e.unknowns, warnings);
    for (const auto& cj : comps.Items()) {
        Component c;
        c.type = cj.Get("type").AsString();
        c.raw = cj;
        const reflect::TypeDesc* t = reflect::FindTypeNamed(componentTypes_, c.type);
        const json::Value& data = cj.Get("data");
        if (t && data.IsObject() && !e.FindComponent(c.type)) {
            c.value = reflect::Object(*t);
            reflect::Load(*t, c.value.Get(), data, &c.unknowns, warnings);
            c.raw.Erase("data");  // written again from the value
        } else if (!t) {
            warn(std::format("エンティティ「{}」の部品「{}」は、いま入っているプラグインでは使えないので、そのまま残します", e.data.name,
                             c.type));
        } else {
            warn(std::format("エンティティ「{}」の部品「{}」は読めない (2 つ目か形が違う) ので、そのまま残します", e.data.name, c.type));
        }
        e.components.push_back(std::move(c));
    }
    return e;
}

json::Value Scene::Save() const {
    json::Value o = extra_;
    o.Set("nextId", nextId_);
    json::Value list = json::Value::MakeArray();
    for (const auto& e : entities_) list.Push(SaveEntity(e));
    o.Set("entities", std::move(list));
    return o;
}

namespace {
constexpr char kClipKind[] = "rm_engine.game_production.entities";
}  // namespace

bool Scene::IsClip(const json::Value& clip) { return clip.IsObject() && clip.Get("kind").AsString() == kClipKind; }

json::Value Scene::Copy(std::span<const int> ids) const {
    const std::vector<int> roots = Roots(ids);
    json::Value list = json::Value::MakeArray();
    for (const auto& e : entities_) {  // list order: siblings keep their order
        if (std::any_of(roots.begin(), roots.end(), [&](int r) { return IsUnder(e.data.id, r); })) list.Push(SaveEntity(e));
    }
    json::Value o = json::Value::MakeObject();
    o.Set("kind", kClipKind);
    o.Set("format", 1);
    o.Set("entities", std::move(list));
    return o;
}

std::vector<int> Scene::Paste(const json::Value& clip, int parent, size_t index, reflect::Warnings* warnings) {
    std::vector<int> roots;
    if (!IsClip(clip)) return roots;
    if (parent != 0 && !Find(parent)) parent = 0;
    // New ids; a parent inside the clip is followed only when it came before (so there is never a
    // loop) - anything else is a root, placed at `parent`.
    std::unordered_map<int, int> newId;
    std::vector<SceneEntity> copies;
    for (const auto& item : clip.Get("entities").Items()) {
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
    const size_t pos = InsertPosition(parent, index);
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(std::min(pos, entities_.size())), std::make_move_iterator(copies.begin()),
                     std::make_move_iterator(copies.end()));
    return roots;
}

void Scene::Load(const json::Value& v, reflect::Warnings* warnings) {
    auto warn = [&](std::string msg) {
        if (warnings) warnings->push_back(std::move(msg));
    };
    entities_.clear();
    extra_ = json::Value::MakeObject();
    nextId_ = 1;
    if (!v.IsObject()) return;
    extra_ = v;  // every key in its place; Save replaces "nextId" and "entities" in place
    for (const auto& item : v.Get("entities").Items()) entities_.push_back(LoadEntity(item, warnings));

    // Ids: positive and unique. Repeated or missing ones get new ids (never one in use).
    int maxId = 0;
    for (const auto& e : entities_) maxId = std::max(maxId, e.data.id);
    nextId_ = std::max<int>(static_cast<int>(v.Get("nextId").AsInt(1)), maxId + 1);
    std::unordered_set<int> seen;
    for (auto& e : entities_) {
        if (e.data.id > 0 && seen.insert(e.data.id).second) continue;
        const int fresh = nextId_++;
        warn(std::format("エンティティ「{}」の ID ({}) が使えないので {} にしました", e.data.name, e.data.id, fresh));
        e.data.id = fresh;
        seen.insert(fresh);
    }
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

}  // namespace rm::game_production
