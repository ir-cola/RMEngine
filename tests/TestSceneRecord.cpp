// Tests for SceneRecord (game production's runtime/SceneRecord): undo of a change of the scene's
// shape kept as the touched entities only. Thousands of random changes - the same kinds the editor
// makes, touching what the editor says it touches - each undone and redone and compared with the
// whole scene. And how much one entry keeps in a scene of 10,000 entities, against the whole scene.

#include "Test.h"

#include "rm_engine/game_production/runtime/Prefab.h"
#include "rm_engine/game_production/runtime/SceneRecord.h"
#include "rm_engine/game_x_3d/runtime/Spin.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <chrono>
#include <cstdio>
#include <format>
#include <random>

using namespace rm;
using namespace rm::game_production;

namespace {

const reflect::TypeDesc& Tf() { return reflect::TypeOf<space3d::Transform>(); }
const reflect::TypeDesc& Sp() { return reflect::TypeOf<game_x_3d::Spin>(); }

// The whole scene but the next id (undo never takes it back: ids are not used again).
json::Value Shape(const Scene& s) {
    json::Value v = s.Save();
    v.Erase("nextId");
    return v;
}

bool IndexRight(const Scene& s) {
    for (const auto& e : s.Entities()) {
        if (s.Find(e.data.id) != &e) return false;
    }
    return true;
}

struct World {
    Scene scene;
    std::mt19937 rng{20261112};
    std::unordered_map<std::string, json::Value> files;  // asset id -> prefab file
    int nextAsset = 1;

    int Pick() {
        const auto& list = scene.Entities();
        if (list.empty()) return 0;
        return list[std::uniform_int_distribution<size_t>(0, list.size() - 1)(rng)].data.id;
    }
    int PickOrTop() { return Chance(4) ? 0 : Pick(); }
    bool Chance(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng) == 0; }
    size_t Index() { return Chance(3) ? Scene::kEnd : std::uniform_int_distribution<size_t>(0, 4)(rng); }
    std::vector<int> Some() {
        std::vector<int> ids;
        const int n = std::uniform_int_distribution<int>(1, 3)(rng);
        for (int i = 0; i < n; ++i) {
            if (const int id = Pick()) ids.push_back(id);
        }
        return ids;
    }
    std::vector<int> Roots() { return scene.PrefabRoots(); }

    // The prefabs nested in `root`, from their files (what the editor's FillNested does).
    void Fill(int root, std::vector<std::string>& chain) {
        if (chain.size() > 8) return;
        for (const int n : scene.NestedPrefabs(root)) {
            const SceneEntity* e = scene.Find(n);
            if (!e) continue;
            const std::string g = e->prefab.asset.guid;
            if (std::find(chain.begin(), chain.end(), g) != chain.end() || !files.contains(g)) continue;
            const json::Value file = files[g];
            const json::Value overrides = e->prefab.overrides;
            scene.ApplyPrefab(n, file, overrides);
            chain.push_back(g);
            Fill(n, chain);
            chain.pop_back();
        }
    }
    bool Contains(const json::Value& file, const std::string& g, int depth = 0) {
        if (depth > 8) return true;
        for (const std::string& a : NestedAssets(file)) {
            if (a == g || (files.contains(a) && Contains(files[a], g, depth + 1))) return true;
        }
        return false;
    }
};

// One random change: what it touches (as the editor names it) and the change itself.
struct Change {
    std::vector<int> touched;
    std::function<void()> run;
    const char* kind;
};

