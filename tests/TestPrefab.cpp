// 1 つのエンティティに同じ型の部品が複数あること、そして Prefab (ゲーム制作の
// runtime/Scene + runtime/Prefab) のテスト: 作る、置く、置いたものが変えたこと (上書き)、
// 変えた Prefab が置いたものに届くこと。

#include "Test.h"

#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/space3d/runtime/Transform.h"

using namespace rm;
using namespace rm::game_production;

namespace {

// Prefab のファイル。アセット ID で引く。
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
    CHECK(!s.AddComponent(e, T()));  // Transform は 1 つだけ
    CHECK(s.AddComponent(e, Sp()));
    CHECK(s.AddComponent(e, Sp()));
    CHECK(s.AddComponent(e, Sp()));
    SceneEntity& en = *s.Find(e);
    CHECK(en.CountComponents(Sp().name) == 3);
    SpinOf(s, e, 0).degreesPerSecond = {1, 0, 0};
    SpinOf(s, e, 1).degreesPerSecond = {2, 0, 0};
    SpinOf(s, e, 2).degreesPerSecond = {3, 0, 0};
    CHECK(en.NthOf(en.components[2]) == 1);
    // 2 つ目が先に来る: 今はその型の最初のもの。
    CHECK(s.MoveComponent(e, Sp().name, -1, 1));
    CHECK(SpinOf(s, e, 0).degreesPerSecond.x == 2 && SpinOf(s, e, 1).degreesPerSecond.x == 1);
    CHECK(s.MoveComponent(e, Sp().name, -1, 0));  // Transform の上
    CHECK(s.Find(e)->components[0].type == Sp().name);
    // 真ん中を消しても、他は残る。
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
    // 型が 1 つしか許さないのに同じファイル (古いプラグイン): 2 つ目は読んだまま残す。
    Scene one;
    one.SetComponentTypes({&T(), &Sp()});
    w.clear();
    one.Load(saved, &w);
    CHECK(w.size() == 1);
    CHECK(one.Find(e)->components[1].Known() && !one.Find(e)->components[2].Known());
    CHECK(one.Save() == saved);
    // 前のファイル (各 1 つ、同じ形) は前のとおり読める。
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

    // エンティティは、そのファイルの置いた Prefab になる。
    CHECK(s.LinkPrefab(cube, kCube, sources));
    CHECK(s.PrefabRootOf(lid) == cube && s.PrefabRootOf(cube) == cube);
    CHECK((s.PrefabMembers(cube) == std::vector<int>{cube, lid}));
    CHECK(CountOverrides(Overrides(s, cube, prefab)) > 0);  // ルート自身の名前と 有効 は常に数える

    // さらに 2 回置く: 新しい ID、つながっている、同じ形。
    const int a = s.PlacePrefab(prefab, kCube);
    const int b = s.PlacePrefab(prefab, kCube, a);  // a の Lid の後
    CHECK(a > 0 && b > 0 && a != cube);
    CHECK(s.Find(b)->data.parent == a);
    CHECK((s.PrefabRoots(kCube.guid) == std::vector<int>{cube, a, b}));
    CHECK(s.PrefabRootOf(s.Children(a)[0]) == a);  // a の Lid (b は自分のルート)
    // b は a の中にあるが、a のものではない。
    const auto members = s.PrefabMembers(a);
    CHECK(members.size() == 2 && std::find(members.begin(), members.end(), b) == members.end());
    // 保存して読み戻す: つながりは残る。
    Scene t = Empty();
    t.Load(s.Save());
    CHECK(t.Save() == s.Save());
    CHECK(t.Find(b)->prefab.Root() && t.Find(b)->prefab.asset == kCube);
    // Prefab ではない: 何も置かない。
    CHECK(s.PlacePrefab(s.Save(), kCube) == 0);
    // つながりを切る: また普通のエンティティ。
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
    // 置いたもの: 動かした (ルートの場所は常に自分のもの)、Lid の名前を変えた、spin を速くした、
    // spin をもう 1 つ足した、Lid の下に普通の子を足した。
    Tf(s, placed).position = {5, 0, 0};
    s.Find(placedLid)->data.name = "My lid";
    SpinOf(s, placed).degreesPerSecond = {0, 360, 0};
    s.AddComponent(placedLid, Sp());
    const int extra = s.Create("extra", placedLid);
    const json::Value ov = Overrides(s, placed, v1, std::vector<std::string>{T().name});

