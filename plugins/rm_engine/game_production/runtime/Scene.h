#pragma once
// The entities of a scene and their parents (saved in the project; the exported game reads it).
// Pure: no drawing, no files - tested in tests/TestScene.cpp.
//
// One Entity is described once (RM_STRUCT): saving, loading, the inspector and undo come from it.
// The order of the entities in the list is the order of siblings (children of the same parent),
// which is the order the 階層 shows them in.
//
// An entity has a name, a switch and its components (Components.h): one of each type, or several
// of a type that allows it (ComponentInfo::multiple). A component is found by its type and its
// number among the entity's components of that type (0 = the first).
//
// Prefabs (Prefab.h): the entities of a placed prefab carry a PrefabLink - which entity of the
// prefab file they are, and on the placed root the file and what differs from it.

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Components.h"

#include <span>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::game_production {

RM_STRUCT(Label="エンティティ")
struct Entity {
    RM_PROPERTY() int id = 0;      // saved, not shown: what other data refers to (never reused)
    RM_PROPERTY() int parent = 0;  // the parent's id, 0 = top level
    RM_PROPERTY(Edit, Label="名前") std::string name = "エンティティ";
    RM_PROPERTY(Edit, Label="有効", Tooltip="外すと、このエンティティと中のもの全部がゲームで動きません") bool active = true;
};

// Where an entity of a placed prefab comes from. Saved as the entity's "prefab" key (only when set):
//   { "source": 3, "path": "Assets/Cube.rmprefab", "overrides": {...} }   (path / overrides: the root only)
struct PrefabLink {
    int source = 0;          // the entity's id inside the prefab file; 0 = not from a prefab
    std::string path;        // the root of a placed prefab: the file, from the project folder ("/")
    json::Value overrides;   // the root: what differs from the prefab (Prefab.h), as of the last save
    json::Value extra;       // the "prefab" object as read (keys of a newer version are kept)

    bool Linked() const { return source > 0; }
    bool Root() const { return source > 0 && !path.empty(); }
};

struct SceneEntity {
    Entity data;
    reflect::Unknowns unknowns;  // keys of a newer version: written back unchanged
    std::vector<Component> components;  // in the order they were added
    PrefabLink prefab;

    // Every entity reference (EntityRef) in its components: to[id] when it is there; otherwise kept,
    // or cleared (0) with `clearOthers`. Copies use it to point at the copies.
    void MapEntityRefs(const std::unordered_map<int, int>& to, bool clearOthers);

    // The nth component of `type` (0 = the first of that type).
    Component* FindComponent(std::string_view type, size_t nth = 0);
    const Component* FindComponent(std::string_view type, size_t nth = 0) const;
    size_t CountComponents(std::string_view type) const;
    // The number of `c` among the entity's components of its type (0 = the first).
    size_t NthOf(const Component& c) const;
};

// Where an entity is in the tree: parents before their children, siblings in order.
struct TreeEntry {
    const SceneEntity* entity = nullptr;
    int depth = 0;
    bool activeInHierarchy = true;  // it and every parent are active
};

class Scene {
public:
    static constexpr size_t kEnd = static_cast<size_t>(-1);

    // The component types offered now (by the plugins), and those of them an entity may have several
    // of. Loading reads the others as unknown and keeps them (so a second one of a type that allows
    // only one); set this before Load.
    void SetComponentTypes(std::vector<const reflect::TypeDesc*> types, std::vector<const reflect::TypeDesc*> multiple = {}) {
        componentTypes_ = std::move(types);
        multipleTypes_ = std::move(multiple);
    }
    const std::vector<const reflect::TypeDesc*>& ComponentTypes() const { return componentTypes_; }
    bool AllowsMultiple(std::string_view type) const;

    bool Empty() const { return entities_.empty(); }
    const std::vector<SceneEntity>& Entities() const { return entities_; }
    SceneEntity* Find(int id);
    const SceneEntity* Find(int id) const;

    std::vector<TreeEntry> TreeOrder() const;
    std::vector<int> Children(int parent) const;  // in order
    // True when `id` is `ancestor` itself or somewhere below it.
    bool IsUnder(int id, int ancestor) const;
    // The given entities without the ones whose parent (or grand-parent ...) is also given, in tree order.
    std::vector<int> Roots(std::span<const int> ids) const;

    // A new entity under `parent` (0 or a missing id = top level) at `index` among its children.
    int Create(std::string name, int parent = 0, size_t index = kEnd);
    // Copies of the given entities and everything below them, each right after its original.
    // Returns the ids of the copies of the roots.
    std::vector<int> Duplicate(std::span<const int> ids);
    // The given entities and everything below them, as text for the clipboard (JSON, the same
    // entity form as the file): { "kind": "rm_engine.game_production.entities", "format": 1,
    // "entities": [...] }. Ids are the originals; Paste gives new ones.
    json::Value Copy(std::span<const int> ids) const;
    // Copies of what Copy wrote, under `parent` (0 or a missing id = top level) at `index` among its
    // children, with new ids. Components of types not offered now are kept as they were. Returns
    // the ids of the copies of the roots (empty: `clip` is not entities).
    std::vector<int> Paste(const json::Value& clip, int parent = 0, size_t index = kEnd, reflect::Warnings* warnings = nullptr);
    static bool IsClip(const json::Value& clip);

