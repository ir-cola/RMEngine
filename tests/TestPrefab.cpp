// Tests for several components of one type on an entity, and for prefabs (game production's
// runtime/Scene + runtime/Prefab): making one, placing it, what a placed one changed (overrides),
// and a changed prefab reaching the placed ones.

#include "Test.h"

#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/space3d/runtime/Transform.h"

using namespace rm;
using namespace rm::game_production;

namespace {

// The prefab files, by their asset ids.
const AssetRef kCube{"0123456789abcdef0123456789abcdef"};
const AssetRef kRoom{"fedcba9876543210fedcba9876543210"};

const reflect::TypeDesc& T() { return reflect::TypeOf<space3d::Transform>(); }
const reflect::TypeDesc& Sp() { return reflect::TypeOf<game_x_3d::Spin>(); }

space3d::Transform& Tf(Scene& s, int id) { return *static_cast<space3d::Transform*>(s.Find(id)->FindComponent(T().name)->value.Get()); }
game_x_3d::Spin& SpinOf(Scene& s, int id, size_t nth = 0) {
    return *static_cast<game_x_3d::Spin*>(s.Find(id)->FindComponent(Sp().name, nth)->value.Get());
}

Scene Empty() {
    Scene s;
    s.SetComponentTypes({&T(), &Sp()}, {&Sp()});
    return s;
}

// Cube (Transform, Spin)
//   Lid (Transform)
Scene WithCube(int* cube, int* lid) {
    Scene s = Empty();
    *cube = s.Create("Cube");
    s.AddComponent(*cube, T());
    s.AddComponent(*cube, Sp());
    *lid = s.Create("Lid", *cube);
    s.AddComponent(*lid, T());
    Tf(s, *lid).position = {0, 1, 0};
    return s;
}

}  // namespace

RM_TEST(Components_Multiple_AddRemoveMoveByNumber) {
    Scene s = Empty();
    const int e = s.Create("e");
    CHECK(s.AddComponent(e, T()));
    CHECK(!s.AddComponent(e, T()));  // one Transform only
    CHECK(s.AddComponent(e, Sp()));
    CHECK(s.AddComponent(e, Sp()));
    CHECK(s.AddComponent(e, Sp()));
    SceneEntity& en = *s.Find(e);
    CHECK(en.CountComponents(Sp().name) == 3);
    SpinOf(s, e, 0).degreesPerSecond = {1, 0, 0};
    SpinOf(s, e, 1).degreesPerSecond = {2, 0, 0};
    SpinOf(s, e, 2).degreesPerSecond = {3, 0, 0};
    CHECK(en.NthOf(en.components[2]) == 1);
    // The second one goes first: it is the first of its type now.
    CHECK(s.MoveComponent(e, Sp().name, -1, 1));
    CHECK(SpinOf(s, e, 0).degreesPerSecond.x == 2 && SpinOf(s, e, 1).degreesPerSecond.x == 1);
    CHECK(s.MoveComponent(e, Sp().name, -1, 0));  // above the Transform
    CHECK(s.Find(e)->components[0].type == Sp().name);
    // Removing the middle one keeps the others.
    CHECK(s.RemoveComponent(e, Sp().name, 1));
    CHECK(SpinOf(s, e, 0).degreesPerSecond.x == 2 && SpinOf(s, e, 1).degreesPerSecond.x == 3);
    CHECK(!s.RemoveComponent(e, Sp().name, 5));
}

RM_TEST(Components_Multiple_SaveLoadAndOldFiles) {
    Scene s = Empty();
    const int e = s.Create("e");
    s.AddComponent(e, T());
    s.AddComponent(e, Sp());
    s.AddComponent(e, Sp());
    SpinOf(s, e, 1).degreesPerSecond = {0, 0, 7};
    const json::Value saved = s.Save();
    Scene t = Empty();
    reflect::Warnings w;
    t.Load(saved, &w);
    CHECK(w.empty());
    CHECK(t.Find(e)->CountComponents(Sp().name) == 2 && SpinOf(t, e, 1).degreesPerSecond.z == 7);
    CHECK(t.Save() == saved);
    // The same file where the type allows only one (an older plugin): the second is kept as read.
    Scene one;
    one.SetComponentTypes({&T(), &Sp()});
    w.clear();
    one.Load(saved, &w);
    CHECK(w.size() == 1);
    CHECK(one.Find(e)->components[1].Known() && !one.Find(e)->components[2].Known());
    CHECK(one.Save() == saved);
    // A file from before (one of each, the same form) reads as it was.
    Scene old = Empty();
    json::Value before = saved;
    before.Find("entities")->At(0).Find("components")->Items().pop_back();
    old.Load(before, &w);
    CHECK(old.Find(e)->CountComponents(Sp().name) == 1 && old.Save() == before);
}

