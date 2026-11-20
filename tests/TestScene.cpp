// ゲーム制作プラグインのシーン (runtime/Scene) のテスト: エンティティの木の形、
// 移動 / コピー / 削除、そして保存形式 (未知のキーは残す、壊れたファイルは直す)。

#include "Test.h"

#include <chrono>

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/space3d/runtime/Transform.h"

using namespace rm;
using namespace rm::game_production;

namespace {

std::vector<int> Order(const Scene& s) {
    std::vector<int> out;
    for (const TreeEntry& t : s.TreeOrder()) out.push_back(t.entity->data.id);
    return out;
}

// 1 A
//   2 A1
//     3 A1a
//   4 A2
// 5 B
Scene Sample() {
    Scene s;
    s.Create("A");
    s.Create("A1", 1);
    s.Create("A1a", 2);
    s.Create("A2", 1);
    s.Create("B");
    return s;
}

}  // namespace

RM_TEST(Scene_CreateAndTreeOrder) {
    Scene s = Sample();
    CHECK((Order(s) == std::vector<int>{1, 2, 3, 4, 5}));
    const auto tree = s.TreeOrder();
    CHECK(tree[2].depth == 2 && tree[3].depth == 1 && tree[4].depth == 0);
    CHECK((s.Children(1) == std::vector<int>{2, 4}));
    // 兄弟の間の位置に。
    const int first = s.Create("first", 0, 0);
    CHECK((s.Children(0) == std::vector<int>{first, 1, 5}));
    const int mid = s.Create("mid", 1, 1);
    CHECK((s.Children(1) == std::vector<int>{2, mid, 4}));
    // 親が無い: 最上位。
    const int lost = s.Create("lost", 999);
    CHECK(s.Find(lost)->data.parent == 0);
}

RM_TEST(Scene_ActiveInHierarchy) {
    Scene s = Sample();
    s.Find(2)->data.active = false;
    for (const TreeEntry& t : s.TreeOrder()) {
        const int id = t.entity->data.id;
        CHECK(t.activeInHierarchy == (id != 2 && id != 3));  // A1a は A1 が無効なので無効
    }
}

RM_TEST(Scene_Move_ReorderReparentAndRefuse) {
    Scene s = Sample();
    // A の前に B (番号は B を除いて数える)。
    CHECK(s.Move(std::vector<int>{5}, 0, 0));
    CHECK((s.Children(0) == std::vector<int>{5, 1}));
    // もう一度同じことをしても、何も変わらない。
    CHECK(!s.Move(std::vector<int>{5}, 0, 0));
    // A2 を B の中へ。
    CHECK(s.Move(std::vector<int>{4}, 5, 0));
    CHECK(s.Find(4)->data.parent == 5);
    CHECK((s.Children(1) == std::vector<int>{2}));
    // 自分の中にも、自分より下にも入れない。無い親の下にも入れない。
    CHECK(!s.Move(std::vector<int>{1}, 3, 0));
    CHECK(!s.Move(std::vector<int>{1}, 1, 0));
    CHECK(!s.Move(std::vector<int>{2}, 777, 0));
    CHECK(s.Find(1)->data.parent == 0);
    // いくつかを同時に、木の順で (A2 は B の下にあり、B が先に来る)。動かしたものの子は
    // ついていく (A1a は A1 の下のまま)。
    CHECK(s.Move(std::vector<int>{3, 2, 4}, 0, Scene::kEnd));
    CHECK((s.Children(0) == std::vector<int>{5, 1, 4, 2}));
    CHECK(s.Find(3)->data.parent == 2);
}

RM_TEST(Scene_Duplicate_CopiesSubtreeNextToOriginal) {
    Scene s = Sample();
    const auto made = s.Duplicate(std::vector<int>{1, 3});  // 3 は 1 の下: いっしょにコピーされ、1 回だけ
    CHECK(made.size() == 1);
    const int copy = made[0];
    CHECK(copy == 6);
    CHECK((s.Children(0) == std::vector<int>{1, copy, 5}));
    CHECK(s.Entities().size() == 9);
    const auto kids = s.Children(copy);
    CHECK(kids.size() == 2);
    CHECK(s.Find(kids[0])->data.name == "A1" && s.Find(kids[1])->data.name == "A2");
    CHECK(s.Children(kids[0]).size() == 1);
    // 元のものには手を付けない。
    CHECK((s.Children(1) == std::vector<int>{2, 4}));
}