    // Deletes the given entities and everything below them. Returns how many were deleted.
    size_t Delete(std::span<const int> ids);
    // Moves the given entities (with everything below them) under `parent` at `index` among its
    // children counted without the moved ones. False when it is refused (into itself, a missing
    // parent) or nothing changed.
    bool Move(std::span<const int> ids, int parent, size_t index);

    // A component of `type` with its default values, after the entity's others. False: no such
    // entity, or it has one already and the type allows only one.
    bool AddComponent(int id, const reflect::TypeDesc& type);
    // The nth component of `type`. False when the entity has no such one (known or not).
    bool RemoveComponent(int id, std::string_view type, size_t nth = 0);
    // Moves the nth component of `type` one place up (by = -1) or down (+1) among all the entity's
    // components (the inspector's order). False when it cannot move (no such entity / component,
    // already first / last).
    bool MoveComponent(int id, std::string_view type, int by, size_t nth = 0);

    // --- Prefabs (the file side is in Prefab.h) -------------------------------------------------
    // The root of the placed prefab `id` belongs to (itself when it is a root); 0: none. Walks up
    // through linked entities only: a plain entity in between ends it.
    int PrefabRootOf(int id) const;
    // The root and the entities of its placed prefab, in tree order (not the entities of another
    // prefab placed inside it, nor plain entities added under it).
    std::vector<int> PrefabMembers(int root) const;
    // Every root of a placed prefab (of the file `path`; empty = any), in tree order.
    std::vector<int> PrefabRoots(std::string_view path = {}) const;
    // `root` and everything below it become a placed prefab of `path`: each entity's source is
    // sources[its id] (entities not in `sources` stay as they are). Prefabs placed inside are
    // taken in (their roots lose their own file).
    bool LinkPrefab(int root, const std::string& path, const std::unordered_map<int, int>& sources);
    // The entities of the placed prefab become plain entities again. False: `root` is not a root.
    bool UnlinkPrefab(int root);
    // A copy of `prefab` (a prefab file's content) under `parent` at `index` among its children, linked
    // to `path`. Returns the new root's id (0: not a prefab, or it has no entities).
    int PlacePrefab(const json::Value& prefab, const std::string& path, int parent = 0, size_t index = kEnd,
                    reflect::Warnings* warnings = nullptr);
    // Makes the placed prefab `root` the prefab again, with `overrides` (Prefab.h) on it: entities
    // keep their ids (matched by source), new ones in the prefab come with new ids, ones gone from it
    // go (with their components); plain entities added under it stay (under the root when their
    // parent went). False: `root` is not a root or `prefab` has no entities.
    bool ApplyPrefab(int root, const json::Value& prefab, const json::Value& overrides, reflect::Warnings* warnings = nullptr);

    // { "nextId": 7, "entities": [ {"id": 1, "parent": 0, "name": ..., "active": ...,
    //     "components": [ {"type": "rm::space3d::Transform", "data": {...}} ] }, ... ] }
    // Keys this version does not know (of the scene and of each entity) are written back.
    json::Value Save() const;
    // Repairs what cannot be (missing or repeated ids, a missing parent, a parent loop) with a warning.
    void Load(const json::Value& v, reflect::Warnings* warnings = nullptr);

    // One entity as it is in the file (and on the clipboard, in a prefab), and back.
    json::Value SaveEntity(const SceneEntity& e) const;
    SceneEntity LoadEntity(const json::Value& item, reflect::Warnings* warnings) const;
    // One component as it is in the file ({"type": ..., "data": {...}}), and back (unknown: kept as read).
    // `allowed`: the entity has none of the type yet, or the type allows several.
    static json::Value SaveComponent(const Component& c);
    Component LoadComponent(const json::Value& cj, bool allowed, const std::string& entityName, reflect::Warnings* warnings) const;

private:
    size_t IndexOf(int id) const;
    size_t InsertPosition(int parent, size_t index) const;
    // Entities as Copy writes them, under `parent` at `index`, with new ids. `idMap`: old id -> new id.
    std::vector<int> PasteList(const json::Value& list, int parent, size_t index, reflect::Warnings* warnings,
                               std::unordered_map<int, int>* idMap);

    std::vector<SceneEntity> entities_;
    std::vector<const reflect::TypeDesc*> componentTypes_;
    std::vector<const reflect::TypeDesc*> multipleTypes_;
    int nextId_ = 1;
    json::Value extra_ = json::Value::MakeObject();  // the scene object as loaded (its unknown keys are kept)
};

}  // namespace rm::game_production