RM_TEST(Prefab_MakeAndPlace) {
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value prefab = MakePrefab(s, cube, &sources);
    CHECK(IsPrefab(prefab));
    const auto& list = prefab.Get("entities").Items();
    CHECK(list.size() == 2);
    CHECK(list[0].Get("name").AsString() == "Cube" && list[0].Get("parent").AsInt() == 0);
    CHECK(list[1].Get("parent").AsInt() == list[0].Get("id").AsInt());
    CHECK(sources.size() == 2);
    CHECK(!list[0].Find("prefab"));
    CHECK(PrefabName("Assets/Things/Cube.rmprefab") == "Cube");

    // The entity becomes a placed prefab of the file.
    CHECK(s.LinkPrefab(cube, kCube, sources));
    CHECK(s.PrefabRootOf(lid) == cube && s.PrefabRootOf(cube) == cube);
    CHECK((s.PrefabMembers(cube) == std::vector<int>{cube, lid}));
    CHECK(CountOverrides(Overrides(s, cube, prefab)) > 0);  // the root's own name and 有効 always count

    // Placed twice more: new ids, linked, the same shape.
    const int a = s.PlacePrefab(prefab, kCube);
    const int b = s.PlacePrefab(prefab, kCube, a);  // after a's Lid
    CHECK(a > 0 && b > 0 && a != cube);
    CHECK(s.Find(b)->data.parent == a);
    CHECK((s.PrefabRoots(kCube.guid) == std::vector<int>{cube, a, b}));
    CHECK(s.PrefabRootOf(s.Children(a)[0]) == a);  // a's Lid (b is a root of its own)
    // b sits inside a, but is not one of a's.
    const auto members = s.PrefabMembers(a);
    CHECK(members.size() == 2 && std::find(members.begin(), members.end(), b) == members.end());
    // Saved and read back: the links stay.
    Scene t = Empty();
    t.Load(s.Save());
    CHECK(t.Save() == s.Save());
    CHECK(t.Find(b)->prefab.Root() && t.Find(b)->prefab.asset == kCube);
    // Not a prefab: nothing placed.
    CHECK(s.PlacePrefab(s.Save(), kCube) == 0);
    // Unlinked: plain entities again.
    CHECK(s.UnlinkPrefab(b));
    CHECK(!s.Find(b)->prefab.Linked() && s.PrefabRootOf(b) == 0);
}

RM_TEST(Prefab_ChangedPrefabReachesPlacedOnesButNotTheirOverrides) {
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value v1 = MakePrefab(s, cube, &sources);
    s.LinkPrefab(cube, kCube, sources);
    const int placed = s.PlacePrefab(v1, kCube);
    const int placedLid = s.Children(placed)[0];
    // The placed one: moved (its root's place is always its own), its Lid renamed, its spin faster,
    // a second spin added, and a plain child added under the Lid.
    Tf(s, placed).position = {5, 0, 0};
    s.Find(placedLid)->data.name = "My lid";
    SpinOf(s, placed).degreesPerSecond = {0, 360, 0};
    s.AddComponent(placedLid, Sp());
    const int extra = s.Create("extra", placedLid);
    const json::Value ov = Overrides(s, placed, v1, std::vector<std::string>{T().name});

    // The prefab changes (from the first one): the Lid goes higher, a new child, Cube's spin about X.
    Tf(s, lid).position = {0, 3, 0};
    Tf(s, cube).position = {9, 9, 9};  // the root's place: does not reach the placed ones
    const int knob = s.Create("Knob", cube);
    SpinOf(s, cube).degreesPerSecond = {45, 90, 0};
    const json::Value v2 = MakePrefab(s, cube, &sources);
    s.LinkPrefab(cube, kCube, sources);
    CHECK(s.Find(knob)->prefab.Linked());

    reflect::Warnings w;
    CHECK(s.ApplyPrefab(placed, v2, ov, &w));
    CHECK(w.empty());
    // The ids stayed; the new child came with a new id.
    CHECK(s.Find(placedLid) && s.Find(placedLid)->data.parent == placed);
    const auto kids = s.Children(placed);
    CHECK(kids.size() == 2 && s.Find(kids[1])->data.name == "Knob");
    // From the prefab: the Lid's height. Kept: the place, the name, the spin, the added spin, the child.
    CHECK(Tf(s, placedLid).position.y == 3);
    CHECK(Tf(s, placed).position.x == 5 && Tf(s, placed).position.y == 0);
    CHECK(s.Find(placedLid)->data.name == "My lid");
    CHECK(SpinOf(s, placed).degreesPerSecond.y == 360 && SpinOf(s, placed).degreesPerSecond.x == 0);
    CHECK(s.Find(placedLid)->CountComponents(Sp().name) == 1);
    CHECK(s.Find(extra) && s.Find(extra)->data.parent == placedLid && !s.Find(extra)->prefab.Linked());

    // Overrides again against v2: the same changes, nothing more.
    const json::Value ov2 = Overrides(s, placed, v2, std::vector<std::string>{T().name});
    CHECK(CountOverrides(ov2) == CountOverrides(ov));
}

