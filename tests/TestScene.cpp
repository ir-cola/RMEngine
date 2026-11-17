// Tests for the scene of the game production plugin (runtime/Scene): the shape of the entity tree,
// moving / copying / deleting, and the save format (unknown keys kept, broken files repaired).

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
    // At an index among the siblings.
    const int first = s.Create("first", 0, 0);
    CHECK((s.Children(0) == std::vector<int>{first, 1, 5}));
    const int mid = s.Create("mid", 1, 1);
    CHECK((s.Children(1) == std::vector<int>{2, mid, 4}));
    // A missing parent: top level.
    const int lost = s.Create("lost", 999);
    CHECK(s.Find(lost)->data.parent == 0);
}

RM_TEST(Scene_ActiveInHierarchy) {
    Scene s = Sample();
    s.Find(2)->data.active = false;
    for (const TreeEntry& t : s.TreeOrder()) {
        const int id = t.entity->data.id;
        CHECK(t.activeInHierarchy == (id != 2 && id != 3));  // A1a is off because A1 is
    }
}

RM_TEST(Scene_Move_ReorderReparentAndRefuse) {
    Scene s = Sample();
    // B before A (index counted without B).
    CHECK(s.Move(std::vector<int>{5}, 0, 0));
    CHECK((s.Children(0) == std::vector<int>{5, 1}));
    // The same again changes nothing.
    CHECK(!s.Move(std::vector<int>{5}, 0, 0));
    // A2 into B.
    CHECK(s.Move(std::vector<int>{4}, 5, 0));
    CHECK(s.Find(4)->data.parent == 5);
    CHECK((s.Children(1) == std::vector<int>{2}));
    // Never into itself or below itself, never under a missing parent.
    CHECK(!s.Move(std::vector<int>{1}, 3, 0));
    CHECK(!s.Move(std::vector<int>{1}, 1, 0));
    CHECK(!s.Move(std::vector<int>{2}, 777, 0));
    CHECK(s.Find(1)->data.parent == 0);
    // Several at once, in tree order (A2 is under B, which comes first); a child of a moved one
    // goes along (A1a stays under A1).
    CHECK(s.Move(std::vector<int>{3, 2, 4}, 0, Scene::kEnd));
    CHECK((s.Children(0) == std::vector<int>{5, 1, 4, 2}));
    CHECK(s.Find(3)->data.parent == 2);
}

RM_TEST(Scene_Duplicate_CopiesSubtreeNextToOriginal) {
    Scene s = Sample();
    const auto made = s.Duplicate(std::vector<int>{1, 3});  // 3 is under 1: copied with it, once
    CHECK(made.size() == 1);
    const int copy = made[0];
    CHECK(copy == 6);
    CHECK((s.Children(0) == std::vector<int>{1, copy, 5}));
    CHECK(s.Entities().size() == 9);
    const auto kids = s.Children(copy);
    CHECK(kids.size() == 2);
    CHECK(s.Find(kids[0])->data.name == "A1" && s.Find(kids[1])->data.name == "A2");
    CHECK(s.Children(kids[0]).size() == 1);
    // The originals are untouched.
    CHECK((s.Children(1) == std::vector<int>{2, 4}));
}

RM_TEST(Scene_Delete_TakesEverythingBelow) {
    Scene s = Sample();
    CHECK(s.Delete(std::vector<int>{2, 5}) == 3);
    CHECK((Order(s) == std::vector<int>{1, 4}));
    CHECK(s.Delete(std::vector<int>{42}) == 0);
    // Ids are never reused.
    CHECK(s.Create("new") == 6);
}

