// Tests for the game's scenes kept as files of their own (plugins/rm_engine/game_production/runtime/
// SceneFile): the file's form, the round trip, unknown keys, the projects of before (the scene inside
// the .rmpf) and that a save writes only the scenes that changed.

#include "Test.h"

#include "core/asset/AssetIndex.h"
#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <map>
#include <set>

using namespace rm;
using namespace rm::game_production;

namespace {

json::Value Parse(const char* text) {
    auto v = json::Parse(text);
    CHECK(v.has_value());
    return v ? std::move(*v) : json::Value();
}

Scene MakeScene() {
    Scene s;
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    const int a = s.Create("床");
    s.AddComponent(a, reflect::TypeOf<space3d::Transform>());
    s.Create("箱", a);
    return s;
}

// The files of a save, as a folder would have them (path -> text).
using Disk = std::map<std::string, std::string>;
void Write(Disk& disk, const ScenePlan& plan) {
    for (const FileWrite& f : plan.files) disk[f.path] = f.text;
}

const std::string kGuid = "0123456789abcdef0123456789abcdef";

}  // namespace

RM_TEST(SceneFile_RoundTrip) {
    Scene scene = MakeScene();
    const json::Value saved = scene.Save();
    const std::string text = SceneFileText(saved);
    std::string err;
    const auto read = ReadSceneFile(text, &err);
    CHECK(read.has_value());
    if (!read) return;
    // kind and format come first.
    CHECK(read->Members().size() >= 2 && read->Members()[0].first == "kind" && read->Members()[1].first == "format");
    CHECK(read->Get("kind").AsString() == kSceneKind && read->Get("format").AsInt() == kSceneFormat);
    Scene again;
    again.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    reflect::Warnings warnings;
    again.Load(*read, &warnings);
    CHECK(warnings.empty());
    CHECK(again.Entities().size() == 2 && again.Entities()[0].data.name == "床" && again.Entities()[1].data.parent == again.Entities()[0].data.id);
    // Written again: the same text (a scene that did not change is not written - its sign is the same).
    CHECK(SceneFileText(again.Save()) == text);
    CHECK(TextSign(SceneFileText(again.Save())) == TextSign(text));
}

RM_TEST(SceneFile_Refused) {
    std::string err;
    CHECK(!ReadSceneFile("{ broken", &err) && !err.empty());
    CHECK(!ReadSceneFile(R"({"kind": "rm_engine.game_production.prefab", "format": 1, "entities": []})", &err));
    CHECK(!ReadSceneFile(R"([1, 2])", &err));
    // A newer version's: not read (and so never written over).
    CHECK(!ReadSceneFile(R"({"kind": "rm_engine.scene", "format": 99, "entities": []})", &err));
    CHECK(ReadSceneFile(R"({"kind": "rm_engine.scene", "format": 1})", &err).has_value());
    CHECK(TextSign("") != 0 && TextSign("a") != TextSign("b"));
}

RM_TEST(SceneFile_UnknownKeysStay) {
    // In the scene file: its own keys and an entity's.
    const char* text = R"({"kind": "rm_engine.scene", "format": 1, "lighting": {"sky": "blue"}, "nextId": 3,
        "entities": [ {"id": 1, "parent": 0, "name": "A", "active": true, "glow": 5, "components": []} ]})";
    std::string err;
    const auto read = ReadSceneFile(text, &err);
    CHECK(read.has_value());
    if (!read) return;
    Scene s;
    s.Load(*read);
    const auto back = ReadSceneFile(SceneFileText(s.Save()), &err);
    CHECK(back && back->Get("lighting").Get("sky").AsString() == "blue");
    CHECK(back && back->Get("entities").Items()[0].Get("glow").AsInt() == 5);
    // In the .rmpf's list.
    SceneList list;
    reflect::Unknowns unknowns;
    reflect::Load(list, Parse(R"({"scenes": ["0123456789abcdef0123456789abcdef"], "open": "", "folders": ["x"]})"), &unknowns);
    CHECK(list.scenes.size() == 1 && list.scenes[0].guid == kGuid);
    const json::Value saved = reflect::Save(list, &unknowns);
    CHECK(saved.Get("folders").Items().size() == 1 && saved.Get("folders").Items()[0].AsString() == "x");
}

RM_TEST(SceneFile_OpenScene) {
    SceneList list;
    CHECK(OpenScene(list).Empty());
    list.scenes = {AssetRef{"a"}, AssetRef{"b"}};
    CHECK(OpenScene(list).guid == "a");
    list.open = AssetRef{"b"};
    CHECK(OpenScene(list).guid == "b");
    list.open = AssetRef{"gone"};  // not in the list: the first
    CHECK(OpenScene(list).guid == "a");
}