RM_TEST(Prefab_RemovedThingsStayRemoved) {
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value v1 = MakePrefab(s, cube, &sources);
    const int placed = s.PlacePrefab(v1, kCube);
    const int placedLid = s.Children(placed)[0];
    const int keep = s.Create("keep", placedLid);  // plain, under the Lid that goes
    s.RemoveComponent(placed, Sp().name);
    s.Delete(std::vector<int>{placedLid});
    s.Create("keep2", placed);
    (void)keep;
    const json::Value ov = Overrides(s, placed, v1);
    CHECK(ov.Get("removedEntities").Items().size() == 1 && ov.Get("removedComponents").Items().size() == 1);
    // The prefab again (as it was): the Lid and the spin do not come back.
    CHECK(s.ApplyPrefab(placed, v1, ov));
    CHECK(s.Children(placed).size() == 1 && s.Find(s.Children(placed)[0])->data.name == "keep2");
    CHECK(s.Find(placed)->CountComponents(Sp().name) == 0);
    // Without the overrides: everything from the prefab (the new Lid has a new id).
    CHECK(s.ApplyPrefab(placed, v1, json::Value::MakeObject()));
    CHECK(s.Children(placed).size() == 2 && s.Find(placed)->CountComponents(Sp().name) == 1);
    CHECK(!s.Find(placedLid));
}

RM_TEST(Prefab_OldPathIsReadAndWrittenAsAsset) {
    // A scene from before assets had ids: the root names its prefab file by a path.
    json::Value scene = json::Value::MakeObject();
    json::Value list = json::Value::MakeArray();
    json::Value e = json::Value::MakeObject();
    e.Set("id", 1);
    e.Set("name", "Cube");
    json::Value link = json::Value::MakeObject();
    link.Set("source", 1);
    link.Set("path", "Assets/Cube.rmprefab");
    link.Set("overrides", json::Value::MakeObject());
    e.Set("prefab", std::move(link));
    list.Push(std::move(e));
    scene.Set("entities", std::move(list));
    Scene s = Empty();
    s.Load(scene);
    CHECK(s.Find(1)->prefab.Root() && s.Find(1)->prefab.asset.guid == "Assets/Cube.rmprefab");
    const json::Value saved = s.Save().Get("entities").At(0).Get("prefab");
    CHECK(saved.Get("asset").AsString() == "Assets/Cube.rmprefab" && !saved.Find("path"));
}

