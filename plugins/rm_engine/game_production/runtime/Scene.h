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
// prefab file they are, and on the placed root the file and what differs from it. A prefab placed
// inside another prefab's file (nested) has both: it is a placed prefab of its own, and one of the
// entities of the prefab around it (`outer`).

#include "base/Json.h"
#include "base/Ref.h"
#include "core/reflect/Data.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/Components.h"

#include <algorithm>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

namespace rm::game_production {

RM_STRUCT(Label="エンティティ")
struct Entity {
    RM_PROPERTY() int id = 0;      // saved, not shown: what other data refers to (never reused)
    RM_PROPERTY() int parent = 0;  // the parent's id, 0 = top level
    RM_PROPERTY(Edit, Label="名前") std::string name = "エンティティ";
    RM_PROPERTY(Edit, Label="有効", Tooltip="外すと、このエンティティと中のもの全部がゲームで動きません") bool active = true;
    // Names to find and sort entities by (scripts: rm::FindWithTag, Entity::Layer); the game settings list them.
    RM_PROPERTY(Edit, Label="タグ", Tooltip="スクリプトで探すときの名前 (rm::FindWithTag(\"Enemy\"))。ゲームの設定の「タグ」に一覧を書いておけます。空: なし") std::string tag;
    RM_PROPERTY(Edit, Label="レイヤー", Tooltip="ゲームの設定の「レイヤー」の名前。空: 既定 (0 番)") std::string layer;
};

// Where an entity of a placed prefab comes from. Saved as the entity's "prefab" key (only when set):
//   { "source": 3, "asset": "<id of the .rmprefab>", "overrides": {...}, "outer": 2 }
//   asset / overrides: the root of a placed prefab only. outer: a root that is itself one of the
//   entities of the placed prefab around it (nested) - its id in that prefab's file.
// Data from before assets had ids has "path": "Assets/Cube.rmprefab" instead of "asset": it is read
// into `asset` as it is (SoftwareHost::AssetFile finds the file by either) and written as "asset".
struct PrefabLink {
    int source = 0;          // the entity's id inside its prefab file (a root: the file's first); 0 = not from a prefab
    AssetRef asset;          // the root of a placed prefab: the prefab file
    json::Value overrides;   // the root: what differs from the prefab (Prefab.h), as of the last save
    int outer = 0;           // a nested root: its id in the file of the prefab around it; 0 = placed by hand
    json::Value extra;       // the "prefab" object as read (keys of a newer version are kept)

    bool Linked() const { return source > 0; }
    bool Root() const { return source > 0 && !asset.Empty(); }
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

// What systems keep for a scene while it plays (a physics world ...), by a name of their own. It lives
// and dies with the Scene object: never copied (Play's copy starts with none - and ■ throws the copy
// away with all of it), never saved. Moved with the scene.
class PlayStates {
public:
    PlayStates() = default;
    PlayStates(const PlayStates&) {}
    PlayStates& operator=(const PlayStates&) {
        map_.clear();
        return *this;
    }
    PlayStates(PlayStates&&) noexcept = default;
    PlayStates& operator=(PlayStates&&) noexcept = default;
    // The slot `key` (empty until a system puts something there).
    std::shared_ptr<void>& Slot(const std::string& key) { return map_[key]; }
    void Clear() { map_.clear(); }

private:
    std::unordered_map<std::string, std::shared_ptr<void>> map_;
};

class Scene {
public:
    static constexpr size_t kEnd = static_cast<size_t>(-1);

    // What systems keep for this scene while it plays (PlayStates): `T` made the first time.
    template <class T>
    T& PlayState(const std::string& key) {
        std::shared_ptr<void>& slot = playStates_.Slot(key);
        if (!slot) slot = std::make_shared<T>();
        return *static_cast<T*>(slot.get());
    }
    PlayStates& PlayStatesOf() { return playStates_; }

    // The component types offered now (by the plugins), and those of them an entity may have several
    // of. Loading reads the others as unknown and keeps them (so a second one of a type that allows
    // only one); set this before Load.
    void SetComponentTypes(std::vector<const reflect::TypeDesc*> types, std::vector<const reflect::TypeDesc*> multiple = {}) {
        componentTypes_ = std::move(types);
        multipleTypes_ = std::move(multiple);
    }
    const std::vector<const reflect::TypeDesc*>& ComponentTypes() const { return componentTypes_; }
    const std::vector<const reflect::TypeDesc*>& MultipleTypes() const { return multipleTypes_; }
    bool AllowsMultiple(std::string_view type) const;

