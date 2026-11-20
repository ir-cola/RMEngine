// コピー内のエンティティ参照 (EntityRef): reflect::MapEntityRefs と、ゲームのシーン - 複製、
// コピー & ペースト、Prefab (作る、置く、上書き、変えた Prefab) は、元ではなくコピーを指す。
// 参照を持つ部品は rmtest::Links (TestReflectTypes.h)。

#include "Test.h"

#include "TestReflectTypes.h"
#include "core/reflect/Data.h"
#include "rm_engine/game_production/runtime/Prefab.h"

#include <algorithm>

using namespace rm;
using namespace rm::game_production;
using rmtest::Links;

namespace {

const reflect::TypeDesc& L() { return reflect::TypeOf<Links>(); }

Links& LinksOf(Scene& s, int id) { return *static_cast<Links*>(s.Find(id)->FindComponent(L().name)->value.Get()); }

// A (Links: target -> B, followers -> [C])
//   B
// C
Scene Make(int* a, int* b, int* c) {
    Scene s;
    s.SetComponentTypes({&L()});
    *a = s.Create("A");
    *b = s.Create("B", *a);
    *c = s.Create("C");
    s.AddComponent(*a, L());
    LinksOf(s, *a).target.id = static_cast<uint64_t>(*b);
    LinksOf(s, *a).followers = {{static_cast<uint64_t>(*c)}};
    return s;
}

bool HasOverride(const json::Value& ov, const char* field) {
    const auto& list = ov.Get("fields").Items();
    return std::any_of(list.begin(), list.end(), [&](const json::Value& o) { return o.Get("field").AsString() == field; });
}

}  // namespace

RM_TEST(EntityRefs_MapInsideStructsAndArrays) {
    Links l;
    l.target.id = 5;
    l.followers = {{5}, {7}, {}};
    reflect::MapEntityRefs(L(), &l, [](uint64_t id) -> uint64_t { return id == 5 ? 50 : 0; });
    CHECK(l.target.id == 50);
    CHECK(l.followers[0].id == 50 && l.followers[1].id == 0 && l.followers[2].id == 0);
}

RM_TEST(EntityRefs_DuplicateAndPastePointAtTheCopies) {
    int a = 0, b = 0, c = 0;
    Scene s = Make(&a, &b, &c);
    // 複製した: A のコピーは B のコピーを指す。C はコピーされていない - 今も C のまま。
    const int a2 = s.Duplicate(std::vector<int>{a})[0];
    const int b2 = s.Children(a2)[0];
    CHECK(a2 != a && b2 != b);
    CHECK(LinksOf(s, a2).target.id == static_cast<uint64_t>(b2));
    CHECK(LinksOf(s, a2).followers[0].id == static_cast<uint64_t>(c));
    CHECK(LinksOf(s, a).target.id == static_cast<uint64_t>(b));  // 元は元のまま
    // コピー & ペースト: 同じ。
    const json::Value clip = s.Copy(std::vector<int>{a});
    const int a3 = s.Paste(clip)[0];
    const int b3 = s.Children(a3)[0];
    CHECK(LinksOf(s, a3).target.id == static_cast<uint64_t>(b3));
    CHECK(LinksOf(s, a3).followers[0].id == static_cast<uint64_t>(c));
    // クリップ自体は元の ID を保つ (ファイルと同じ形なので)。
    CHECK(clip.Get("entities").At(0).Get("components").At(0).Get("data").Get("target").AsInt() == b);
}

RM_TEST(EntityRefs_Prefabs) {
    int a = 0, b = 0, c = 0;
    Scene s = Make(&a, &b, &c);
    std::unordered_map<int, int> sources;
    const json::Value prefab = MakePrefab(s, a, &sources);
    // ファイルの中: B の ID がファイルにある。C は Prefab の外 - 消える。
    const json::Value& data = prefab.Get("entities").At(0).Get("components").At(0).Get("data");
    CHECK(data.Get("target").AsInt() == sources[b]);
    CHECK(data.Get("followers").At(0).AsInt() == 0);
    s.LinkPrefab(a, AssetRef{"Assets/A.rmprefab"}, sources);
    // 中の参照は、置いたものの変更ではない。C への参照は変更 (そのまま保たれる)。
    const json::Value ov = Overrides(s, a, prefab);
    CHECK(!HasOverride(ov, "target"));
    CHECK(HasOverride(ov, "followers"));

    // 置いた: 自分の B を指す。
    const int p = s.PlacePrefab(prefab, AssetRef{"Assets/A.rmprefab"});
    const int pb = s.Children(p)[0];
    CHECK(LinksOf(s, p).target.id == static_cast<uint64_t>(pb));
    CHECK(LinksOf(s, p).followers[0].id == 0);
    CHECK(!HasOverride(Overrides(s, p, prefab), "target"));
    // 置いたものは C を指す (自分の変更)。Prefab が変わる。更新すると、どちらも正しいまま。
    LinksOf(s, p).followers[0].id = static_cast<uint64_t>(c);
    const json::Value pov = Overrides(s, p, prefab);
    CHECK(HasOverride(pov, "followers"));
    s.Find(b)->data.name = "B2";
    const json::Value v2 = MakePrefab(s, a, &sources);
    CHECK(s.ApplyPrefab(p, v2, pov));
    CHECK(s.Find(pb) && s.Find(pb)->data.name == "B2");
    CHECK(LinksOf(s, p).target.id == static_cast<uint64_t>(pb));
    CHECK(LinksOf(s, p).followers[0].id == static_cast<uint64_t>(c));
}