RM_TEST(Scene_Delete_TakesEverythingBelow) {
    Scene s = Sample();
    CHECK(s.Delete(std::vector<int>{2, 5}) == 3);
    CHECK((Order(s) == std::vector<int>{1, 4}));
    CHECK(s.Delete(std::vector<int>{42}) == 0);
    // ID は再利用しない。
    CHECK(s.Create("new") == 6);
}

RM_TEST(Scene_SaveLoad_RoundTripAndUnknownKeys) {
    Scene s = Sample();
    s.Delete(std::vector<int>{5});
    json::Value saved = s.Save();
    saved.Set("future", "kept");
    json::Value& first = saved.Find("entities")->At(0);
    first.Set("components", json::Value::MakeArray());  // 新しいバージョンのキー

    Scene t;
    reflect::Warnings w;
    t.Load(saved, &w);
    CHECK(w.empty());
    CHECK((Order(t) == Order(s)));
    const json::Value again = t.Save();
    CHECK(again == saved);
    CHECK(again.Get("future").AsString() == "kept");
    CHECK(again.Get("entities").At(0).Find("components") != nullptr);
    // 次の ID は、削除と再読み込みの後も残る。
    CHECK(t.Create("x") == 6);
}

RM_TEST(Scene_Load_RepairsBrokenFiles) {
    auto entity = [](int id, int parent, const char* name) {
        json::Value e = json::Value::MakeObject();
        e.Set("id", id);
        e.Set("parent", parent);
        e.Set("name", name);
        return e;
    };
    json::Value v = json::Value::MakeObject();
    json::Value list = json::Value::MakeArray();
    list.Push(entity(1, 0, "a"));
    list.Push(entity(1, 0, "same id"));
    list.Push(entity(0, 0, "no id"));
    list.Push(entity(4, 50, "missing parent"));
    list.Push(entity(5, 6, "loop 1"));
    list.Push(entity(6, 5, "loop 2"));
    list.Push(entity(7, 7, "own parent"));
    v.Set("entities", std::move(list));

    Scene s;
    reflect::Warnings w;
    s.Load(v, &w);
    CHECK(w.size() >= 5);
    // ID はすべて一意で正の数。すべてのエンティティが最上位から届く。
    std::vector<int> ids;
    for (const auto& e : s.Entities()) ids.push_back(e.data.id);
    CHECK(s.TreeOrder().size() == s.Entities().size());
    for (size_t i = 0; i < ids.size(); ++i) {
        CHECK(ids[i] > 0);
        for (size_t k = i + 1; k < ids.size(); ++k) CHECK(ids[i] != ids[k]);
    }
    CHECK(s.Find(4)->data.parent == 0);
    CHECK(s.Find(7)->data.parent == 0);
    // 新しい ID は、読み込んだものとぶつからない。
    const int fresh = s.Create("new");
    CHECK(fresh > 7);
}

namespace {

const reflect::TypeDesc& TransformType() { return reflect::TypeOf<space3d::Transform>(); }

}  // namespace

RM_TEST(Scene_Components_AddRemoveDuplicate) {
    Scene s = Sample();
    s.SetComponentTypes({&TransformType()});
    CHECK(s.AddComponent(1, TransformType()));
    CHECK(!s.AddComponent(1, TransformType()));  // 型ごとに 1 つ
    CHECK(!s.AddComponent(99, TransformType()));
    Component* c = s.Find(1)->FindComponent(TransformType().name);
    CHECK(c && c->Known());
    auto* t = static_cast<space3d::Transform*>(c->value.Get());
    CHECK(t->scale.x == 1 && t->position.y == 0);  // 型の既定値
    t->position = {1, 2, 3};
    // エンティティのコピーは、部品も自分のコピーを持つ。
    const auto made = s.Duplicate(std::vector<int>{1});
    auto* copy = static_cast<space3d::Transform*>(s.Find(made[0])->FindComponent(TransformType().name)->value.Get());
    CHECK(copy != t && copy->position.z == 3);
    copy->position.z = 9;
    CHECK(t->position.z == 3);
    CHECK(s.RemoveComponent(1, TransformType().name));
    CHECK(!s.RemoveComponent(1, TransformType().name));
    CHECK(s.Find(1)->components.empty());
}

