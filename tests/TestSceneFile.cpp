// ゲームのシーンを専用のファイルとして持つこと (plugins/rm_engine/game_production/runtime/
// SceneFile) のテスト: ファイルの形、往復、未知のキー、以前のプロジェクト (.rmpf の中にシーン)、
// そして保存は変わったシーンだけを書くこと。

#include "Test.h"

#include "core/asset/AssetIndex.h"
#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/SceneFile.h"
#include "rm_engine/game_x_3d/runtime/Register.h"
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

// 保存のファイルを、フォルダーが持つように (パス -> テキスト)。
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
    // kind と format が先に来る。
    CHECK(read->Members().size() >= 2 && read->Members()[0].first == "kind" && read->Members()[1].first == "format");
    CHECK(read->Get("kind").AsString() == kSceneKind && read->Get("format").AsInt() == kSceneFormat);
    Scene again;
    again.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    reflect::Warnings warnings;
    again.Load(*read, &warnings);
    CHECK(warnings.empty());
    CHECK(again.Entities().size() == 2 && again.Entities()[0].data.name == "床" && again.Entities()[1].data.parent == again.Entities()[0].data.id);
    // もう一度書く: 同じテキスト (変わっていないシーンは書かない - その印は同じ)。
    CHECK(SceneFileText(again.Save()) == text);
    CHECK(TextSign(SceneFileText(again.Save())) == TextSign(text));
}

RM_TEST(SceneFile_Refused) {
    std::string err;
    CHECK(!ReadSceneFile("{ broken", &err) && !err.empty());
    CHECK(!ReadSceneFile(R"({"kind": "rm_engine.game_production.prefab", "format": 1, "entities": []})", &err));
    CHECK(!ReadSceneFile(R"([1, 2])", &err));
    // 新しいバージョンのもの: 読まない (だから上書きもしない)。
    CHECK(!ReadSceneFile(R"({"kind": "rm_engine.scene", "format": 99, "entities": []})", &err));
    CHECK(ReadSceneFile(R"({"kind": "rm_engine.scene", "format": 1})", &err).has_value());
    CHECK(TextSign("") != 0 && TextSign("a") != TextSign("b"));
}

RM_TEST(SceneFile_UnknownKeysStay) {
    // シーンのファイルに: そのキーと、エンティティのキー。
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
    // .rmpf の一覧に。
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
    list.open = AssetRef{"gone"};  // 一覧に無い: 最初のもの
    CHECK(OpenScene(list).guid == "a");
}

RM_TEST(SceneFile_NewPath) {
    std::set<std::string> taken;
    auto isTaken = [&](const std::string& p) { return taken.contains(p); };
    CHECK(NewScenePath("Main", isTaken) == "Assets/Scenes/Main.rmscene");
    taken = {"Assets/Scenes/Main.rmscene", "Assets/Scenes/Main 2.rmscene"};
    CHECK(NewScenePath("Main", isTaken) == "Assets/Scenes/Main 3.rmscene");
}

// シーンの右クリックの「複製」: 元と同じフォルダーに "<名前> 2" ...。数で終わる名前はその次から。
RM_TEST(SceneFile_CopyPath) {
    CHECK((SplitSceneNumber("Stage 2") == std::pair<std::string, int>{"Stage", 2}));
    CHECK((SplitSceneNumber("Stage") == std::pair<std::string, int>{"Stage", 0}));
    CHECK((SplitSceneNumber("Stage2") == std::pair<std::string, int>{"Stage2", 0}));
    CHECK((SplitSceneNumber("2") == std::pair<std::string, int>{"2", 0}));
    CHECK((SplitSceneNumber(" 2") == std::pair<std::string, int>{" 2", 0}));
    CHECK((SplitSceneNumber("Stage 0") == std::pair<std::string, int>{"Stage 0", 0}));
    CHECK((SplitSceneNumber("Big 12345678901") == std::pair<std::string, int>{"Big 12345678901", 0}));  // (溢れない)
    CHECK((SplitSceneNumber("ステージ 3") == std::pair<std::string, int>{"ステージ", 3}));

    std::set<std::string> taken;
    auto isTaken = [&](const std::string& p) { return taken.contains(p); };
    taken = {"Assets/Scenes/Stage.rmscene"};
    CHECK(CopyScenePath("Assets/Scenes/Stage.rmscene", isTaken) == "Assets/Scenes/Stage 2.rmscene");
    taken = {"Assets/Scenes/Stage.rmscene", "Assets/Scenes/Stage 2.rmscene", "Assets/Scenes/Stage 3.rmscene"};
    CHECK(CopyScenePath("Assets/Scenes/Stage.rmscene", isTaken) == "Assets/Scenes/Stage 4.rmscene");
    CHECK(CopyScenePath("Assets/Scenes/Stage 2.rmscene", isTaken) == "Assets/Scenes/Stage 4.rmscene");
    taken = {"Assets/Levels/Level 10.rmscene"};
    CHECK(CopyScenePath("Assets/Levels/Level 10.rmscene", isTaken) == "Assets/Levels/Level 11.rmscene");  // (元と同じフォルダー)
    taken.clear();
    CHECK(CopyScenePath("Top.rmscene", isTaken) == "Top 2.rmscene");
}

