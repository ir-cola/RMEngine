// Tests for the game's scenes kept as files of their own (plugins/rm_engine/game_production/runtime/
// SceneFile): the file's form, the round trip, unknown keys, the projects of before (the scene inside
// the .rmpf) and that a save writes only the scenes that changed.

#include "Test.h"

#include "core/asset/AssetIndex.h"
#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
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
