// 世界のセーブ (ゲーム制作の runtime/WorldSave。設計 docs/SAVE.md) のテスト: 「セーブする」を付けた物の
// 位置・有効・部品の値を取って (CaptureWorld)、読み直したシーンに当てる (ApplyWorld)。ゲームの中で
// 消した物は消え、ゲームの中で Prefab から作った物・名前で作った物は作り直され、部品の中の参照は
// 作り直した物を指す。壊れたセーブ (無い物・使えない部品・形の違う値・新しい版) でも落ちない。
// スクリプトの部分 (rm_engine.game_production.save 版 2) を通して、ファイルに書いて、シーンの切り替え
// (SwitchAskedScene) で戻るところまで。

#include "Test.h"

#include "TestReflectTypes.h"
#include "base/Json.h"
#include "core/reflect/Data.h"
#include "platform/Shell.h"
#include "rm_engine/game_production/runtime/Globals.h"
#include "rm_engine/game_production/runtime/Id.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/WorldSave.h"
#include "rm_engine/game_x_3d/runtime/Id.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::game_production;
namespace fs = std::filesystem;

namespace {

const AssetRef kBullet{"0123456789abcdef0123456789abcdef"};

const reflect::TypeDesc& T() { return reflect::TypeOf<space3d::Transform>(); }
const reflect::TypeDesc& Sp() { return reflect::TypeOf<game_x_3d::Spin>(); }
const reflect::TypeDesc& Sv() { return reflect::TypeOf<Saveable>(); }
const reflect::TypeDesc& L() { return reflect::TypeOf<rmtest::Links>(); }

Scene Base() {
    Scene s;
    s.SetComponentTypes({&T(), &Sp(), &Sv(), &L()}, {&Sp()});
    return s;
}

Scene From(const json::Value& file) {
    Scene s = Base();
    s.Load(file);
    return s;
}

space3d::Transform& Tf(Scene& s, int id) { return *static_cast<space3d::Transform*>(s.Find(id)->FindComponent(T().name)->value.Get()); }

Saveable& Mark(Scene& s, int id) {
    if (!s.Find(id)->FindComponent(Sv().name)) s.AddComponent(id, Sv());
    return *static_cast<Saveable*>(s.Find(id)->FindComponent(Sv().name)->value.Get());
}

// 名前の物 (木の順)。
std::vector<int> Named(const Scene& s, const std::string& name) {
    std::vector<int> out;
    for (const TreeEntry& t : s.TreeOrder()) {
        if (t.entity->data.name == name) out.push_back(t.entity->data.id);
    }
    return out;
}

bool At(Scene& s, int id, float x, float y, float z) {
    const Vec3 p = Tf(s, id).position;
    return p.x == x && p.y == y && p.z == z;
}

const std::vector<std::string> kPlace{reflect::TypeOf<space3d::Transform>().name};

fs::path TempFolder(const wchar_t* name) {
    const fs::path p = fs::temp_directory_path() / (std::wstring(name) + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(p, ec);
    return p;
}

}  // namespace

RM_TEST(WorldSave_PositionsActiveAndDestroyedSceneEntities) {
    // シーンのファイル: A (セーブする)、B (セーブする)、C (印なし)、D (セーブする。A の子)。
    Scene file = Base();
    const int a = file.Create("A");
    const int b = file.Create("B");
    const int c = file.Create("C");
    const int d = file.Create("D", a);
    for (const int id : {a, b, c, d}) file.AddComponent(id, T());
    for (const int id : {a, b, d}) Mark(file, id);
    const json::Value saved = file.Save();

    // Play: A を動かして無効に、C も動かす (印が無いので残らない)、B を消す、D を一番上へ。
    Scene play = From(saved);
    const int base = play.NextId();
    Tf(play, a).position = {1, 2, 3};
    play.Find(a)->data.active = false;
    Tf(play, c).position = {9, 9, 9};
    const int gone[] = {b};
    play.Delete(gone);
    const int moved[] = {d};
    play.Move(moved, 0, Scene::kEnd);
    const json::Value world = CaptureWorld(play, base, kPlace, {"Main", "g1"});
    CHECK(world.Get("format").AsInt() == kWorldFormat && world.Get("base").AsInt() == base && world.Get("entities").Size() == 2);
    CHECK(SavedScene(world).name == "Main" && SavedScene(world).id == "g1");
    // 「位置」の部品だけ (と、セーブする自身)。
    CHECK(world.Get("entities").At(0).Get("state").Get("components").Size() == 2);

    // 読み直したシーンに当てる。
    Scene fresh = From(saved);
    reflect::Warnings warnings;
    const WorldApplied done = ApplyWorld(fresh, world, nullptr, warnings);
    CHECK(warnings.empty() && done.restored == 2 && done.made == 0 && done.removed == 1);
    CHECK(At(fresh, a, 1, 2, 3) && !fresh.Find(a)->data.active);
    CHECK(fresh.Find(b) == nullptr);                     // ゲームの中で消した
    CHECK(At(fresh, c, 0, 0, 0));                         // 印が無い: ファイルのまま
    CHECK(fresh.Find(d) && fresh.Find(d)->data.parent == 0);  // 親も戻る

    // このフレームの終わりに消える物は入れない。
    const int leaving[] = {a};
    const json::Value without = CaptureWorld(play, base, kPlace, {}, leaving);
    CHECK(without.Get("entities").Size() == 1 && without.Get("entities").At(0).Get("id").AsInt() == d);
}

RM_TEST(WorldSave_SpawnedPrefabsAndEntitiesAreMadeAgain) {
    Scene file = Base();
    const int floor = file.Create("Floor");
    file.AddComponent(floor, T());
    // Prefab「Bullet」: 一番上と子の Spark。どちらもセーブする。
    Scene ps = Base();
    const int pr = ps.Create("Bullet");
    const int pk = ps.Create("Spark", pr);
    for (const int id : {pr, pk}) {
        ps.AddComponent(id, T());
        Mark(ps, id);
    }
    const json::Value prefab = MakePrefab(ps, pr);
    const PrefabReader read = [&](const AssetRef& r) -> const json::Value* { return r == kBullet ? &prefab : nullptr; };

    Scene play = From(file.Save());
    const int base = play.NextId();
    reflect::Warnings w;
    const int b1 = SpawnPrefab(play, kBullet, 0, read, w);
    const int b2 = SpawnPrefab(play, kBullet, floor, read, w);
    CHECK(b1 >= base && b2 > b1 && w.empty());
    Tf(play, b1).position = {1, 0, 0};
    Tf(play, b2).position = {2, 0, 0};
    Tf(play, play.Children(b1)[0]).position = {0, 5, 0};
    const int spark2[] = {play.Children(b2)[0]};
    play.Delete(spark2);  // 2 つ目の弾の Spark はゲームの中で消した
    // 名前で作った物 (rm::Spawn("Marker")) に、全部の部品の値: 参照 (Links) は 1 つ目の弾を指す。
    const int m = play.Create("Marker");
    play.AddComponent(m, T());
    play.AddComponent(m, L());
    Mark(play, m).everything = true;
    Tf(play, m).position = {7, 7, 7};
    static_cast<rmtest::Links*>(play.Find(m)->FindComponent(L().name)->value.Get())->target.id = static_cast<uint64_t>(b1);
    static_cast<rmtest::Links*>(play.Find(m)->FindComponent(L().name)->value.Get())->followers = {{static_cast<uint64_t>(b2)}, {999}};
    const json::Value world = CaptureWorld(play, base, kPlace, {"Main", ""});

    auto check = [&](Scene& s, const WorldApplied& done, const reflect::Warnings& warnings) {
        CHECK(warnings.empty() && done.made == 5 && done.removed == 1);
        const std::vector<int> bullets = Named(s, "Bullet");
        CHECK(bullets.size() == 2 && Named(s, "Spark").size() == 1);
        if (bullets.size() != 2) return;
        const int n1 = s.Find(bullets[0])->data.parent == 0 ? bullets[0] : bullets[1];
        const int n2 = n1 == bullets[0] ? bullets[1] : bullets[0];
        CHECK(At(s, n1, 1, 0, 0) && s.Children(n1).size() == 1 && At(s, s.Children(n1)[0], 0, 5, 0));
        CHECK(At(s, n2, 2, 0, 0) && s.Find(n2)->data.parent == floor && s.Children(n2).empty());
        CHECK(s.Find(n1)->prefab.Root() && s.Find(n1)->prefab.asset == kBullet);  // Prefab のまま
        const std::vector<int> markers = Named(s, "Marker");
        CHECK(markers.size() == 1);
        if (markers.size() != 1) return;
        CHECK(At(s, markers[0], 7, 7, 7) && s.Find(markers[0])->FindComponent(Sv().name));
        const auto* links = static_cast<const rmtest::Links*>(s.Find(markers[0])->FindComponent(L().name)->value.Get());
        // 作り直した物を指す。作り直さなかったゲームの中の物 (999) は 0。
        CHECK(links->target.id == static_cast<uint64_t>(n1) && links->followers.size() == 2 && links->followers[0].id == static_cast<uint64_t>(n2) &&
              links->followers[1].id == 0);
    };
    Scene fresh = From(file.Save());
    const int freshBase = fresh.NextId();
    reflect::Warnings warnings;
    check(fresh, ApplyWorld(fresh, world, read, warnings), warnings);

    // 読んだ後にもう一度セーブして読む: 作り直した物はまたゲームの中で作った物。同じになる。
    const json::Value again = CaptureWorld(fresh, freshBase, kPlace, {"Main", ""});
    Scene third = From(file.Save());
    reflect::Warnings w3;
    check(third, ApplyWorld(third, again, read, w3), w3);

    // Prefab がもう無い: 飛ばして知らせる (落ちない)。
    Scene fourth = From(file.Save());
    reflect::Warnings w4;
    const WorldApplied d4 = ApplyWorld(fourth, world, nullptr, w4);
    CHECK(!w4.empty() && Named(fourth, "Bullet").empty() && Named(fourth, "Marker").size() == 1 && d4.made == 1);
}

RM_TEST(WorldSave_EverythingRestoresFieldsAndAddedComponents) {
    Scene file = Base();
    const int e = file.Create("Hero");
    file.AddComponent(e, T());
    file.AddComponent(e, Sp());
    Mark(file, e).everything = true;
    Scene play = From(file.Save());
    const int base = play.NextId();
    play.Find(e)->data.name = "Hero (Lv 2)";
    play.Find(e)->data.tag = "Player";
    static_cast<game_x_3d::Spin*>(play.Find(e)->FindComponent(Sp().name)->value.Get())->degreesPerSecond = {1, 2, 3};
    play.AddComponent(e, Sp());  // ゲームの中で足した 2 つ目
    static_cast<game_x_3d::Spin*>(play.Find(e)->FindComponent(Sp().name, 1)->value.Get())->degreesPerSecond = {4, 5, 6};
    const json::Value world = CaptureWorld(play, base, kPlace, {});

    Scene fresh = From(file.Save());
    reflect::Warnings warnings;
    const WorldApplied done = ApplyWorld(fresh, world, nullptr, warnings);
    CHECK(warnings.empty() && done.restored == 1);
    const SceneEntity& h = *fresh.Find(e);
    CHECK(h.data.name == "Hero (Lv 2)" && h.data.tag == "Player" && h.data.id == e && h.CountComponents(Sp().name) == 2);
    CHECK(static_cast<const game_x_3d::Spin*>(h.FindComponent(Sp().name, 0)->value.Get())->degreesPerSecond.y == 2);
    CHECK(static_cast<const game_x_3d::Spin*>(h.FindComponent(Sp().name, 1)->value.Get())->degreesPerSecond.z == 6);
}

RM_TEST(WorldSave_BrokenOrOldSavesDoNotCrash) {
    Scene file = Base();
    const int a = file.Create("A");
    file.AddComponent(a, T());
    Mark(file, a);
    Scene play = From(file.Save());
    const int base = play.NextId();
    Tf(play, a).position = {3, 0, 0};
    json::Value world = CaptureWorld(play, base, kPlace, {"Main", "g1"});

    // 壊す: 新しい版・シーンに無い物・使えない部品・形の違う値・知らないキー。
    world.Set("format", 99);
    world.Set("note", "kept?");
    json::Value list = world.Get("entities");
    json::Value missing = json::Value::MakeObject();
    missing.Set("id", 12345);
    missing.Set("state", json::Value::MakeObject());
    list.Push(missing);
    json::Value& comps = list.At(0).Find("state")->Find("components")->Items().front();  // Transform
    json::Value data = comps.Get("data");
    data.Set("scale", "big");  // 形が違う
    comps.Set("data", data);
    json::Value unknown = json::Value::MakeObject();
    unknown.Set("type", "rm::nobody::Thing");
    unknown.Set("data", json::Value::MakeObject());
    list.At(0).Find("state")->Find("components")->Push(unknown);
    world.Set("entities", list);

    // ゲームを直した: セーブの後でシーンに足した「セーブする」の物は消さない。
    Scene file2 = From(file.Save());
    const int added = file2.Create("New");
    file2.AddComponent(added, T());
    Mark(file2, added);
    CHECK(added >= base);
    Scene fresh = From(file2.Save());
    reflect::Warnings warnings;
    const WorldApplied done = ApplyWorld(fresh, world, nullptr, warnings);
    CHECK(At(fresh, a, 3, 0, 0) && fresh.Find(added) && done.removed == 0 && done.restored == 1);
    const auto has = [&](const char* text) {
        return std::any_of(warnings.begin(), warnings.end(), [&](const std::string& m) { return m.find(text) != std::string::npos; });
    };
    CHECK(has("新しい版") && has("12345") && has("rm::nobody::Thing") && warnings.size() >= 3);

    // セーブでない物・空: 何もしない。
    reflect::Warnings w2;
    CHECK(ApplyWorld(fresh, json::Value("x"), nullptr, w2).restored == 0 && w2.size() == 1);
    reflect::Warnings w3;
    CHECK(ApplyWorld(fresh, json::Value::MakeObject(), nullptr, w3).restored == 0 && w3.empty() && fresh.Find(added));
}

RM_TEST(WorldSave_ThroughTheScriptsPartAndASceneSwitch) {
    // 部品の型はプラグインのランタイムの部分から (ゲーム制作 = セーブする、ゲーム制作 × 3D = Transform)。
    RuntimeRegistry reg;
    const PluginId ids[] = {game_production::kPluginId.Id(), game_x_3d::kPluginId.Id()};
    RegisterRuntimeParts(reg, ids);
    CHECK(PlacementTypes(reg) == kPlace);
    const auto comps = reg.Contributions<ComponentInfo>(kComponentPoint);
    CHECK(std::any_of(comps.begin(), comps.end(), [](const auto& c) { return &c.item->type() == &Sv(); }));

    Scene file = Base();
    const int a = file.Create("A");
    file.AddComponent(a, T());
    Mark(file, a);
    const json::Value fileJson = file.Save();

    ScriptWorld w;
    std::string told;
    w.log = [&](int, std::string_view t) { told += std::string(t) + "\n"; };
    w.registry = &reg;
    w.saveFolder = TempFolder(L"rm_world_saves_");
    w.globals.Reset(std::vector<GlobalVariable>{{"score", GlobalType::Int, "0"}});
    w.sceneNames = {"Main", "Other"};
    w.sceneIds = {"g1", "g2"};
    w.sceneIndex = 0;
    Scene play = From(fileJson);
    w.scene = &play;
    w.sceneBase = play.NextId();
    const RmApi api = MakeScriptApi(w);
    const auto* sv = static_cast<const RmSaveApi2*>(api.part(api.ctx, RM_PART_SAVE, RM_PART_SAVE_VERSION2));
    CHECK(sv && api.part(api.ctx, RM_PART_SAVE, RM_PART_SAVE_VERSION));  // 版 1 もそのまま
    if (!sv) return;

    Tf(play, a).position = {4, 5, 6};
    w.globals.Find("score")->v[0] = 7;
    CHECK(sv->hasWorld(api.ctx, "1") == 0 && sv->readWorld(api.ctx, "1") == 0 && told.empty());  // まだ無い: 黙って false
    CHECK(sv->writeWorld(api.ctx, "1") == 1 && sv->hasWorld(api.ctx, "1") == 1);
    // グローバル変数だけの Write は、スロットの世界を残す。
    CHECK(sv->write(api.ctx, "1") == 1 && sv->hasWorld(api.ctx, "1") == 1);
    // "world" の中の知らないキーは、書き直しても残る。
    std::string why;
    json::Value doc = *ReadSaveDoc(w.saveFolder, "1", &why);
    json::Value wj = doc.Get(kWorldSaveKey);
    wj.Set("note", "from a newer engine");
    doc.Set(kWorldSaveKey, wj);
    CHECK(platform::WriteFileAtomic(SaveFile(w.saveFolder, "1"), json::Write(doc), &why));
    CHECK(sv->writeWorld(api.ctx, "1") == 1);
    CHECK(ReadSaveDoc(w.saveFolder, "1", &why)->Get(kWorldSaveKey).Get("note").AsString() == "from a newer engine");
    // グローバル変数だけのセーブ: 世界は無い。
    CHECK(sv->write(api.ctx, "2") == 1 && sv->hasWorld(api.ctx, "2") == 0 && sv->readWorld(api.ctx, "2") == 0);
    CHECK(told.find("世界がありません") != std::string::npos);

    // 読む: グローバル変数はすぐ、世界はフレームの後の切り替えで。
    Tf(play, a).position = {0, 0, 0};
    w.globals.Find("score")->v[0] = 0;
    CHECK(sv->readWorld(api.ctx, "1") == 1 && w.sceneAsked == 0 && w.worldAskedScene == 0 && w.globals.Find("score")->v[0] == 7);
    CHECK(At(play, a, 0, 0, 0));  // (まだ)
    ScriptRunner runner;
    ScriptModule module;
    std::string err;
    const auto load = [&](size_t index, Scene& into, std::string*) {
        into.Load(fileJson);
        return index == 0;
    };
    CHECK(SwitchAskedScene(play, w, runner, module, {}, load, &err) == SceneSwitch::Switched);
    CHECK(At(play, a, 4, 5, 6) && w.scene == &play && w.worldAsked.IsNull() && w.sceneBase == From(fileJson).NextId());
    CHECK(told.find("セーブした世界を読みました") != std::string::npos);
    // 新しいスロットにグローバル変数だけを書く: 最後に読んだセーブの世界は付いて来ない (世界はスロットのもの)。
    CHECK(sv->write(api.ctx, "3") == 1 && sv->hasWorld(api.ctx, "3") == 0);

    // セーブしたシーンが一覧に無い: 読まない (グローバル変数も)。
    w.sceneNames = {"Title"};
    w.sceneIds = {"g9"};
    w.globals.Find("score")->v[0] = 1;
    CHECK(sv->readWorld(api.ctx, "1") == 0 && w.sceneAsked == -1 && w.globals.Find("score")->v[0] == 1);
    CHECK(told.find("シーンの一覧にありません") != std::string::npos);
    // スロットはパスではない。
    CHECK(sv->writeWorld(api.ctx, "..\\x") == 0 && sv->hasWorld(api.ctx, "..\\x") == 0);
    std::error_code ec;
    fs::remove_all(w.saveFolder, ec);
}