RM_TEST(Prefab_Nested_KeptAsALinkAndFilledFromItsOwnFile) {
    const std::vector<std::string> placement{T().name};
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value cube1 = MakePrefab(s, cube, &sources);
    s.LinkPrefab(cube, kCube, sources);

    // Room: Floor, and a Cube placed in it whose lid was renamed there.
    const int room = s.Create("Room");
    s.AddComponent(room, T());
    s.Create("Floor", room);
    const int inRoom = s.PlacePrefab(cube1, kCube, room);
    const int inRoomLid = s.Children(inRoom)[0];
    s.Find(inRoomLid)->data.name = "Lid in room";
    s.Find(inRoom)->prefab.overrides = Overrides(s, inRoom, cube1, placement);  // as the editor does first

    std::unordered_map<int, int> roomSources;
    const json::Value room1 = MakePrefab(s, room, &roomSources);
    // The nested cube is its root only, linked to its own file with what was changed on it.
    CHECK(room1.Get("entities").Items().size() == 3);
    CHECK((NestedAssets(room1) == std::vector<std::string>{kCube.guid}));
    const json::Value& nestedInFile = room1.Get("entities").At(2);
    CHECK(nestedInFile.Get("prefab").Get("asset").AsString() == kCube.guid);
    CHECK(!roomSources.contains(inRoomLid));
    CHECK(s.LinkPrefab(room, kRoom, roomSources));
    CHECK(s.Find(inRoom)->prefab.Root() && s.Find(inRoom)->prefab.outer == roomSources[inRoom]);
    CHECK((s.NestedPrefabs(room) == std::vector<int>{inRoom}));
    const auto members = s.PrefabMembers(room);
    CHECK(std::find(members.begin(), members.end(), inRoomLid) == members.end());
    CHECK(s.PrefabRootOf(inRoomLid) == inRoom);

    // Placed again: the nested cube comes as its root; filled from the cube's file with its overrides.
    const int room2 = s.PlacePrefab(room1, kRoom);
    const auto nested = s.NestedPrefabs(room2);
    CHECK(nested.size() == 1 && s.Children(nested[0]).empty());
    const int cube2 = nested[0];
    CHECK(s.ApplyPrefab(cube2, cube1, s.Find(cube2)->prefab.overrides));
    CHECK(s.Children(cube2).size() == 1 && s.Find(s.Children(cube2)[0])->data.name == "Lid in room");
    const int cube2Lid = s.Children(cube2)[0];
    CHECK((s.PrefabRoots(kCube.guid) == std::vector<int>{cube, inRoom, cube2}));  // outer ones first

    // The cube changes: every placed cube follows, the nested ones too, keeping their own changes.
    Tf(s, lid).position = {0, 5, 0};
    const json::Value cube2File = MakePrefab(s, cube, &sources);
    for (const int r : s.PrefabRoots(kCube.guid)) {
        if (r != cube) s.ApplyPrefab(r, cube2File, Overrides(s, r, cube1, placement));
    }
    CHECK(Tf(s, cube2Lid).position.y == 5 && s.Find(cube2Lid)->data.name == "Lid in room");
    CHECK(Tf(s, inRoomLid).position.y == 5);

    // The room changes (its floor renamed): the nested cube stays with its insides and its id.
    s.Find(s.Children(room)[0])->data.name = "Stone floor";
    s.Find(inRoom)->prefab.overrides = Overrides(s, inRoom, cube2File, placement);
    const json::Value room2File = MakePrefab(s, room, &roomSources);
    s.Find(cube2)->prefab.overrides = Overrides(s, cube2, cube2File, placement);
    CHECK(s.ApplyPrefab(room2, room2File, Overrides(s, room2, room1, placement)));
    CHECK(s.Find(s.Children(room2)[0])->data.name == "Stone floor");
    CHECK(s.Find(cube2) && s.Find(cube2Lid) && s.Find(cube2Lid)->data.parent == cube2);
    CHECK(s.Find(cube2)->prefab.Root() && s.Find(cube2)->prefab.outer > 0);

    // A copy of the nested cube alone: placed by hand. A copy of a lid alone: a plain entity.
    const auto copies = s.Duplicate(std::vector<int>{cube2, cube2Lid});
    CHECK(s.Find(copies[0])->prefab.Root() && s.Find(copies[0])->prefab.outer == 0);
    const auto lidCopy = s.Duplicate(std::vector<int>{cube2Lid});
    CHECK(!s.Find(lidCopy[0])->prefab.Linked());
    s.Delete(copies);

    // The cube taken out of the room: it goes from the placed room with everything in it.
    s.Delete(std::vector<int>{inRoom});
    const json::Value room3File = MakePrefab(s, room, &roomSources);
    CHECK(s.ApplyPrefab(room2, room3File, Overrides(s, room2, room2File, placement)));
    CHECK(!s.Find(cube2) && !s.Find(cube2Lid));
    CHECK(s.NestedPrefabs(room2).empty());
    CHECK(s.Find(lidCopy[0]));  // the plain copy stays where it was put
}