// 複製は一覧で元のすぐ後ろに。一覧に無いシーンの複製は一覧に入れない (開いているシーンは別: 最後に)。
RM_TEST(SceneFile_ListWithCopy) {
    const AssetRef a{"a"}, b{"b"}, c{"c"}, x{"x"}, copy{"copy"};
    SceneList list;
    list.scenes = {a, b, c};
    list.first = b;
    CHECK((SceneListWithCopy(list, a, copy, a).scenes == std::vector<AssetRef>{a, copy, b, c}));
    CHECK((SceneListWithCopy(list, c, copy, a).scenes == std::vector<AssetRef>{a, b, c, copy}));
    CHECK(SceneListWithCopy(list, b, copy, a).first == b);  // (初期シーンは変わらない)
    CHECK((SceneListWithCopy(list, x, copy, a).scenes == list.scenes));  // (一覧に無い Assets のシーン)
    CHECK((SceneListWithCopy(list, x, copy, x).scenes == std::vector<AssetRef>{a, b, c, copy}));  // (開いているが一覧に無い)
    CHECK((SceneListWithCopy(list, AssetRef{}, copy, AssetRef{}).scenes == std::vector<AssetRef>{a, b, c, copy}));  // (保存していない新しいシーン)
    CHECK((SceneListWithCopy(list, a, AssetRef{}, a).scenes == list.scenes));  // (複製に id が無い)
    list.scenes = {a, copy};
    CHECK((SceneListWithCopy(list, a, copy, a).scenes == list.scenes));  // (2 度は入れない)
}

// 削除: 一覧・ビルドの外し・初期シーン・見せるシーンから除く。開いていたなら、次に開くものを選ぶ。
RM_TEST(SceneFile_DeleteFromList) {
    const AssetRef a{"a"}, b{"b"}, c{"c"}, d{"d"}, x{"x"}, y{"y"};
    SceneList list;
    list.scenes = {a, b, c, d};
    list.notBuilt = {c};
    list.first = c;
    list.open = c;
    const SceneList without = SceneListWithout(list, c);
    CHECK((without.scenes == std::vector<AssetRef>{a, b, d}));
    CHECK(without.notBuilt.empty() && without.first.Empty() && without.open.Empty());
    CHECK(FirstScene(without) == a);  // (一覧の最初がゲームの始まるシーンになる)
    const SceneList other = SceneListWithout(list, a);
    CHECK(other.first == c && other.open == c && other.notBuilt.size() == 1);
    CHECK((SceneListWithout(list, AssetRef{}).scenes == list.scenes));

    auto all = [](const AssetRef&) { return true; };
    const std::vector<AssetRef> unlisted{x, y};
    // ゲームが始まるシーンが先
    list.first = a;
    CHECK(SceneAfterDelete(list, c, unlisted, all) == a);
    // 消すのがゲームの始まるシーン: 一覧で次のもの、無ければ前のもの (近い順)
    list.first = c;
    CHECK(SceneAfterDelete(list, c, unlisted, all) == d);
    CHECK(SceneAfterDelete(list, c, unlisted, [&](const AssetRef& r) { return r != d; }) == b);
    list.first = d;
    CHECK(SceneAfterDelete(list, d, unlisted, all) == c);
    // ファイルの無いもの (一覧に残った消えたシーン) は飛ばし、一覧に無い Assets のシーンへ
    list.scenes = {a, b};
    list.first = {};
    CHECK(SceneAfterDelete(list, a, unlisted, [&](const AssetRef& r) { return r != b; }) == x);
    // 一覧に無いシーンを消す: 一覧のもの
    CHECK(SceneAfterDelete(list, x, unlisted, all) == a);
    // ほかにシーンが無い: 空 (消させない)
    list.scenes = {a};
    CHECK(SceneAfterDelete(list, a, {}, all).Empty());
    CHECK(SceneAfterDelete(list, a, {a}, all).Empty());
}

