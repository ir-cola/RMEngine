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
//     "path": "Assets/Cube.rmprefab" and "overrides": {
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
// The path is from the project folder (a relative path, '/'). ★ It becomes an asset reference when
// assets have ids.
//
// Pure (no files) - tested in tests/TestPrefab.cpp. Reading and writing the files is the editor's.

#include "base/Json.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rm::game_production {

inline constexpr char kPrefabKind[] = "rm_engine.game_production.prefab";
inline constexpr char kPrefabExtension[] = ".rmprefab";

bool IsPrefab(const json::Value& v);

// The prefab of `root` and everything below it. Entities of a placed prefab keep their source as
// their id in the file (so the other placed ones still match after it is written back); the others
// get ids after those. `sources` (if given): scene id -> id in the file, for Scene::LinkPrefab.
// Links of prefabs placed inside are not written (they become part of this one).
json::Value MakePrefab(const Scene& scene, int root, std::unordered_map<int, int>* sources = nullptr);

// How the placed prefab `root` differs from `prefab` (the form above). `placement`: component types
// whose fields on the root always count as overrides.
json::Value Overrides(const Scene& scene, int root, const json::Value& prefab, std::span<const std::string> placement = {});
// How many changes `overrides` holds (shown in the inspector).
size_t CountOverrides(const json::Value& overrides);

// The file name's stem (no folder, no extension): "Assets/Enemies/Bat.rmprefab" -> "Bat".
std::string PrefabName(std::string_view path);

}  // namespace rm::game_production