RM_TEST(Scene_Components_SaveLoadAndUnknownTypes) {
    Scene s = Sample();
    s.SetComponentTypes({&TransformType()});
    s.AddComponent(2, TransformType());
    static_cast<space3d::Transform*>(s.Find(2)->FindComponent(TransformType().name)->value.Get())->rotation = {0, 45, 0};
    json::Value saved = s.Save();
    // ここに無いプラグインの部品と、知っている部品の中の新しいバージョンのキー。
    json::Value& e2 = saved.Find("entities")->At(1);
    json::Value foreign = json::Value::MakeObject();
    foreign.Set("type", "someone::Physics");
    foreign.Set("data", json::Value::MakeObject());
    foreign.Find("data")->Set("mass", 2.5);
    e2.Find("components")->Push(foreign);
    e2.Find("components")->At(0).Find("data")->Set("pivot", "center");

    Scene t;
    t.SetComponentTypes({&TransformType()});
    reflect::Warnings w;
    t.Load(saved, &w);
    CHECK(w.size() == 1);  // 未知の部品
    const SceneEntity* e = t.Find(2);
    CHECK(e->components.size() == 2);
    CHECK(e->components[0].Known() && !e->components[1].Known());
    CHECK(static_cast<const space3d::Transform*>(e->components[0].value.Get())->rotation.y == 45);
    CHECK(t.Save() == saved);  // どちらも、そのまま書き戻す

    // 型を渡さない (3D 空間 が入っていない): 未知として読み、それでも書き戻す。
    Scene none;
    none.Load(saved, nullptr);
    CHECK(!none.Find(2)->components[0].Known());
    CHECK(none.Save() == saved);
}

RM_TEST(Scene_CopyPaste_NewIdsWhereAsked) {
    Scene s = Sample();
    s.SetComponentTypes({&TransformType()});
    s.AddComponent(2, TransformType());
    static_cast<space3d::Transform*>(s.Find(2)->FindComponent(TransformType().name)->value.Get())->position = {1, 2, 3};
    const json::Value clip = s.Copy(std::vector<int>{1, 3});  // 3 は 1 の下: いっしょにコピーされ、1 回だけ
    CHECK(Scene::IsClip(clip) && clip.Get("entities").Items().size() == 4);
    CHECK(s.Entities().size() == 5);  // コピーしても何も変わらない

    // 最上位の末尾に: 新しい ID で、その下は同じ形。
    const auto roots = s.Paste(clip);
    CHECK((roots == std::vector<int>{6}));
    CHECK((s.Children(0) == std::vector<int>{1, 5, 6}));
    const auto kids = s.Children(6);
    CHECK(kids.size() == 2 && s.Find(kids[0])->data.name == "A1" && s.Find(kids[1])->data.name == "A2");
    CHECK(s.Children(kids[0]).size() == 1);
    // 部品もついてきた。自分のコピーとして。
    auto* t = static_cast<space3d::Transform*>(s.Find(kids[0])->FindComponent(TransformType().name)->value.Get());
    CHECK(t->position.z == 3);
    t->position.z = 9;
    CHECK(static_cast<space3d::Transform*>(s.Find(2)->FindComponent(TransformType().name)->value.Get())->position.z == 3);

    // まず B の下に。2 回でもよい (また新しい ID)。
    const auto under = s.Paste(clip, 5, 0);
    CHECK(under.size() == 1 && s.Children(5).front() == under[0] && s.Find(under[0])->data.parent == 5);
    // 無い親: 最上位。エンティティでないもの: 何もしない。
    CHECK(s.Find(s.Paste(clip, 999)[0])->data.parent == 0);
    CHECK(s.Paste(json::Value("hello")).empty());
    CHECK(s.Paste(s.Save()).empty());  // シーン全体はクリップではない
}

RM_TEST(Scene_Paste_NeverMakesALoop) {
    Scene s;
    // 手で書いた (または壊れた): 子の後にある親、親のループ、繰り返した ID。
    json::Value clip = json::Value::MakeObject();
    clip.Set("kind", "rm_engine.game_production.entities");
    json::Value list = json::Value::MakeArray();
    auto entity = [&](int id, int parent, const char* name) {
        json::Value e = json::Value::MakeObject();
        e.Set("id", id);
        e.Set("parent", parent);
        e.Set("name", name);
        list.Push(std::move(e));
    };
    entity(1, 2, "a");  // 親が後に来る: ルート
    entity(2, 1, "b");  // a の下
    entity(2, 0, "c");  // 同じ ID がもう一度: 自分のルート
    entity(3, 3, "d");  // 自分が親: ルート
    clip.Set("entities", std::move(list));
    const auto roots = s.Paste(clip);
    CHECK(roots.size() == 3);
    CHECK(s.Entities().size() == 4);
    CHECK(s.TreeOrder().size() == 4);  // すべてのエンティティが最上位から届く
}