Change RandomChange(World& w) {
    Scene& s = w.scene;
    const bool big = s.Entities().size() > 70;
    const int kind = big ? 1 : std::uniform_int_distribution<int>(0, 11)(w.rng);
    switch (kind) {
        case 0: {
            const int parent = w.PickOrTop();
            const size_t index = w.Index();
            return {{}, [&s, parent, index] { s.Create("e", parent, index); }, "create"};
        }
        case 1: {
            std::vector<int> ids = w.Some();
            if (big) ids = {s.TreeOrder().front().entity->data.id};
            return {ids, [&s, ids] { s.Delete(ids); }, "delete"};
        }
        case 2: {
            const std::vector<int> ids = w.Some();
            const int parent = w.PickOrTop();
            const size_t index = w.Index();
            return {ids, [&s, ids, parent, index] { s.Move(ids, parent, index); }, "move"};
        }
        case 3: {
            const int id = w.Pick();
            const SceneEntity* e = s.Find(id);
            const int parent = e ? e->data.parent : 0;
            const size_t index = w.Index();
            return {{id}, [&s, id, parent, index] { s.Move(std::vector<int>{id}, parent, index); }, "reorder"};
        }
        case 4: {
            const json::Value clip = s.Copy(w.Some());
            const int parent = w.PickOrTop();
            const size_t index = w.Index();
            return {{}, [&s, clip, parent, index] { s.Paste(clip, parent, index); }, "paste"};
        }
        case 5: {
            const std::vector<int> ids = w.Some();
            return {{}, [&s, ids] { s.Duplicate(ids); }, "duplicate"};
        }
        case 6: {
            const int id = w.Pick();
            const reflect::TypeDesc* t = w.Chance(2) ? &Tf() : &Sp();
            return {{id}, [&s, id, t] { s.AddComponent(id, *t); }, "add component"};
        }
        case 7: {
            const int id = w.Pick();
            const std::string type = w.Chance(2) ? Tf().name : Sp().name;
            return {{id}, [&s, id, type] { s.RemoveComponent(id, type); }, "remove component"};
        }
        case 8: {  // 「Prefab にする」
            const int id = w.Pick();
            if (!id) return {{}, [] {}, "nothing"};
            const AssetRef ref{std::format("{:032x}", w.nextAsset++)};
            return {{id}, [&w, id, ref] {
                        std::unordered_map<int, int> sources;
                        w.files[ref.guid] = MakePrefab(w.scene, id, &sources);
                        w.scene.LinkPrefab(id, ref, sources);
                    },
                    "make prefab"};
        }
        case 9: {  // 「Prefab を置く」
            if (w.files.empty()) return {{}, [] {}, "nothing"};
            auto it = w.files.begin();
            std::advance(it, std::uniform_int_distribution<size_t>(0, w.files.size() - 1)(w.rng));
            const AssetRef ref{it->first};
            const json::Value file = it->second;
            const int parent = w.PickOrTop();
            return {{}, [&w, ref, file, parent] {
                        const int made = w.scene.PlacePrefab(file, ref, parent);
                        std::vector<std::string> chain{ref.guid};
                        if (made) w.Fill(made, chain);
                    },
                    "place prefab"};
        }
        case 10: {  // 「Prefab に反映」 from one placed root: every placed one of that prefab is touched
            const std::vector<int> roots = w.Roots();
            if (roots.empty()) return {{}, [] {}, "nothing"};
            const int root = roots[std::uniform_int_distribution<size_t>(0, roots.size() - 1)(w.rng)];
            const AssetRef ref = s.Find(root)->prefab.asset;
            if (!w.files.contains(ref.guid)) return {{}, [] {}, "nothing"};
            std::unordered_map<int, int> sources;
            const json::Value before = w.files[ref.guid];
            const json::Value next = MakePrefab(s, root, &sources, &before);
            if (w.Contains(next, ref.guid)) return {{}, [] {}, "nothing"};
            return {s.PrefabRoots(ref.guid), [&w, root, ref, sources, before, next] {
                        Scene& sc = w.scene;
                        w.files[ref.guid] = next;
                        sc.LinkPrefab(root, ref, sources);
                        for (const int r : sc.PrefabRoots(ref.guid)) {
                            if (r == root || !sc.Find(r)) continue;
                            sc.ApplyPrefab(r, next, Overrides(sc, r, before));
                            std::vector<std::string> chain{ref.guid};
                            w.Fill(r, chain);
                        }
                    },
                    "apply to prefab"};
        }
        default: {  // 「Prefab から切り離す」
            const std::vector<int> roots = w.Roots();
            if (roots.empty()) return {{}, [] {}, "nothing"};
            const int root = roots[std::uniform_int_distribution<size_t>(0, roots.size() - 1)(w.rng)];
            return {{root}, [&s, root] { s.UnlinkPrefab(root); }, "unlink"};
        }
    }
}

}  // namespace