    // Prefab が変わる (最初のものから): Lid が高くなる、新しい子、Cube の spin が X まわり。
    Tf(s, lid).position = {0, 3, 0};
    Tf(s, cube).position = {9, 9, 9};  // ルートの場所: 置いたものには届かない
    const int knob = s.Create("Knob", cube);
    SpinOf(s, cube).degreesPerSecond = {45, 90, 0};
    const json::Value v2 = MakePrefab(s, cube, &sources);
    s.LinkPrefab(cube, kCube, sources);
    CHECK(s.Find(knob)->prefab.Linked());

    reflect::Warnings w;
    CHECK(s.ApplyPrefab(placed, v2, ov, &w));
    CHECK(w.empty());
    // ID は変わらない。新しい子は新しい ID で来た。
    CHECK(s.Find(placedLid) && s.Find(placedLid)->data.parent == placed);
    const auto kids = s.Children(placed);
    CHECK(kids.size() == 2 && s.Find(kids[1])->data.name == "Knob");
    // Prefab から: Lid の高さ。残るもの: 場所、名前、spin、足した spin、子。
    CHECK(Tf(s, placedLid).position.y == 3);
    CHECK(Tf(s, placed).position.x == 5 && Tf(s, placed).position.y == 0);
    CHECK(s.Find(placedLid)->data.name == "My lid");
    CHECK(SpinOf(s, placed).degreesPerSecond.y == 360 && SpinOf(s, placed).degreesPerSecond.x == 0);
    CHECK(s.Find(placedLid)->CountComponents(Sp().name) == 1);
    CHECK(s.Find(extra) && s.Find(extra)->data.parent == placedLid && !s.Find(extra)->prefab.Linked());

    // v2 に対してもう一度上書きを取る: 同じ変更で、それ以上は無い。
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
    const int keep = s.Create("keep", placedLid);  // 普通のもの。消える Lid の下
    s.RemoveComponent(placed, Sp().name);
    s.Delete(std::vector<int>{placedLid});
    s.Create("keep2", placed);
    (void)keep;
    const json::Value ov = Overrides(s, placed, v1);
    CHECK(ov.Get("removedEntities").Items().size() == 1 && ov.Get("removedComponents").Items().size() == 1);
    // Prefab をもう一度 (元のまま): Lid と spin は戻らない。
    CHECK(s.ApplyPrefab(placed, v1, ov));
    CHECK(s.Children(placed).size() == 1 && s.Find(s.Children(placed)[0])->data.name == "keep2");
    CHECK(s.Find(placed)->CountComponents(Sp().name) == 0);
    // 上書き無し: すべて Prefab から (新しい Lid は新しい ID)。
    CHECK(s.ApplyPrefab(placed, v1, json::Value::MakeObject()));
    CHECK(s.Children(placed).size() == 2 && s.Find(placed)->CountComponents(Sp().name) == 1);
    CHECK(!s.Find(placedLid));
}