RM_TEST(Scene_MoveComponent_UpAndDown) {
    Scene s = Sample();
    s.SetComponentTypes({&TransformType()});
    s.AddComponent(1, TransformType());
    // 2 つ目の部品: ここに無いプラグインのもの (読んだまま残す)。Transform の後。
    json::Value saved = s.Save();
    json::Value foreign = json::Value::MakeObject();
    foreign.Set("type", "someone::Physics");
    saved.Find("entities")->At(0).Find("components")->Push(foreign);
    s.Load(saved);
    const std::string t = TransformType().name;
    CHECK(!s.MoveComponent(1, t, -1));  // もう先頭
    CHECK(s.MoveComponent(1, t, 1));
    CHECK(s.Find(1)->components[0].type == "someone::Physics" && s.Find(1)->components[1].type == t);
    CHECK(!s.MoveComponent(1, t, 1));  // もう最後
    CHECK(s.MoveComponent(1, "someone::Physics", 1));
    CHECK(s.Find(1)->components[0].type == t);
    CHECK(!s.MoveComponent(1, "nothing", 1));
    CHECK(!s.MoveComponent(99, t, 1));
    CHECK(!s.MoveComponent(1, t, 2));  // 一度に 1 つずつ
}

namespace {

double SecondsSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

// 1 万のエンティティ: 100 本の 100 個の鎖 (それぞれ次の親)。どれも Transform と
// 回り続ける を持つ - エンティティごとにリストをたどるものが目立つくらい、深くて広い。
json::Value BigScene() {
    const std::string transform = TransformType().name;
    const std::string spin = reflect::TypeOf<game_x_3d::Spin>().name;
    json::Value list = json::Value::MakeArray();
    for (int i = 1; i <= 10000; ++i) {
        json::Value e = json::Value::MakeObject();
        e.Set("id", i);
        e.Set("parent", i % 100 == 1 ? 0 : i - 1);
        e.Set("name", "e");
        json::Value comps = json::Value::MakeArray();
        for (const std::string* type : {&transform, &spin}) {
            json::Value c = json::Value::MakeObject();
            c.Set("type", *type);
            c.Set("data", json::Value::MakeObject());
            comps.Push(std::move(c));
        }
        e.Set("components", std::move(comps));
        list.Push(std::move(e));
    }
    json::Value o = json::Value::MakeObject();
    o.Set("nextId", 10001);
    o.Set("entities", std::move(list));
    return o;
}

}  // namespace

RM_TEST(Scene_TenThousand_InReasonableTime) {
    // 上限はわざとゆるくしてある (遅い PC や忙しい PC で失敗してはいけない): 小さな遅れではなく、
    // エンティティの数の 2 乗で増える作業 (ここでは数秒から数分) を捕まえる。
    const reflect::TypeDesc& spin = reflect::TypeOf<game_x_3d::Spin>();
    Scene s;
    s.SetComponentTypes({&TransformType(), &spin}, {&spin});
    auto t = std::chrono::steady_clock::now();
    s.Load(BigScene());
    CHECK(s.Entities().size() == 10000);
    CHECK(SecondsSince(t) < 5.0);

    t = std::chrono::steady_clock::now();
    long long found = 0;
    for (int round = 0; round < 10; ++round) {
        for (int id = 1; id <= 10000; ++id) found += s.Find(id) ? 1 : 0;
    }
    CHECK(found == 100000);
    CHECK(SecondsSince(t) < 3.0);

    // すべてのエンティティに対するシステムの 1 フレーム。
    static const SystemInfo kSpin{"回り続ける", SystemPhase::Frame, 0, &game_x_3d::RunSpin};
    const SystemInfo* list[] = {&kSpin};
    SystemRunner runner;
    runner.SetSystems(list);
    t = std::chrono::steady_clock::now();
    runner.Advance(s, 0.1, 0.02f);
    CHECK(SecondsSince(t) < 3.0);
    CHECK(static_cast<const space3d::Transform*>(s.Find(10000)->FindComponent(TransformType().name)->value.Get())->rotation.y > 0);

    // 半分のルートを選び、鎖のコピーを作り、その後で半分を削除する。
    std::vector<int> half;
    for (int id = 1; id <= 5000; ++id) half.push_back(id);
    t = std::chrono::steady_clock::now();
    CHECK(s.Roots(half).size() == 50);
    CHECK(s.Duplicate(std::vector<int>{101}).size() == 1);
    CHECK(s.Delete(half) == 5000);
    CHECK(SecondsSince(t) < 5.0);
    CHECK(s.Entities().size() == 5100);
}