RM_TEST(SceneFile_OnlyChangedScenesAreWritten) {
    Scene scene = MakeScene();
    Disk disk;
    // 新しいシーン: 先にその .meta (新しい ID 入り)、その後にシーン。
    const SceneSlot fresh{"", "Assets/Scenes/Main.rmscene", 0};
    const ScenePlan first = PlanSceneSave(scene.Save(), fresh, kGuid);
    CHECK(first.files.size() == 2);
    CHECK(first.files[0].path == "Assets/Scenes/Main.rmscene.meta" && first.files[1].path == "Assets/Scenes/Main.rmscene");
    CHECK(asset::GuidInMeta(first.files[0].text) == kGuid);
    CHECK(first.after.guid == kGuid && first.after.onDisk == TextSign(first.files[1].text));
    Write(disk, first);
    // 何も変わっていない: 何も書かない。
    const ScenePlan same = PlanSceneSave(scene.Save(), first.after, "ffffffffffffffffffffffffffffffff");
    CHECK(same.files.empty() && same.after.guid == kGuid && same.after.onDisk == first.after.onDisk);
    // 変わった: シーンだけ (ID は同じ、.meta は無し)。
    scene.Create("木");
    const ScenePlan changed = PlanSceneSave(scene.Save(), first.after, "ffffffffffffffffffffffffffffffff");
    CHECK(changed.files.size() == 1 && changed.files[0].path == "Assets/Scenes/Main.rmscene");
    CHECK(changed.after.guid == kGuid && changed.after.onDisk != first.after.onDisk);
    Write(disk, changed);
    // 元に戻した (Undo): 今のファイルと違うので、もう一度書く。
    const ScenePlan back = PlanSceneSave(MakeScene().Save(), changed.after, "");
    CHECK(back.files.size() == 1);
    // ディスクから読んで、そのまま保存した: 書かない。
    std::string err;
    const auto read = ReadSceneFile(disk["Assets/Scenes/Main.rmscene"], &err);
    CHECK(read.has_value());
    Scene loaded;
    loaded.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    if (read) loaded.Load(*read);
    const SceneSlot asRead{kGuid, "Assets/Scenes/Main.rmscene", TextSign(disk["Assets/Scenes/Main.rmscene"])};
    CHECK(PlanSceneSave(loaded.Save(), asRead, "").files.empty());
}

// 途中で失敗した保存 (シーンは書いたが .rmpf は書かなかった - またはその逆): 次の保存は
// シーンをもう一度書く。スロットが最後に知っていた内容に戻っていても (Undo)。.meta が書かれた
// 新しいシーンは、その ID とファイルを保つ。
RM_TEST(SceneFile_AfterAFailedSave) {
    const Scene scene = MakeScene();
    const SceneSlot read{kGuid, "Assets/Scenes/Main.rmscene", TextSign(SceneFileText(scene.Save()))};
    SceneSlot tried = read;
    tried.onDisk = 12345;  // (別のテキストを書いていた)
    const SceneSlot after = SlotAfterFailedSave(read, tried, false);
    CHECK(after.guid == kGuid && after.path == read.path && after.onDisk == 0);
    CHECK(PlanSceneSave(scene.Save(), after, "").files.size() == 1);  // 変わっていなくても、もう一度書く
    // 新しいシーン: その .meta がある - ID とファイルは保つ。無い - やはり無し (.meta は次の保存で)。
    const SceneSlot fresh{"", "Assets/Scenes/Main.rmscene", 0};
    const SceneSlot triedNew{kGuid, "Assets/Scenes/Main.rmscene", 99};
    const SceneSlot kept = SlotAfterFailedSave(fresh, triedNew, true);
    CHECK(kept.guid == kGuid && kept.path == triedNew.path && kept.onDisk == 0);
    const SceneSlot none = SlotAfterFailedSave(fresh, triedNew, false);
    CHECK(none.guid.empty() && none.onDisk == 0 && PlanSceneSave(scene.Save(), none, kGuid).files.size() == 2);
}