RM_TEST(Scene_SaveLoad_RoundTripAndUnknownKeys) {
    Scene s = Sample();
    s.Delete(std::vector<int>{5});
    json::Value saved = s.Save();
    saved.Set("future", "kept");
    json::Value& first = saved.Find("entities")->At(0);
    first.Set("components", json::Value::MakeArray());  // a key of a newer version

    Scene t;
    reflect::Warnings w;
    t.Load(saved, &w);
    CHECK(w.empty());
    CHECK((Order(t) == Order(s)));
    const json::Value again = t.Save();
    CHECK(again == saved);
    CHECK(again.Get("future").AsString() == "kept");
    CHECK(again.Get("entities").At(0).Find("components") != nullptr);
    // The next id survives a deletion and a reload.
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
    // Every id unique and positive, every entity reachable from the top.
    std::vector<int> ids;
    for (const auto& e : s.Entities()) ids.push_back(e.data.id);
    CHECK(s.TreeOrder().size() == s.Entities().size());
    for (size_t i = 0; i < ids.size(); ++i) {
        CHECK(ids[i] > 0);
        for (size_t k = i + 1; k < ids.size(); ++k) CHECK(ids[i] != ids[k]);
    }
    CHECK(s.Find(4)->data.parent == 0);
    CHECK(s.Find(7)->data.parent == 0);
    // New ids never collide with loaded ones.
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
    CHECK(!s.AddComponent(1, TransformType()));  // one of each type
    CHECK(!s.AddComponent(99, TransformType()));
    Component* c = s.Find(1)->FindComponent(TransformType().name);
    CHECK(c && c->Known());
    auto* t = static_cast<space3d::Transform*>(c->value.Get());
    CHECK(t->scale.x == 1 && t->position.y == 0);  // the type's defaults
    t->position = {1, 2, 3};
    // A copy of the entity has its own copy of the component.
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
    // A component of a plugin that is not here, and a key of a newer version inside a known one.
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
    CHECK(w.size() == 1);  // the unknown component
    const SceneEntity* e = t.Find(2);
    CHECK(e->components.size() == 2);
    CHECK(e->components[0].Known() && !e->components[1].Known());
    CHECK(static_cast<const space3d::Transform*>(e->components[0].value.Get())->rotation.y == 45);
    CHECK(t.Save() == saved);  // both written back as they were

    // Without the type offered (3D 空間 not installed): read as unknown, still written back.
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
    const json::Value clip = s.Copy(std::vector<int>{1, 3});  // 3 is under 1: copied with it, once
    CHECK(Scene::IsClip(clip) && clip.Get("entities").Items().size() == 4);
    CHECK(s.Entities().size() == 5);  // copying changes nothing

    // At the end of the top level: new ids, the same shape below.
    const auto roots = s.Paste(clip);
    CHECK((roots == std::vector<int>{6}));
    CHECK((s.Children(0) == std::vector<int>{1, 5, 6}));
    const auto kids = s.Children(6);
    CHECK(kids.size() == 2 && s.Find(kids[0])->data.name == "A1" && s.Find(kids[1])->data.name == "A2");
    CHECK(s.Children(kids[0]).size() == 1);
    // The component came along, a copy of its own.
    auto* t = static_cast<space3d::Transform*>(s.Find(kids[0])->FindComponent(TransformType().name)->value.Get());
    CHECK(t->position.z == 3);
    t->position.z = 9;
    CHECK(static_cast<space3d::Transform*>(s.Find(2)->FindComponent(TransformType().name)->value.Get())->position.z == 3);

    // Under B, first; twice is fine (new ids again).
    const auto under = s.Paste(clip, 5, 0);
    CHECK(under.size() == 1 && s.Children(5).front() == under[0] && s.Find(under[0])->data.parent == 5);
    // A missing parent: the top level. Not entities: nothing.
    CHECK(s.Find(s.Paste(clip, 999)[0])->data.parent == 0);
    CHECK(s.Paste(json::Value("hello")).empty());
    CHECK(s.Paste(s.Save()).empty());  // a whole scene is not a clip
}

RM_TEST(Scene_Paste_NeverMakesALoop) {
    Scene s;
    // Written by hand (or broken): a parent after its child, a parent loop, a repeated id.
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
    entity(1, 2, "a");  // its parent comes later: a root
    entity(2, 1, "b");  // under a
    entity(2, 0, "c");  // the same id again: a root of its own
    entity(3, 3, "d");  // its own parent: a root
    clip.Set("entities", std::move(list));
    const auto roots = s.Paste(clip);
    CHECK(roots.size() == 3);
    CHECK(s.Entities().size() == 4);
    CHECK(s.TreeOrder().size() == 4);  // every entity is reachable from the top
}