RM_TEST(SceneFile_NewPath) {
    std::set<std::string> taken;
    auto isTaken = [&](const std::string& p) { return taken.contains(p); };
    CHECK(NewScenePath("Main", isTaken) == "Assets/Scenes/Main.rmscene");
    taken = {"Assets/Scenes/Main.rmscene", "Assets/Scenes/Main 2.rmscene"};
    CHECK(NewScenePath("Main", isTaken) == "Assets/Scenes/Main 3.rmscene");
}

RM_TEST(SceneFile_OnlyChangedScenesAreWritten) {
    Scene scene = MakeScene();
    Disk disk;
    // A new scene: its .meta (with the new id) first, then the scene.
    const SceneSlot fresh{"", "Assets/Scenes/Main.rmscene", 0};
    const ScenePlan first = PlanSceneSave(scene.Save(), fresh, kGuid);
    CHECK(first.files.size() == 2);
    CHECK(first.files[0].path == "Assets/Scenes/Main.rmscene.meta" && first.files[1].path == "Assets/Scenes/Main.rmscene");
    CHECK(asset::GuidInMeta(first.files[0].text) == kGuid);
    CHECK(first.after.guid == kGuid && first.after.onDisk == TextSign(first.files[1].text));
    Write(disk, first);
    // Nothing changed: nothing written.
    const ScenePlan same = PlanSceneSave(scene.Save(), first.after, "ffffffffffffffffffffffffffffffff");
    CHECK(same.files.empty() && same.after.guid == kGuid && same.after.onDisk == first.after.onDisk);
    // Changed: the scene only (its id stays, no .meta).
    scene.Create("木");
    const ScenePlan changed = PlanSceneSave(scene.Save(), first.after, "ffffffffffffffffffffffffffffffff");
    CHECK(changed.files.size() == 1 && changed.files[0].path == "Assets/Scenes/Main.rmscene");
    CHECK(changed.after.guid == kGuid && changed.after.onDisk != first.after.onDisk);
    Write(disk, changed);
    // Changed back (an undo): written again, as it differs from the file now.
    const ScenePlan back = PlanSceneSave(MakeScene().Save(), changed.after, "");
    CHECK(back.files.size() == 1);
    // A file read from the disk and saved untouched: not written.
    std::string err;
    const auto read = ReadSceneFile(disk["Assets/Scenes/Main.rmscene"], &err);
    CHECK(read.has_value());
    Scene loaded;
    loaded.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    if (read) loaded.Load(*read);
    const SceneSlot asRead{kGuid, "Assets/Scenes/Main.rmscene", TextSign(disk["Assets/Scenes/Main.rmscene"])};
    CHECK(PlanSceneSave(loaded.Save(), asRead, "").files.empty());
}

// A save that failed part way (the scene written, the .rmpf not - or the other way): the next save
// writes the scene again, even when it is back to what the slot last knew (an undo); a new scene whose
// .meta got written keeps that id and file.
RM_TEST(SceneFile_AfterAFailedSave) {
    const Scene scene = MakeScene();
    const SceneSlot read{kGuid, "Assets/Scenes/Main.rmscene", TextSign(SceneFileText(scene.Save()))};
    SceneSlot tried = read;
    tried.onDisk = 12345;  // (another text was being written)
    const SceneSlot after = SlotAfterFailedSave(read, tried, false);
    CHECK(after.guid == kGuid && after.path == read.path && after.onDisk == 0);
    CHECK(PlanSceneSave(scene.Save(), after, "").files.size() == 1);  // written again, though unchanged
    // A new scene: its .meta there - its id and file kept; not there - still none (a .meta at the next save).
    const SceneSlot fresh{"", "Assets/Scenes/Main.rmscene", 0};
    const SceneSlot triedNew{kGuid, "Assets/Scenes/Main.rmscene", 99};
    const SceneSlot kept = SlotAfterFailedSave(fresh, triedNew, true);
    CHECK(kept.guid == kGuid && kept.path == triedNew.path && kept.onDisk == 0);
    const SceneSlot none = SlotAfterFailedSave(fresh, triedNew, false);
    CHECK(none.guid.empty() && none.onDisk == 0 && PlanSceneSave(scene.Save(), none, kGuid).files.size() == 2);
}