RM_TEST(Prefab_OldPathIsReadAndWrittenAsAsset) {
    // アセットに ID が無かった頃のシーン: ルートはパスで Prefab のファイルを指す。
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

    // Room: Floor と、その中に置いた Cube (その中で lid の名前を変えた)。
    const int room = s.Create("Room");
    s.AddComponent(room, T());
    s.Create("Floor", room);
    const int inRoom = s.PlacePrefab(cube1, kCube, room);
    const int inRoomLid = s.Children(inRoom)[0];
    s.Find(inRoomLid)->data.name = "Lid in room";
    s.Find(inRoom)->prefab.overrides = Overrides(s, inRoom, cube1, placement);  // エディターが最初にするとおり

    std::unordered_map<int, int> roomSources;
    const json::Value room1 = MakePrefab(s, room, &roomSources);
    // 入れ子の cube はルートだけで、自分のファイルにつながり、変えたことを持つ。
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

    // もう一度置く: 入れ子の cube はルートとして来る。cube のファイルから、その上書きとともに埋める。
    const int room2 = s.PlacePrefab(room1, kRoom);
    const auto nested = s.NestedPrefabs(room2);
    CHECK(nested.size() == 1 && s.Children(nested[0]).empty());
    const int cube2 = nested[0];
    CHECK(s.ApplyPrefab(cube2, cube1, s.Find(cube2)->prefab.overrides));
    CHECK(s.Children(cube2).size() == 1 && s.Find(s.Children(cube2)[0])->data.name == "Lid in room");
    const int cube2Lid = s.Children(cube2)[0];
    CHECK((s.PrefabRoots(kCube.guid) == std::vector<int>{cube, inRoom, cube2}));  // 外側が先

    // cube が変わる: 置いた cube はすべて追う。入れ子のものも、自分の変更は保ったまま。
    Tf(s, lid).position = {0, 5, 0};
    const json::Value cube2File = MakePrefab(s, cube, &sources);
    for (const int r : s.PrefabRoots(kCube.guid)) {
        if (r != cube) s.ApplyPrefab(r, cube2File, Overrides(s, r, cube1, placement));
    }
    CHECK(Tf(s, cube2Lid).position.y == 5 && s.Find(cube2Lid)->data.name == "Lid in room");
    CHECK(Tf(s, inRoomLid).position.y == 5);

    // room が変わる (floor の名前を変える): 入れ子の cube は、その中身と ID を保つ。
    s.Find(s.Children(room)[0])->data.name = "Stone floor";
    s.Find(inRoom)->prefab.overrides = Overrides(s, inRoom, cube2File, placement);
    const json::Value room2File = MakePrefab(s, room, &roomSources);
    s.Find(cube2)->prefab.overrides = Overrides(s, cube2, cube2File, placement);
    CHECK(s.ApplyPrefab(room2, room2File, Overrides(s, room2, room1, placement)));
    CHECK(s.Find(s.Children(room2)[0])->data.name == "Stone floor");
    CHECK(s.Find(cube2) && s.Find(cube2Lid) && s.Find(cube2Lid)->data.parent == cube2);
    CHECK(s.Find(cube2)->prefab.Root() && s.Find(cube2)->prefab.outer > 0);

    // 入れ子の cube だけのコピー: 手で置いたもの。lid だけのコピー: 普通のエンティティ。
    const auto copies = s.Duplicate(std::vector<int>{cube2, cube2Lid});
    CHECK(s.Find(copies[0])->prefab.Root() && s.Find(copies[0])->prefab.outer == 0);
    const auto lidCopy = s.Duplicate(std::vector<int>{cube2Lid});
    CHECK(!s.Find(lidCopy[0])->prefab.Linked());
    s.Delete(copies);

    // room から出した cube: 置いた room から、その中身ごと消える。
    s.Delete(std::vector<int>{inRoom});
    const json::Value room3File = MakePrefab(s, room, &roomSources);
    CHECK(s.ApplyPrefab(room2, room3File, Overrides(s, room2, room2File, placement)));
    CHECK(!s.Find(cube2) && !s.Find(cube2Lid));
    CHECK(s.NestedPrefabs(room2).empty());
    CHECK(s.Find(lidCopy[0]));  // 普通のコピーは、置いた場所に残る
}

