// Tests for the scene of the game production plugin (runtime/Scene): the shape of the entity tree,
// moving / copying / deleting, and the save format (unknown keys kept, broken files repaired).

#include "Test.h"

#include "rm_engine/game_production/runtime/Scene.h"

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