RM_TEST(SceneFile_ProjectOfBefore) {
    // 0.0.1: .rmpf のゲームデータの中にシーンがあり、設定の隣にある (と、新しいバージョンのキー)。
    json::Value data = Parse(R"({"settings": {"space": "3d"}, "future": 1,
        "scene": {"nextId": 3, "entities": [ {"id": 1, "parent": 0, "name": "カメラ", "active": true, "components": []},
                                             {"id": 2, "parent": 1, "name": "子", "active": false, "components": []} ]}})");
    const auto old = TakeOldScene(data);
    CHECK(old.has_value());
    CHECK(!data.Find("scene") && data.Get("settings").Get("space").AsString() == "3d" && data.Get("future").AsInt() == 1);
    if (!old) return;
    Scene scene;
    scene.Load(*old);
    // 切り離す: 新しい ID でそのファイルに書き、同じに読み戻す。
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

    // もう切り離してあるデータ: 取るものは無い ("scene" はそこに放っておく)。
    json::Value now = Parse(R"({"settings": {}, "sceneList": {"scenes": []}, "scene": {"entities": []}})");
    CHECK(!TakeOldScene(now) && now.Find("scene"));
    json::Value none = Parse(R"({"settings": {}})");
    CHECK(!TakeOldScene(none));
}

// ゲームのシーン: 一覧の順、一覧に無いのに開いているもの (最後)、最初のもの、
// Game::LoadScene が受け取る名前。
RM_TEST(SceneFile_GameScenesAndNames) {
    const AssetRef a{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}, b{"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}, c{"cccccccccccccccccccccccccccccccc"};
    SceneList list;
    list.scenes = {a, b};
    CHECK(FirstScene(list) == a);  // 選んだものが無い: 一覧の最初
    list.first = b;
    CHECK(FirstScene(list) == b);
    list.first = c;  // 一覧に無い (外した): 一覧の最初
    CHECK(FirstScene(list) == a);
    CHECK(FirstScene(SceneList{}).Empty());
    CHECK((GameScenes(list, b) == std::vector<AssetRef>{a, b}));
    CHECK((GameScenes(list, c) == std::vector<AssetRef>{a, b, c}));            // 開いているが一覧に無い: 最後
    CHECK((GameScenes(list, AssetRef{}) == std::vector<AssetRef>{a, b, AssetRef{}}));  // まだ保存していない新しいシーン
    // Play は、書き出したゲームと同じように番号を付ける: ビルドから外したものは除く。開いているものは、
    // 外していても最後 (2026-11-19: Game::LoadScene(2) がゲームの別のシーンに行っていた)。
    SceneList three;
    three.scenes = {a, b, c};
    three.notBuilt = {b};
    CHECK((PlayScenes(three, a) == std::vector<AssetRef>{a, c}));
    CHECK((PlayScenes(three, b) == std::vector<AssetRef>{a, c, b}));
    CHECK((PlayScenes(three, AssetRef{}) == std::vector<AssetRef>{a, c, AssetRef{}}));
    three.notBuilt = {a};  // (最初のものは常にビルドされる)
    CHECK((PlayScenes(three, c) == std::vector<AssetRef>{a, b, c}));
    list.scenes = {a, a, AssetRef{}, b};
    CHECK((GameScenes(list, a) == std::vector<AssetRef>{a, b}));  // それぞれ 1 回、空の ID は無し
    CHECK(SceneNameOf("Assets/Scenes/Title.rmscene") == "Title" && SceneNameOf("Stage 2.rmscene") == "Stage 2" && SceneNameOf("Assets\\x.rmscene") == "x");
    const std::vector<std::string> names{"Title", "Stage", "Stage"};
    CHECK(FindSceneByName(names, "Stage") == std::optional<size_t>(1) && !FindSceneByName(names, "stage") && !FindSceneByName(names, ""));
    // 一覧は、保存して読み込んでも "first" (と新しいバージョンのキー) を保つ。
    SceneList saved;
    saved.scenes = {a, b};
    saved.first = b;
    SceneList back;
    reflect::Load(back, reflect::Save(saved));
    CHECK(back.first == b && back.scenes == saved.scenes);
}

// シーンが始まるときのカメラ: ゲーム設定の 最初のカメラ がこのシーンのカメラならそれ、
// そうでなければカメラ部品を持つ最初の動いているエンティティ。
RM_TEST(SceneFile_SceneCamera) {
    Scene s;
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>()});
    const std::vector<std::string> cameras{reflect::TypeOf<space3d::Transform>().name};  // (カメラを表す型)
    const int box = s.Create("箱");
    const int off = s.Create("止めたカメラ");
    s.AddComponent(off, reflect::TypeOf<space3d::Transform>());
    s.Find(off)->data.active = false;
    const int cam = s.Create("カメラ");
    s.AddComponent(cam, reflect::TypeOf<space3d::Transform>());
    CHECK(SceneCamera(s, cameras, static_cast<uint64_t>(cam)) == static_cast<uint64_t>(cam));
    CHECK(SceneCamera(s, cameras, static_cast<uint64_t>(box)) == static_cast<uint64_t>(cam));  // カメラではない: 最初の動いているもの
    CHECK(SceneCamera(s, cameras, 999) == static_cast<uint64_t>(cam));                         // このシーンに無い
    CHECK(SceneCamera(s, {}, static_cast<uint64_t>(cam)) == 0);                                // カメラの型が無い: 無し
}

