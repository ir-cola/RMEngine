#pragma once
// The entities of a scene and their parents (saved in the project; the exported game reads it).
// Pure: no drawing, no files - tested in tests/TestScene.cpp.
//
// One Entity is described once (RM_STRUCT): saving, loading, the inspector and undo come from it.
// The order of the entities in the list is the order of siblings (children of the same parent),
// which is the order the 階層 shows them in.
//
// An entity has a name, a switch and its components (Components.h): at most one of each type.

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Components.h"

#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace rm::game_production {

RM_STRUCT(Label="エンティティ")
struct Entity {
    RM_PROPERTY() int id = 0;      // saved, not shown: what other data refers to (never reused)
    RM_PROPERTY() int parent = 0;  // the parent's id, 0 = top level
    RM_PROPERTY(Edit, Label="名前") std::string name = "エンティティ";
    RM_PROPERTY(Edit, Label="有効", Tooltip="外すと、このエンティティと中のもの全部がゲームで動きません") bool active = true;
};

struct SceneEntity {
    Entity data;
    reflect::Unknowns unknowns;  // keys of a newer version: written back unchanged
    std::vector<Component> components;  // in the order they were added

    Component* FindComponent(std::string_view type);
    const Component* FindComponent(std::string_view type) const;
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

    // The component types offered now (by the plugins). Loading reads the others as unknown and
    // keeps them; set this before Load.
    void SetComponentTypes(std::vector<const reflect::TypeDesc*> types) { componentTypes_ = std::move(types); }
    const std::vector<const reflect::TypeDesc*>& ComponentTypes() const { return componentTypes_; }

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

    // A component of `type` with its default values. False: no such entity, or it has one already.
    bool AddComponent(int id, const reflect::TypeDesc& type);
    // False when the entity has none of that type (known or not).
    bool RemoveComponent(int id, std::string_view type);
    // Moves a component one place up (by = -1) or down (+1) among the entity's (the inspector's order).
    // False when it cannot move (no such entity / component, already first / last).
    bool MoveComponent(int id, std::string_view type, int by);

    // { "nextId": 7, "entities": [ {"id": 1, "parent": 0, "name": ..., "active": ...,
    //     "components": [ {"type": "rm::space3d::Transform", "data": {...}} ] }, ... ] }
    // Keys this version does not know (of the scene and of each entity) are written back.
    json::Value Save() const;
    // Repairs what cannot be (missing or repeated ids, a missing parent, a parent loop) with a warning.
    void Load(const json::Value& v, reflect::Warnings* warnings = nullptr);

private:
    // One entity as it is in the file (and on the clipboard), and back.
    json::Value SaveEntity(const SceneEntity& e) const;
    SceneEntity LoadEntity(const json::Value& item, reflect::Warnings* warnings) const;
    size_t IndexOf(int id) const;
    size_t InsertPosition(int parent, size_t index) const;

    std::vector<SceneEntity> entities_;
    std::vector<const reflect::TypeDesc*> componentTypes_;
    int nextId_ = 1;
    json::Value extra_ = json::Value::MakeObject();  // the scene object as loaded (its unknown keys are kept)
};

}  // namespace rm::game_production