    // The scene's own values (SceneSettingInfo: 重力, 空 ...): one of each type offered, in this order.
    // Saved as "settings": [{ "type", "data" }] (as components); a type not offered now is kept as it
    // was read. Setting the types (before or after Load) gives the scene each one it has not got yet,
    // with its defaults.
    void SetSettingTypes(std::vector<const reflect::TypeDesc*> types);
    const std::vector<const reflect::TypeDesc*>& SettingTypes() const { return settingTypes_; }
    const std::vector<Component>& Settings() const { return settings_; }
    Component* FindSetting(std::string_view type);
    const Component* FindSetting(std::string_view type) const;
    // The setting of type T (null: its type is not offered to this scene).
    template <class T>
    T* Setting() {
        Component* c = FindSetting(reflect::TypeOf<T>().name);
        return c && c->Known() ? static_cast<T*>(c->value.Get()) : nullptr;
    }
    template <class T>
    const T* Setting() const {
        const Component* c = FindSetting(reflect::TypeOf<T>().name);
        return c && c->Known() ? static_cast<const T*>(c->value.Get()) : nullptr;
    }

    bool Empty() const { return entities_.empty(); }
    const std::vector<SceneEntity>& Entities() const { return entities_; }
    SceneEntity* Find(int id);
    const SceneEntity* Find(int id) const;

    std::vector<TreeEntry> TreeOrder() const;
    std::vector<int> Children(int parent) const;  // in order
    // True when `id` is `ancestor` itself or somewhere below it.
    bool IsUnder(int id, int ancestor) const;
    // The given entities and everything below them (one pass over the scene: use it instead of
    // asking IsUnder for every entity).
    std::unordered_set<int> Subtree(std::span<const int> roots) const;
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
    // parent) or nothing changed. A part of a placed prefab moved to another parent leaves it (plain
    // entities - the prefab decides its own parents); a nested prefab moved out of the one around it
    // is placed by hand. (So a part never joins another placed prefab with an id of the wrong file.)
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
    // The root and the entities of its placed prefab, in tree order: the roots of the prefabs nested
    // in it too (they are its entities), but not what is inside those, nor plain entities added.
    std::vector<int> PrefabMembers(int root) const;
    // The roots of the prefabs nested in the placed prefab `root` (its members that are roots).
    std::vector<int> NestedPrefabs(int root) const;
    // Every root of a placed prefab (of the prefab file `asset` - an AssetRef's guid; empty = any), in
    // tree order: an outer one before the ones nested in it.
    std::vector<int> PrefabRoots(std::string_view asset = {}) const;
    // `root` and everything below it in `sources` become a placed prefab of `asset`: each entity's
    // source is sources[its id]; a placed prefab's root among them stays one and becomes nested
    // (outer = sources[its id]). Entities not in `sources` (inside nested prefabs) stay as they are.
    bool LinkPrefab(int root, const AssetRef& asset, const std::unordered_map<int, int>& sources);
    // The entities of the placed prefab become plain entities again (the prefabs nested in it stay,
    // placed by hand now). False: `root` is not a root.
    bool UnlinkPrefab(int root);
    // A copy of `prefab` (a prefab file's content) under `parent` at `index` among its children, linked
    // to `asset`. The prefabs nested in it come as their roots only: the editor fills them from their
    // own files (ApplyPrefab on each of NestedPrefabs). Returns the new root's id (0: not a prefab, or
    // it has no entities).
    int PlacePrefab(const json::Value& prefab, const AssetRef& asset, int parent = 0, size_t index = kEnd,
                    reflect::Warnings* warnings = nullptr);
    // Makes the placed prefab `root` the prefab again, with `overrides` (Prefab.h) on it: entities
    // keep their ids (matched by source), new ones in the prefab come with new ids, ones gone from it
    // go (with their components; a nested prefab with everything in it); plain entities added under
    // it stay (under the root when their parent went). A nested prefab that was there keeps its own
    // overrides (what was changed on it here); a new one takes the file's. What is inside the nested
    // ones is left as it is: the editor makes them again from their own files. False: `root` is not a
    // root or `prefab` has no entities.
    bool ApplyPrefab(int root, const json::Value& prefab, const json::Value& overrides, reflect::Warnings* warnings = nullptr);

    // Ids are never used again, not even after an undo brought back an older scene: the next id
    // (what a new entity gets) is kept by the undo command and put back with KeepNextId.
    int NextId() const { return nextId_; }
    void KeepNextId(int atLeast) { nextId_ = std::max(nextId_, atLeast); }
    // Ids above this are taken as broken (and replaced when loading): there is always room for new ones.
    static constexpr int kMaxId = 1 << 30;