RM_TEST(Prefab_MovedPartsLeaveTheirPrefab_NothingIsLost) {
    const std::vector<std::string> placement{T().name};
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value v1 = MakePrefab(s, cube, &sources);
    const int a = s.PlacePrefab(v1, kCube);
    const int b = s.PlacePrefab(v1, kCube);
    const int aLid = s.Children(a)[0];
    const size_t count = s.Entities().size();
    // A の lid が B の下に行く: A から離れる (普通のエンティティになる)。ファイルの A の ID のまま B に加わることはない。
    CHECK(s.Move(std::vector<int>{aLid}, b, Scene::kEnd));
    CHECK(!s.Find(aLid)->prefab.Linked() && s.PrefabRootOf(aLid) == 0);
    CHECK(s.PrefabMembers(b).size() == 2);
    // Prefab からもう一度作る (プロジェクトを開くとき): 何も消えない。
    CHECK(s.ApplyPrefab(a, v1, Overrides(s, a, v1, placement)));
    CHECK(s.ApplyPrefab(b, v1, Overrides(s, b, v1, placement)));
    CHECK(s.Entities().size() == count && s.Find(aLid) && s.Find(aLid)->data.parent == b);
    // 同じ親の子の間の順番だけ: まだ部品のまま。
    int bLid = 0;
    for (const int k : s.Children(b)) {
        if (s.Find(k)->prefab.Linked()) bLid = k;  // b 自身の lid (動かしたものは普通のもの)
    }
    CHECK(bLid != 0 && bLid != aLid);
    CHECK(s.Move(std::vector<int>{bLid}, b, 0));
    CHECK(s.Find(bLid)->prefab.Linked() && s.PrefabRootOf(bLid) == b);

    // 前のデータ: ファイルに同じ ID の部品が 2 つ。2 つ目は普通のエンティティとして残す。
    s.Find(aLid)->prefab.source = s.Find(bLid)->prefab.source;
    reflect::Warnings w;
    const size_t before = s.Entities().size();
    CHECK(s.ApplyPrefab(b, v1, json::Value::MakeObject(), &w));
    CHECK(w.size() == 1 && s.Entities().size() == before);
    CHECK(s.Find(aLid) && s.Find(bLid));
    CHECK(s.PrefabMembers(b).size() == 2);
}

RM_TEST(Prefab_NestedRootMovedOutIsPlacedByHand) {
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value cubeFile = MakePrefab(s, cube, &sources);
    const int room = s.Create("Room");
    const int inRoom = s.PlacePrefab(cubeFile, kCube, room);
    std::unordered_map<int, int> roomSources;
    MakePrefab(s, room, &roomSources);
    s.LinkPrefab(room, kRoom, roomSources);
    CHECK(s.Find(inRoom)->prefab.outer > 0);
    // room から出て、別の置いた cube へ: そこでは手で置いたもの (room のファイルの ID は消える)。
    const int other = s.PlacePrefab(cubeFile, kCube);
    CHECK(s.Move(std::vector<int>{inRoom}, other, Scene::kEnd));
    CHECK(s.Find(inRoom)->prefab.Root() && s.Find(inRoom)->prefab.outer == 0);
    CHECK(s.NestedPrefabs(room).empty() && s.NestedPrefabs(other).empty());
    CHECK(s.PrefabRootOf(s.Children(inRoom)[0]) == inRoom);  // 自分の部品は自分のまま
}

RM_TEST(Prefab_UnknownKeysOfTheFileStay) {
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    json::Value old = MakePrefab(s, cube);
    old.Set("author", "someone");  // 新しいバージョンのキー
    const json::Value again = MakePrefab(s, cube, nullptr, &old);
    CHECK(again.Get("author").AsString() == "someone" && IsPrefab(again) && !NewerPrefab(again));
    json::Value newer = again;
    newer.Set("format", kPrefabFormat + 1);
    CHECK(NewerPrefab(newer));
}

RM_TEST(Scene_IdsAreNeverUsedAgain) {
    Scene s = Empty();
    s.Create("a");
    const json::Value before = s.Save();
    const int made = s.Create("b");
    // 「エンティティを作成」 の Undo がすること: 前のシーンに戻り、次の ID は保つ。
    const int next = s.NextId();
    s.Load(before);
    s.KeepNextId(next);
    CHECK(s.Create("c") > made);
    // 数え続けるには大きすぎる ID のある壊れたファイル: 置き換え、オーバーフローしない。
    json::Value broken = s.Save();
    broken.Find("entities")->At(0).Set("id", int64_t{2147483647});
    broken.Set("nextId", int64_t{2147483647});
    Scene t = Empty();
    reflect::Warnings w;
    t.Load(broken, &w);
    CHECK(!w.empty());
    for (const auto& e : t.Entities()) CHECK(e.data.id > 0 && e.data.id <= Scene::kMaxId);
    CHECK(t.Create("d") > 0);
}