RM_TEST(SceneFile_ProjectOfBefore) {
    // 0.0.1: the scene inside the .rmpf's game data, next to the settings (and a key of a newer version).
    json::Value data = Parse(R"({"settings": {"space": "3d"}, "future": 1,
        "scene": {"nextId": 3, "entities": [ {"id": 1, "parent": 0, "name": "カメラ", "active": true, "components": []},
                                             {"id": 2, "parent": 1, "name": "子", "active": false, "components": []} ]}})");
    const auto old = TakeOldScene(data);
    CHECK(old.has_value());
    CHECK(!data.Find("scene") && data.Get("settings").Get("space").AsString() == "3d" && data.Get("future").AsInt() == 1);
    if (!old) return;
    Scene scene;
    scene.Load(*old);
    // Split off: written to its file with a new id, read back the same.
    Disk disk;
    const ScenePlan plan = PlanSceneSave(scene.Save(), {"", "Assets/Scenes/Main.rmscene", 0}, kGuid);
    Write(disk, plan);
    std::string err;
    const auto read = ReadSceneFile(disk["Assets/Scenes/Main.rmscene"], &err);
    CHECK(read.has_value());
    Scene again;
    if (read) again.Load(*read);
    CHECK(again.Entities().size() == 2 && again.Entities()[1].data.name == "子" && !again.Entities()[1].data.active);
    CHECK(json::Write(again.Save().Get("entities")) == json::Write(scene.Save().Get("entities")));
    CHECK(again.Save().Get("nextId").AsInt() == 3);
    CHECK(asset::GuidInMeta(disk["Assets/Scenes/Main.rmscene.meta"]) == kGuid);

    // Data already split: nothing to take (and "scene" there is left alone).
    json::Value now = Parse(R"({"settings": {}, "sceneList": {"scenes": []}, "scene": {"entities": []}})");
    CHECK(!TakeOldScene(now) && now.Find("scene"));
    json::Value none = Parse(R"({"settings": {}})");
    CHECK(!TakeOldScene(none));
}

// The game's scenes: the list's order, the one open when it is not in it (last), the first one, the
// names Game::LoadScene takes.
RM_TEST(SceneFile_GameScenesAndNames) {
    const AssetRef a{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}, b{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}, c{"cccccccccccccccccccccccccccccccc"};
    SceneList list;
    list.scenes = {a, b};
    CHECK(FirstScene(list) == a);  // none chosen: the list's first
    list.first = b;
    CHECK(FirstScene(list) == b);
    list.first = c;  // not in the list (taken out): the list's first
    CHECK(FirstScene(list) == a);
    CHECK(FirstScene(SceneList{}).Empty());
    CHECK((GameScenes(list, b) == std::vector<AssetRef>{a, b}));
    CHECK((GameScenes(list, c) == std::vector<AssetRef>{a, b, c}));            // open, not in the list: last
    CHECK((GameScenes(list, AssetRef{}) == std::vector<AssetRef>{a, b, AssetRef{}}));  // a new scene not saved yet
    // Play numbers them as the exported game: without the ones left out of the build; the open one, left
    // out, last (2026-11-19: Game::LoadScene(2) went to another scene in the game).
    SceneList three;
    three.scenes = {a, b, c};
    three.notBuilt = {b};
    CHECK((PlayScenes(three, a) == std::vector<AssetRef>{a, c}));
    CHECK((PlayScenes(three, b) == std::vector<AssetRef>{a, c, b}));
    CHECK((PlayScenes(three, AssetRef{}) == std::vector<AssetRef>{a, c, AssetRef{}}));
    three.notBuilt = {a};  // (the first is always built)
    CHECK((PlayScenes(three, c) == std::vector<AssetRef>{a, b, c}));
    list.scenes = {a, a, AssetRef{}, b};
    CHECK((GameScenes(list, a) == std::vector<AssetRef>{a, b}));  // each once, no empty ids
    CHECK(SceneNameOf("Assets/Scenes/Title.rmscene") == "Title" && SceneNameOf("Stage 2.rmscene") == "Stage 2" && SceneNameOf("Assets\\x.rmscene") == "x");
    const std::vector<std::string> names{"Title", "Stage", "Stage"};
    CHECK(FindSceneByName(names, "Stage") == std::optional<size_t>(1) && !FindSceneByName(names, "stage") && !FindSceneByName(names, ""));
    // The list keeps "first" (and keys of a newer version) through a save and a load.
    SceneList saved;
    saved.scenes = {a, b};
    saved.first = b;
    SceneList back;
    reflect::Load(back, reflect::Save(saved));
    CHECK(back.first == b && back.scenes == saved.scenes);
}

// The camera a scene starts with: the game settings' 最初のカメラ when it is a camera of this scene,
// else the first running entity with a camera component.
RM_TEST(SceneFile_SceneCamera) {
    Scene s;
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    const std::vector<std::string> cameras{reflect::TypeOf<space3d::Transform>().name};  // (a type standing for a camera)
    const int box = s.Create("箱");
    const int off = s.Create("止めたカメラ");
    s.AddComponent(off, reflect::TypeOf<space3d::Transform>());
    s.Find(off)->data.active = false;
    const int cam = s.Create("カメラ");
    s.AddComponent(cam, reflect::TypeOf<space3d::Transform>());
    CHECK(SceneCamera(s, cameras, static_cast<uint64_t>(cam)) == static_cast<uint64_t>(cam));
    CHECK(SceneCamera(s, cameras, static_cast<uint64_t>(box)) == static_cast<uint64_t>(cam));  // not a camera: the first running one
    CHECK(SceneCamera(s, cameras, 999) == static_cast<uint64_t>(cam));                         // not in this scene
    CHECK(SceneCamera(s, {}, static_cast<uint64_t>(cam)) == 0);                                // no camera types: none
}
