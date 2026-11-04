#pragma once
// The entities of a scene and their parents (saved in the project; the exported game reads it).
// Pure: no drawing, no files - tested in tests/TestScene.cpp.
//
// One Entity is described once (RM_STRUCT): saving, loading, the inspector and undo come from it.
// The order of the entities in the list is the order of siblings (children of the same parent),
// which is the order the 階層 shows them in.
//
// Components (Transform and the rest) come with the ECS; for now an entity has a name and a switch.

#include "base/Json.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"

#include <span>
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
    // Deletes the given entities and everything below them. Returns how many were deleted.
    size_t Delete(std::span<const int> ids);
    // Moves the given entities (with everything below them) under `parent` at `index` among its
    // children counted without the moved ones. False when it is refused (into itself, a missing
    // parent) or nothing changed.
    bool Move(std::span<const int> ids, int parent, size_t index);

    // { "nextId": 7, "entities": [ {"id": 1, "parent": 0, "name": ..., "active": ...}, ... ] }
    // Keys this version does not know (of the scene and of each entity) are written back.
    json::Value Save() const;
    // Repairs what cannot be (missing or repeated ids, a missing parent, a parent loop) with a warning.
    void Load(const json::Value& v, reflect::Warnings* warnings = nullptr);

private:
    size_t IndexOf(int id) const;
    size_t InsertPosition(int parent, size_t index) const;

    std::vector<SceneEntity> entities_;
    int nextId_ = 1;
    json::Value extra_ = json::Value::MakeObject();  // the scene object as loaded (its unknown keys are kept)
};

}  // namespace rm::game_production