RM_TEST(Scene_IndexStaysRightAfterEveryChange) {
    Scene s = Sample();
    auto allFound = [&](const Scene& sc) {
        for (const auto& e : sc.Entities()) {
            if (sc.Find(e.data.id) != &e) return false;
        }
        return true;
    };
    CHECK(allFound(s));
    const json::Value before = s.Save();
    const int made = s.Create("new", 1, 0);
    CHECK(allFound(s) && s.Find(made)->data.name == "new");
    CHECK(s.Move(std::vector<int>{5}, 0, 0));
    CHECK(allFound(s));
    const auto copies = s.Duplicate(std::vector<int>{1});
    CHECK(allFound(s) && s.Find(copies[0]));
    s.Delete(std::vector<int>{2});
    CHECK(allFound(s) && !s.Find(2) && !s.Find(3));
    s.Paste(s.Copy(std::vector<int>{1}));
    CHECK(allFound(s));
    // Undo がすること: 前のシーンに戻る。その後に作った ID は消え、他は見つかる。
    s.Load(before);
    CHECK(allFound(s) && !s.Find(made) && s.Find(3) && s.Find(3)->data.name == "A1a");
    // シーンのコピーは、自分の正しいインデックスを保つ。
    Scene copy = s;
    CHECK(allFound(copy) && copy.Find(1) != s.Find(1));
}

RM_TEST(Scene_Settings) {
    // シーン自身の値 (SceneSettingInfo): 型ごとに 1 つ出され、変えるまでは既定値。
    const reflect::TypeDesc& spin = reflect::TypeOf<game_x_3d::Spin>();
    Scene s = Sample();
    CHECK(s.Settings().empty());
    CHECK(!s.Save().Find("settings"));  // 何も出さない: ファイルは前のまま
    s.SetSettingTypes({&spin});
    CHECK(s.Settings().size() == 1 && s.Setting<game_x_3d::Spin>() != nullptr);
    s.Setting<game_x_3d::Spin>()->degreesPerSecond.y = 45;
    const json::Value saved = s.Save();
    CHECK(saved.Get("settings").Size() == 1 && saved.Get("settings").At(0).Get("type").AsString() == spin.name);

    // 読み戻す (型を Load の前に設定しても後でも同じ)。
    Scene a;
    a.SetSettingTypes({&spin});
    a.Load(saved);
    CHECK(a.Setting<game_x_3d::Spin>() && a.Setting<game_x_3d::Spin>()->degreesPerSecond.y == 45);
    Scene b;
    b.Load(saved);
    CHECK(b.Settings().size() == 1 && !b.Settings()[0].Known());  // 出していない: 読んだまま残す
    CHECK(b.Save().Get("settings") == saved.Get("settings"));     // ... そして同じに書き戻す
    b.SetSettingTypes({&spin});
    CHECK(b.Setting<game_x_3d::Spin>() && b.Setting<game_x_3d::Spin>()->degreesPerSecond.y == 45);

    // 設定の無いファイル: 既定値。設定の中の未知のキーは残る。型ごとの 2 つ目は捨てる。
    json::Value v = saved;
    json::Value list = json::Value::MakeArray();
    json::Value one = saved.Get("settings").At(0);
    json::Value data = one.Get("data");
    data.Set("future", 7);
    one.Set("data", data);
    list.Push(one);
    list.Push(saved.Get("settings").At(0));
    v.Set("settings", list);
    Scene c;
    c.SetSettingTypes({&spin});
    c.Load(v);
    CHECK(c.Settings().size() == 1);
    CHECK(c.Save().Get("settings").At(0).Get("data").Get("future").AsInt() == 7);
    v.Erase("settings");
    c.Load(v);
    CHECK(c.Setting<game_x_3d::Spin>() && c.Setting<game_x_3d::Spin>()->degreesPerSecond.y == game_x_3d::Spin{}.degreesPerSecond.y);
    // コピー (Play) は自分のものを持つ。
    Scene d = c;
    d.Setting<game_x_3d::Spin>()->degreesPerSecond.y = 1;
    CHECK(c.Setting<game_x_3d::Spin>()->degreesPerSecond.y != 1);
}