// 左手系 (2026-11-20) より前のシーン / Prefab (形式 1: 右手系) は、読むときに Z で写す: 位置の Z、
// オイラー角の X と Y が逆に (見た目は前と同じ)。その型のプラグインが写し方を言う。形式 2 はそのまま。
RM_TEST(SceneFile_OldRightHandedIsMirrored) {
    RuntimeRegistry registry;
    rm::game_x_3d::RegisterRuntime(registry);
    static const rm::game_production::MirrorZInfo kFake{"Fake", [](json::Value& data) { rm::game_production::NegateJsonNumber(data, "offset", 2); }};
    registry.Contribute(rm::game_production::kMirrorZPoint, kFake);
    const std::string old = R"({"kind": "rm_engine.scene", "format": 1, "entities": [{"id": 1, "components": [
        {"type": "rm::space3d::Transform", "data": {"position": [1, 2, 3], "rotation": [10, 20, 30], "scale": [1, 2, 3]}},
        {"type": "Fake", "data": {"offset": [4, 5, 6]}}]}], "settings": [{"type": "Fake", "data": {"offset": [0, 0, 7]}}]})";
    const auto doc = rm::game_production::ReadSceneFile(old, nullptr, &registry);
    CHECK(doc.has_value());
    if (!doc) return;
    const json::Value& comps = doc->Get("entities").Items()[0].Get("components");
    const json::Value& t = comps.Items()[0].Get("data");
    CHECK(t.Get("position").Items()[2].AsDouble() == -3 && t.Get("position").Items()[0].AsDouble() == 1);
    CHECK(t.Get("rotation").Items()[0].AsDouble() == -10 && t.Get("rotation").Items()[1].AsDouble() == -20 && t.Get("rotation").Items()[2].AsDouble() == 30);
    CHECK(t.Get("scale").Items()[2].AsDouble() == 3);  // (大きさはそのまま)
    CHECK(comps.Items()[1].Get("data").Get("offset").Items()[2].AsDouble() == -6);
    CHECK(doc->Get("settings").Items()[0].Get("data").Get("offset").Items()[2].AsDouble() == -7);
    CHECK(doc->Get("format").AsInt() == rm::game_production::kSceneFormat);
    // 形式 2 (このバージョンが書いたもの) と、registry なしの読み方はそのまま。
    std::string now = old;
    now.replace(now.find("\"format\": 1"), 11, "\"format\": 2");
    const auto kept = rm::game_production::ReadSceneFile(now, nullptr, &registry);
    CHECK(kept && kept->Get("entities").Items()[0].Get("components").Items()[0].Get("data").Get("position").Items()[2].AsDouble() == 3);
    const auto plain = rm::game_production::ReadSceneFile(old, nullptr);
    CHECK(plain && plain->Get("entities").Items()[0].Get("components").Items()[0].Get("data").Get("position").Items()[2].AsDouble() == 3);
    // Prefab も (形式 1 だけ。1 回だけ写す)。
    json::Value prefab = *json::Parse(R"({"kind": "rm_engine.game_production.prefab", "format": 1, "entities": [{"id": 1, "components": [
        {"type": "rm::space3d::Transform", "data": {"position": [0, 0, 5]}}]}]})");
    rm::game_production::UpgradePrefab(prefab, registry);
    rm::game_production::UpgradePrefab(prefab, registry);
    CHECK(prefab.Get("format").AsInt() == rm::game_production::kPrefabFormat);
    CHECK(prefab.Get("entities").Items()[0].Get("components").Items()[0].Get("data").Get("position").Items()[2].AsDouble() == -5);
}
