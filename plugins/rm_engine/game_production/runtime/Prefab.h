#pragma once
// Prefabs: an entity and everything below it kept as a file, placed in scenes as many times as
// wanted. Change the prefab and every placed one follows - except what was changed on the placed
// one itself (its overrides).
//
//   prefab file (.rmprefab, JSON)
//     { "kind": "rm_engine.game_production.prefab", "format": 1,
//       "entities": [ <the root>, <below it ...> ] }      the scene's entity form, parents first
//
//   a placed prefab, in the scene
//     every entity has "prefab": { "source": <its id in the file> }; the root also has
//     "asset": "<the .rmprefab's asset id>" and "overrides": {
//        "fields":            [ {"source": 1, "component": "", "index": 0, "field": "name", "value": "Cube 2"},
//                               {"source": 1, "component": "rm::space3d::Transform", "index": 0, "field": "position", "value": [...]} ],
//        "addedComponents":   [ {"source": 2, "component": {"type": ..., "data": {...}}} ],
//        "removedComponents": [ {"source": 2, "component": "rm::...", "index": 0} ],
//        "removedEntities":   [ 3 ] }
//
//   component "" = the entity's own fields (name, 有効). Fields are compared one by one at the top
//   level of the entity / component (a vector field is one field). The root's own fields and the
//   fields of its "placement" components (Transform - ComponentInfo::placement) always count as
//   overrides: a placed thing keeps its name and its place when the prefab changes.
//
// The prefab file is named by its asset id (AssetRef: moving or renaming it keeps the link). Data
// from before assets had ids has "path" (from the project folder) - read the same way.
//
// NESTED PREFABS: a placed prefab inside the entities a prefab is made from stays a prefab of its own.
// The outer file keeps only its root, with its link ("prefab": {"source", "asset", "overrides"} -
// what was changed on it, against ITS prefab); what is inside it always comes from its own file.
//   * in the scene the nested root is both: the root of its placed prefab, and one of the outer's
//     entities (PrefabLink::outer = its id in the outer file)
//   * the outer's overrides cover the outer's own entities (the nested root's place, say); a change
//     on the nested prefab's insides is the nested root's own overrides
//   * changing the inner prefab reaches every placed one, nested ones included (they are made from
//     its file); changing the outer remakes its entities and keeps the nested roots that were there
//     with their own overrides (the editor first works them out again)
//   * the editor fills the nested ones (Scene::NestedPrefabs → ApplyPrefab from their files),
//     outermost first, and refuses a prefab that would end up inside itself
//
// Pure (no files) - tested in tests/TestPrefab.cpp. Reading and writing the files is the editor's.

#include "base/Json.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rm::game_production {

inline constexpr char kPrefabKind[] = "rm_engine.game_production.prefab";
inline constexpr char kPrefabExtension[] = ".rmprefab";
inline constexpr int kPrefabFormat = 1;  // the "format" this version writes

bool IsPrefab(const json::Value& v);
// Written by a newer version ("format" above kPrefabFormat): not read, and never written over.
bool NewerPrefab(const json::Value& v);

// The prefab of `root` and everything below it. Entities of a placed prefab keep their source as
// their id in the file (so the other placed ones still match after it is written back); the others
// get ids after those. `sources` (if given): scene id -> id in the file, for Scene::LinkPrefab.
// A prefab placed inside is written as its root with its link and its current PrefabLink::overrides
// (the caller works them out first); what is inside it is not written (★ nor plain entities put
// under its insides). `previous`: the file as it was (when writing it again) - its keys this version
// does not know are kept (rule 6).
json::Value MakePrefab(const Scene& scene, int root, std::unordered_map<int, int>* sources = nullptr, const json::Value* previous = nullptr);

// The asset ids of the prefabs nested in `prefab` (a file's content), in order, each once.
std::vector<std::string> NestedAssets(const json::Value& prefab);

// How the placed prefab `root` differs from `prefab` (the form above). `placement`: component types
// whose fields on the root always count as overrides.
json::Value Overrides(const Scene& scene, int root, const json::Value& prefab, std::span<const std::string> placement = {});
// How many changes `overrides` holds (shown in the inspector).
size_t CountOverrides(const json::Value& overrides);

// A prefab file's content by its asset id (the editor's files, an exported game's .rmpak); null: none.
using PrefabReader = std::function<const json::Value*(const AssetRef& asset)>;
inline constexpr size_t kMaxPrefabNesting = 16;  // prefabs inside prefabs inside ...
// The prefabs nested in the placed prefab `root` filled from their own files (each with its own
// overrides), and the ones nested in those. `chain`: the prefabs `root` is inside, itself included -
// one that would end up inside itself is not filled (told in `warnings`; `title` names it).
void FillNestedPrefabs(Scene& scene, int root, std::vector<std::string>& chain, const PrefabReader& read, reflect::Warnings& warnings,
                       const std::function<std::string(const AssetRef&)>& title);
// A copy of the prefab `asset` with everything nested in it, under `parent` (0: the top) - what
// rm::Spawn(prefab) makes while the game plays. 0: no such prefab (or it has no entities).
int SpawnPrefab(Scene& scene, const AssetRef& asset, int parent, const PrefabReader& read, reflect::Warnings& warnings);

// The file name's stem (no folder, no extension): "Assets/Enemies/Bat.rmprefab" -> "Bat".
std::string PrefabName(std::string_view path);

}  // namespace rm::game_production