    // { "nextId": 7, "entities": [ {"id": 1, "parent": 0, "name": ..., "active": ...,
    //     "components": [ {"type": "rm::space3d::Transform", "data": {...}} ] }, ... ] }
    // Keys this version does not know (of the scene and of each entity) are written back.
    json::Value Save() const;
    // Repairs what cannot be (missing or repeated ids, a missing parent, a parent loop) with a warning.
    void Load(const json::Value& v, reflect::Warnings* warnings = nullptr);

    // One entity as it is in the file (and on the clipboard, in a prefab), and back.
    json::Value SaveEntity(const SceneEntity& e) const;

    // --- The state of some entities, for undo (SceneRecord.h) ----------------------------------
    // One entity as it is: there or not, where it is in the list (the list's order is the order of
    // siblings), and everything else about it (SaveEntity: its fields, parent, components, prefab link).
    struct EntityState {
        int id = 0;
        bool present = false;
        size_t index = 0;
        json::Value data;
        bool operator==(const EntityState&) const = default;
    };
    std::vector<EntityState> CaptureStates(std::span<const int> ids) const;
    // The entities `ids` become as `states` says (each of them taken out, then the ones there put back
    // at their places, in order). Every other entity must be as it was when the states were captured
    // (so the places are right): SceneRecord undoes in order, newest first.
    void RestoreStates(std::span<const int> ids, std::span<const EntityState> states);
    SceneEntity LoadEntity(const json::Value& item, reflect::Warnings* warnings) const;
    // One component as it is in the file ({"type": ..., "data": {...}}), and back (unknown: kept as read).
    // `allowed`: the entity has none of the type yet, or the type allows several.
    static json::Value SaveComponent(const Component& c);
    Component LoadComponent(const json::Value& cj, bool allowed, const std::string& entityName, reflect::Warnings* warnings) const;
    // A component (or a setting) as SaveComponent wrote it: its value when its type is one of `types`
    // (and `allowed`), else kept as read (raw).
    static Component ReadComponent(const json::Value& cj, std::span<const reflect::TypeDesc* const> types, bool allowed,
                                   reflect::Warnings* warnings);
    void AddMissingSettings();

private:
    size_t IndexOf(int id) const;
    // Where each id is in entities_ (Find is one look-up, not a walk through the list). Made again
    // when it is next needed after anything that adds, removes or reorders entities (Touch), and
    // whenever a look-up finds it out of date (a safety net: it is never trusted blindly).
    void Touch() { indexStale_ = true; }
    void Reindex() const;
    size_t InsertPosition(int parent, size_t index) const;
    // Entities as Copy writes them, under `parent` at `index`, with new ids. `idMap`: old id -> new id.
    std::vector<int> PasteList(const json::Value& list, int parent, size_t index, reflect::Warnings* warnings,
                               std::unordered_map<int, int>* idMap);
    // Copies (pasted, duplicated) of entities of a placed prefab that came without its root are plain
    // entities; a copied nested root without the prefab around it is placed by hand.
    void DetachCopies(const std::vector<int>& made);
    // `id` leaves the placed prefab it is a part of: it and the parts below it become plain entities,
    // the prefabs nested in them are placed by hand (a nested root itself: placed by hand).
    void DetachFromPrefab(int id);

    std::vector<SceneEntity> entities_;
    mutable std::unordered_map<int, size_t> index_;
    mutable bool indexStale_ = true;
    std::vector<const reflect::TypeDesc*> componentTypes_;
    std::vector<const reflect::TypeDesc*> multipleTypes_;
    std::vector<const reflect::TypeDesc*> settingTypes_;
    std::vector<Component> settings_;
    int nextId_ = 1;
    json::Value extra_ = json::Value::MakeObject();  // the scene object as loaded (its unknown keys are kept)
    PlayStates playStates_;
};

// One field of an entity of a scene: the entity's own (component "") or one of its components (its type
// and place among that type's), by the path inside it. What Play's この値を残す reads in the copy that
// plays and writes into the scene from before Play.
struct SceneField {
    int entity = 0;
    std::string component;
    size_t nth = 0;
    reflect::FieldPath path;
};
// Its value as saved; nullopt: the entity, the component or the field is not there.
std::optional<json::Value> ReadSceneField(Scene& scene, const SceneField& f);
// Sets it (as loading would: range, unknown names kept). False: not there.
bool WriteSceneField(Scene& scene, const SceneField& f, const json::Value& value, reflect::Warnings* warnings = nullptr);

}  // namespace rm::game_production
