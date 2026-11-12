#pragma once
// What one change of a scene's shape did, for undo - kept as the entities it touched, before and
// after (like Unity's Undo.RecordObject), not as the whole scene twice.
//
//   SceneRecord r = SceneRecord::Begin(scene, touched);   // "these (and everything below them) will change"
//   ... the change: Create / Delete / Move / Paste / Duplicate / AddComponent / ApplyPrefab ...
//   r.End(scene);                                         // + every entity made since Begin (new ids)
//   r.Undo(scene);  r.Redo(scene);                        // only those entities are put back
//
// What is kept of an entity: whether it is there, its place in the list (= the order of siblings)
// and everything SaveEntity writes (its RM_PROPERTY fields, parent, components, prefab link).
//
// THE ONE RULE: `touched` must name every entity the change alters other than the ones it makes.
// An entity is "altered" when anything SaveEntity writes changes, or it is taken out / put in. (The
// others move in the list only because entities before them come and go - that is fine: their order
// among themselves stays.) Begin takes the touched entities with everything below them, which covers
// what the scene's operations change (a prefab placed again changes its whole subtree).
// The editor checks the rule in its Debug build on every edit (the whole scene before and after,
// against Undo / Redo of the record); tests/TestSceneRecord.cpp runs thousands of random changes.
//
// Ids are never used again (Scene::NextId only goes up), so "made since Begin" is the ids from the
// next id at Begin to the next id at End.
//
// Pure - the runtime part of game production (no editor, no files).

#include "rm_engine/game_production/runtime/Scene.h"

#include <span>
#include <vector>

namespace rm::game_production {

class SceneRecord {
public:
    static SceneRecord Begin(const Scene& scene, std::span<const int> touched);
    void End(const Scene& scene);

    void Undo(Scene& scene) const { scene.RestoreStates(ids_, before_); }
    void Redo(Scene& scene) const { scene.RestoreStates(ids_, after_); }

    // False when the change changed nothing (nothing is put in the history then).
    bool Changed() const { return before_ != after_; }
    // About how much memory it keeps (the touched entities as text, before and after).
    size_t Bytes() const { return bytes_; }
    size_t Entities() const { return ids_.size(); }

private:
    std::vector<int> ids_;
    std::vector<Scene::EntityState> before_, after_;
    int nextAtBegin_ = 0;
    size_t bytes_ = 0;
};

}  // namespace rm::game_production
