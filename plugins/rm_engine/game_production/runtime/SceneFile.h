#pragma once
// A game's scenes kept as files of their own, one per scene (not inside the .rmpf: the .rmpf has a
// limit of platform::kMaxFileBytes, and a save writes only the scenes that changed). Pure: no files are
// touched here - tested in tests/TestSceneFile.cpp. The editor (GameSoftware) and, later, the exported
// game read the files.
//
//   Assets/Scenes/<名前>.rmscene      an asset like any other: a .meta with its id next to it
//   { "kind": "rm_engine.scene", "format": 1, "nextId": 7, "entities": [ ... ] }    (Scene::Save + kind / format)
//
//   .rmpf  "softwareData": [ { ... "software": "game", "data": {
//              "settings":  { ... the game settings ... },
//              "sceneList": { "scenes": ["<asset id>", ...], "open": "<asset id>" } } } ]
//
// Projects of before (0.0.1: the one scene inside the .rmpf as "scene") are still read; the next save
// writes the scene to its own file, drops "scene" from the .rmpf and keeps the .rmpf as it was once,
// next to it (<名前>.rmpf.before-scenes.bak).
//
// Keys of a newer version are kept: in the scene file (the scene's own unknown keys, kind / format
// among them), in "sceneList" (reflect::Unknowns) and in the game's data (the editor saves into the data
// as it was read).

#include "base/Json.h"
#include "base/Ref.h"
#include "core/reflect/Reflect.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::game_production {

inline constexpr char kSceneKind[] = "rm_engine.scene";
inline constexpr int64_t kSceneFormat = 1;  // the "format" this version writes
inline constexpr char kSceneExtension[] = ".rmscene";
inline constexpr char kScenesFolder[] = "Assets/Scenes";  // where a new scene's file goes (from the project folder)
inline constexpr char kOldSceneBackupSuffix[] = ".before-scenes.bak";  // <名前>.rmpf + this

// The game's scenes, in the .rmpf ("sceneList"): their asset ids in the game's order (what goes into
// the exported game; Game::LoadScene(number) counts in it), the one the game starts with, and the one
// the editor shows.
RM_STRUCT()
struct SceneList {
    RM_PROPERTY() std::vector<AssetRef> scenes;
    RM_PROPERTY() AssetRef open;   // empty: the first
    RM_PROPERTY() AssetRef first;  // the game starts here (empty, or not in the list: the list's first)
    // Scenes of the list an export leaves out (ゲームの設定 ▸ ビルド); the one the game starts with is
    // always in. Play in the editor leaves them out too (PlayScenes: the same numbers as the game).
    RM_PROPERTY() std::vector<AssetRef> notBuilt;
};
// The scene goes into an export (not left out - or the one the game starts with).
bool SceneBuilt(const SceneList& list, const AssetRef& scene);

// The scene shown: `open` when it is in the list, else the first. Empty when the list is.
AssetRef OpenScene(const SceneList& list);
// The scene the game starts with: `first` when it is in the list, else the list's first. Empty when
// the list is.
AssetRef FirstScene(const SceneList& list);
// The scenes of the game, in order: the list's, then the one open in the editor when it is not in it
// (a new scene not saved yet: an empty ref) - what Play can switch to and what an export puts in.
std::vector<AssetRef> GameScenes(const SceneList& list, const AssetRef& open);
// What Play in the editor can switch to, numbered as the exported game numbers its scenes: GameScenes
// without the ones left out of the build - and the one open, when it is left out, last (it plays, but
// the scenes after it keep the exported game's numbers).
std::vector<AssetRef> PlayScenes(const SceneList& list, const AssetRef& open);
// A scene's name, as Game::LoadScene("…") takes it: its file's name without .rmscene
// ("Assets/Scenes/Title.rmscene" -> "Title").
std::string SceneNameOf(std::string_view path);
// The place of the first scene of that name (nullopt: none).
std::optional<size_t> FindSceneByName(const std::vector<std::string>& names, std::string_view name);

// A scene's file: kind and format first, then the scene's keys (Scene::Save) - a "kind" / "format"
// it kept from the file it was read from is replaced by this version's.
std::string SceneFileText(const json::Value& scene);
// The scene (the object to give Scene::Load) in a scene file's text. Refused, with the reason
// (Japanese) in *error: not JSON, not a scene file (kind), or written by a newer version (format) -
// never read, and so never written over.
std::optional<json::Value> ReadSceneFile(std::string_view text, std::string* error);

// A short sign of a file's text (FNV-1a, 64 bits): the scene is written only when the sign of its
// text differs from the one of the file on disk (as read or last written). 0 never comes out.
uint64_t TextSign(std::string_view text);

// The data of a project of before: its scene ("scene"), taken out of `data` (which keeps every other
// key). Nothing (and `data` unchanged) when it has none, or already has "sceneList".
std::optional<json::Value> TakeOldScene(json::Value& data);

// Where a new scene's file goes: Assets/Scenes/<name>.rmscene, or "<name> 2", "<name> 3" ... when
// `taken` says that one is there. `name` must be a valid file name (base/FileName).
template <class Taken>
std::string NewScenePath(std::string_view name, Taken taken) {
    for (int i = 1;; ++i) {
        std::string p = std::string(kScenesFolder) + "/" + std::string(name) + (i == 1 ? "" : " " + std::to_string(i)) + kSceneExtension;
        if (!taken(p) || i > 10000) return p;
    }
}

// One scene's part of a save: what to write for it.
struct SceneSlot {
    std::string guid;     // its asset id ("": none yet - a scene of before, or not saved yet)
    std::string path;     // its file, from the project folder ("Assets/Scenes/Main.rmscene")
    uint64_t onDisk = 0;  // TextSign of its file as read or last written (0: no file yet)
};
struct FileWrite {
    std::string path;  // from the project folder
    std::string text;
};
struct ScenePlan {
    std::vector<FileWrite> files;  // in this order: the .meta of a new scene first, then the scene
    SceneSlot after;               // the slot once the files are written (its id, its file's sign)
};
// What saving `scene` (Scene::Save) into `slot` writes: nothing when the text is the one on disk;
// the scene's file when it changed; and, for a scene without an id, a .meta giving it `newGuid`
// (written before the scene, so a look at the folder never gives it another id).
ScenePlan PlanSceneSave(const json::Value& scene, const SceneSlot& slot, const std::string& newGuid);

// The slot to save into after a save planned `tried` (its `after`) and failed part way - some of its
// files written, some not, which is not known: the file on disk is not known any more (onDisk 0: the
// scene is written again), and a new scene whose .meta did get written (`newMetaThere`) keeps that id
// and file (else it stays without an id: its .meta was written first, so its file is not there either).
SceneSlot SlotAfterFailedSave(const SceneSlot& slot, const SceneSlot& tried, bool newMetaThere);

}  // namespace rm::game_production