RM_TEST(SceneRecord_ThousandsOfRandomChanges_UndoAndRedoGiveTheWholeScene) {
    World w;
    w.scene.SetComponentTypes({&Tf(), &Sp()}, {&Sp()});
    for (int i = 0; i < 6; ++i) w.scene.Create("start", i % 2 ? w.Pick() : 0);
    int failures = 0, changed = 0;
    for (int step = 0; step < 4000 && failures < 5; ++step) {
        Change c = RandomChange(w);
        const json::Value before = Shape(w.scene);
        SceneRecord r = SceneRecord::Begin(w.scene, c.touched);
        c.run();
        r.End(w.scene);
        const json::Value after = Shape(w.scene);
        if (r.Changed()) ++changed;
        CHECK(r.Changed() == (before != after));
        r.Undo(w.scene);
        const bool undone = Shape(w.scene) == before && IndexRight(w.scene);
        r.Redo(w.scene);
        const bool redone = Shape(w.scene) == after && IndexRight(w.scene);
        CHECK(undone && redone);
        if (!undone || !redone) {
            ++failures;
            std::printf("    step %d (%s): undo %s, redo %s\n", step, c.kind, undone ? "ok" : "WRONG", redone ? "ok" : "WRONG");
        }
    }
    CHECK(changed > 2000);  // most of them really changed something
}

RM_TEST(SceneRecord_UndoInOrderThroughManyChanges) {
    // Several records undone newest first, then redone oldest first: each step the scene as it was.
    World w;
    w.scene.SetComponentTypes({&Tf(), &Sp()}, {&Sp()});
    for (int i = 0; i < 6; ++i) w.scene.Create("start");
    std::vector<json::Value> shapes{Shape(w.scene)};
    std::vector<SceneRecord> records;
    for (int step = 0; step < 300; ++step) {
        Change c = RandomChange(w);
        SceneRecord r = SceneRecord::Begin(w.scene, c.touched);
        c.run();
        r.End(w.scene);
        records.push_back(std::move(r));
        shapes.push_back(Shape(w.scene));
    }
    bool ok = true;
    for (size_t i = records.size(); i-- > 0;) {
        records[i].Undo(w.scene);
        ok = ok && Shape(w.scene) == shapes[i];
    }
    CHECK(ok);
    for (size_t i = 0; i < records.size(); ++i) {
        records[i].Redo(w.scene);
        ok = ok && Shape(w.scene) == shapes[i + 1];
    }
    CHECK(ok && IndexRight(w.scene));
}

RM_TEST(SceneRecord_TenThousand_OneDeleteKeepsLittle) {
    // 100 chains of 100, every entity with a Transform. One leaf deleted: the whole scene twice (as
    // before) against the touched entity only.
    json::Value list = json::Value::MakeArray();
    const std::string tf = Tf().name;
    for (int i = 1; i <= 10000; ++i) {
        json::Value e = json::Value::MakeObject();
        e.Set("id", i);
        e.Set("parent", i % 100 == 1 ? 0 : i - 1);
        e.Set("name", "e");
        json::Value c = json::Value::MakeObject();
        c.Set("type", tf);
        c.Set("data", json::Value::MakeObject());
        json::Value comps = json::Value::MakeArray();
        comps.Push(std::move(c));
        e.Set("components", std::move(comps));
        list.Push(std::move(e));
    }
    json::Value file = json::Value::MakeObject();
    file.Set("nextId", 10001);
    file.Set("entities", std::move(list));
    Scene a, b;
    for (Scene* s : {&a, &b}) {
        s->SetComponentTypes({&Tf()});
        s->Load(file);
    }
    using clock = std::chrono::steady_clock;
    // Before: the whole scene before and after, as text (what the history kept).
    auto t = clock::now();
    const json::Value wholeBefore = a.Save();
    a.Delete(std::vector<int>{100});
    const json::Value wholeAfter = a.Save();
    const size_t wholeBytes = json::Write(wholeBefore, false).size() + json::Write(wholeAfter, false).size();
    const double wholeMs = std::chrono::duration<double, std::milli>(clock::now() - t).count();
    // After: the record of the touched entity.
    t = clock::now();
    SceneRecord r = SceneRecord::Begin(b, std::vector<int>{100});
    b.Delete(std::vector<int>{100});
    r.End(b);
    const double recordMs = std::chrono::duration<double, std::milli>(clock::now() - t).count();
    std::printf("    10000 entities, one deleted: whole scene %zu bytes %.1f ms / record %zu bytes %.2f ms\n", wholeBytes, wholeMs,
                r.Bytes(), recordMs);
    CHECK(r.Entities() == 1 && r.Bytes() * 1000 < wholeBytes);
    CHECK(Shape(a) == Shape(b));
    r.Undo(b);
    CHECK(b.Find(100) && b.Find(100)->data.parent == 99 && Shape(b) == [&] {
        json::Value v = wholeBefore;
        v.Erase("nextId");
        return v;
    }());
}