RM_TEST(Prefab_OrderOfSiblingsAndComponentsIsAnOverride) {
    const std::vector<std::string> placement{T().name};
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    const int handle = s.Create("Handle", cube);
    s.AddComponent(handle, T());
    std::unordered_map<int, int> sources;
    const json::Value v1 = MakePrefab(s, cube, &sources);
    const int placed = s.PlacePrefab(v1, kCube);
    const std::vector<int> kids = s.Children(placed);  // Lid, Handle
    CHECK(kids.size() == 2 && s.Find(kids[0])->data.name == "Lid");
    CHECK(CountOverrides(Overrides(s, placed, v1, placement)) == CountOverrides(Overrides(s, placed, v1, placement)));
    const size_t before = CountOverrides(Overrides(s, placed, v1, placement));
    // 置いたもの: Handle が Lid の前、その Spin は Transform の前。
    CHECK(s.Move(std::vector<int>{kids[1]}, placed, 0));
    CHECK(s.Find(kids[1])->prefab.Linked());  // (順番だけ: まだ部品)
    CHECK(s.MoveComponent(placed, Sp().name, -1));
    const json::Value ov = Overrides(s, placed, v1, placement);
    CHECK(ov.Get("childOrder").Items().size() == 1 && ov.Get("componentOrder").Items().size() == 1);
    CHECK(CountOverrides(ov) == before + 2);  // インスペクターの数に出る
    // Prefab からもう一度作る (開き直す、または置いた別のものに反映する): 順番は残る。
    CHECK(s.ApplyPrefab(placed, v1, ov));
    const std::vector<int> after = s.Children(placed);
    CHECK(after.size() == 2 && s.Find(after[0])->data.name == "Handle" && s.Find(after[1])->data.name == "Lid");
    CHECK(s.Find(placed)->components[0].type == Sp().name && s.Find(placed)->components[1].type == T().name);
    // 何も変えていない: 順番の上書きは無い。
    const int fresh = s.PlacePrefab(v1, kCube);
    const json::Value none = Overrides(s, fresh, v1, placement);
    CHECK(none.Get("childOrder").Items().empty() && none.Get("componentOrder").Items().empty());
}

RM_TEST(Prefab_NestedRootMadeAPrefabOfItsOwn_LeavesTheOuter) {
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value cubeFile = MakePrefab(s, cube, &sources);
    const int room = s.Create("Room");
    const int inRoom = s.PlacePrefab(cubeFile, kCube, room);
    std::unordered_map<int, int> roomSources;
    MakePrefab(s, room, &roomSources);
    s.LinkPrefab(room, kRoom, roomSources);
    CHECK(s.Find(inRoom)->prefab.outer > 0);
    // 入れ子の cube の 「Prefab にする」: 自分の Prefab になり、今は room の中に手で置いた扱い。
    const AssetRef kOwn{"00000000000000000000000000000001"};
    std::unordered_map<int, int> own;
    MakePrefab(s, inRoom, &own);
    CHECK(s.LinkPrefab(inRoom, kOwn, own));
    CHECK(s.Find(inRoom)->prefab.asset == kOwn && s.Find(inRoom)->prefab.outer == 0);
    CHECK(s.NestedPrefabs(room).empty());
}

RM_TEST(Prefab_NestedRootAppliedToItsPrefab_StaysInTheOuter) {
    // 入れ子の cube (また自分の Prefab) の 「Prefab に反映」: まだ room の Prefab の中にあり、
    // そのつながりにある新しいバージョンのキーは残る。
    int cube = 0, lid = 0;
    Scene s = WithCube(&cube, &lid);
    std::unordered_map<int, int> sources;
    const json::Value cubeFile = MakePrefab(s, cube, &sources);
    const int room = s.Create("Room");
    const int inRoom = s.PlacePrefab(cubeFile, kCube, room);
    std::unordered_map<int, int> roomSources;
    MakePrefab(s, room, &roomSources);
    s.LinkPrefab(room, kRoom, roomSources);
    const int outer = s.Find(inRoom)->prefab.outer;
    CHECK(outer > 0);
    s.Find(inRoom)->prefab.extra = json::Value::MakeObject();
    s.Find(inRoom)->prefab.extra.Set("future", true);
    std::unordered_map<int, int> again;
    MakePrefab(s, inRoom, &again);
    CHECK(s.LinkPrefab(inRoom, kCube, again));
    CHECK(s.Find(inRoom)->prefab.asset == kCube && s.Find(inRoom)->prefab.outer == outer);
    CHECK((s.NestedPrefabs(room) == std::vector<int>{inRoom}));
    CHECK(s.Find(inRoom)->prefab.extra.Get("future").AsBool());
}