RM_TEST(Scene_MoveComponent_UpAndDown) {
    Scene s = Sample();
    s.SetComponentTypes({&TransformType()});
    s.AddComponent(1, TransformType());
    // A second component: one of a plugin that is not here (kept as read), after the Transform.
    json::Value saved = s.Save();
    json::Value foreign = json::Value::MakeObject();
    foreign.Set("type", "someone::Physics");
    saved.Find("entities")->At(0).Find("components")->Push(foreign);
    s.Load(saved);
    const std::string t = TransformType().name;
    CHECK(!s.MoveComponent(1, t, -1));  // first already
    CHECK(s.MoveComponent(1, t, 1));
    CHECK(s.Find(1)->components[0].type == "someone::Physics" && s.Find(1)->components[1].type == t);
    CHECK(!s.MoveComponent(1, t, 1));  // last now
    CHECK(s.MoveComponent(1, "someone::Physics", 1));
    CHECK(s.Find(1)->components[0].type == t);
    CHECK(!s.MoveComponent(1, "nothing", 1));
    CHECK(!s.MoveComponent(99, t, 1));
    CHECK(!s.MoveComponent(1, t, 2));  // one place at a time
}

namespace {

double SecondsSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

// 10,000 entities: 100 chains of 100 (each the parent of the next), every one with a Transform and a
// 回り続ける - deep and wide enough that anything that walks the list per entity shows.
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
    // The limits are loose on purpose (a slow or busy PC must not fail): they catch work that grows
    // with the square of the entities (seconds to minutes here), not small slowdowns.
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

    // One frame of a system over every entity.
    static const SystemInfo kSpin{"回り続ける", SystemPhase::Frame, 0, &game_x_3d::RunSpin};
    const SystemInfo* list[] = {&kSpin};
    SystemRunner runner;
    runner.SetSystems(list);
    t = std::chrono::steady_clock::now();
    runner.Advance(s, 0.1, 0.02f);
    CHECK(SecondsSince(t) < 3.0);
    CHECK(static_cast<const space3d::Transform*>(s.Find(10000)->FindComponent(TransformType().name)->value.Get())->rotation.y > 0);

    // The roots of half of them, a copy of a chain, then half of them deleted.
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
    // What undo does: the scene from before. The ids made since are gone, the others found again.
    s.Load(before);
    CHECK(allFound(s) && !s.Find(made) && s.Find(3) && s.Find(3)->data.name == "A1a");
    // A copy of the scene keeps its own, right index.
    Scene copy = s;
    CHECK(allFound(copy) && copy.Find(1) != s.Find(1));
}

RM_TEST(Scene_Settings) {
    // The scene's own values (SceneSettingInfo): one of each type offered, with its defaults until changed.
    const reflect::TypeDesc& spin = reflect::TypeOf<game_x_3d::Spin>();
    Scene s = Sample();
    CHECK(s.Settings().empty());
    CHECK(!s.Save().Find("settings"));  // none offered: the file is as before
    s.SetSettingTypes({&spin});
    CHECK(s.Settings().size() == 1 && s.Setting<game_x_3d::Spin>() != nullptr);
    s.Setting<game_x_3d::Spin>()->degreesPerSecond.y = 45;
    const json::Value saved = s.Save();
    CHECK(saved.Get("settings").Size() == 1 && saved.Get("settings").At(0).Get("type").AsString() == spin.name);

    // Read back (the types set before or after Load: the same).
    Scene a;
    a.SetSettingTypes({&spin});
    a.Load(saved);
    CHECK(a.Setting<game_x_3d::Spin>() && a.Setting<game_x_3d::Spin>()->degreesPerSecond.y == 45);
    Scene b;
    b.Load(saved);
    CHECK(b.Settings().size() == 1 && !b.Settings()[0].Known());  // not offered: kept as read
    CHECK(b.Save().Get("settings") == saved.Get("settings"));     // ... and written back the same
    b.SetSettingTypes({&spin});
    CHECK(b.Setting<game_x_3d::Spin>() && b.Setting<game_x_3d::Spin>()->degreesPerSecond.y == 45);

    // A file without settings: the defaults. Unknown keys inside a setting stay; a second of a type is dropped.
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
    // A copy (Play) has its own.
    Scene d = c;
    d.Setting<game_x_3d::Spin>()->degreesPerSecond.y = 1;
    CHECK(c.Setting<game_x_3d::Spin>()->degreesPerSecond.y != 1);
}
