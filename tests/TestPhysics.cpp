// プラグイン 物理 (plugins/rm_engine/physics) のテスト: 部品から求める形 (Shapes - 純粋)、
// そして世界 (PhysicsWorld - Jolt): 落ちて止まる、トリガー、OnHit が 1 回、2D の物体が平面に
// とどまる、Play のコピーの世界を捨てる、部品の保存と読み込み。
// さらに、箱 1000 個で 1 ステップにかかる時間 (表示する: プラグインのレポートが出す数)。

#include "Test.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_production/runtime/sdk/RmAbi.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/PhysicsWorld.h"
#include "rm_engine/physics/runtime/Shapes.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace rm;
using namespace rm::physics;
using game_production::HookCall;
using game_production::Scene;

namespace {

bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

constexpr float kStep = 1.f / 60.f;
const Vec3 kGravity{0, -9.81f, 0};

const reflect::TypeDesc& T3() { return reflect::TypeOf<space3d::Transform>(); }
const reflect::TypeDesc& T2() { return reflect::TypeOf<space2d::Transform2D>(); }

Scene NewScene() {
    Scene s;
    s.SetComponentTypes({&T3(), &T2(), &reflect::TypeOf<Rigidbody>(), &reflect::TypeOf<Collider>(), &reflect::TypeOf<Collider2D>(),
                         &reflect::TypeOf<Character>()});
    return s;
}

template <class T>
T& Get(Scene& s, int id) {
    return *static_cast<T*>(s.Find(id)->FindComponent(reflect::TypeOf<T>().name)->value.Get());
}

template <class T>
T& Add(Scene& s, int id) {
    s.AddComponent(id, reflect::TypeOf<T>());
    return Get<T>(s, id);
}

// `at` にある 3D エンティティ。`size` のボックスコライダー付き (物体は、無し = 静的)。
int Box3(Scene& s, const char* name, Vec3 at, Vec3 size, std::optional<Motion> motion) {
    const int id = s.Create(name);
    Add<space3d::Transform>(s, id).position = at;
    Add<Collider>(s, id).size = size;
    if (motion) Add<Rigidbody>(s, id).motion = *motion;
    return id;
}

void Run(PhysicsWorld& w, Scene& s, int steps, std::vector<HookCall>* hooks = nullptr) {
    for (int i = 0; i < steps; ++i) w.Step(s, kStep, kGravity, hooks, nullptr);
}

int Count(const std::vector<HookCall>& hooks, int entity, int hook) {
    return static_cast<int>(std::count_if(hooks.begin(), hooks.end(), [&](const HookCall& h) { return h.entity == entity && h.hook == hook; }));
}

}  // namespace

// --- 形 (純粋) --------------------------------------------------------------------------------

RM_TEST(Physics_Shapes_SizesTakeTheScale) {
    Collider c;
    c.size = {2, 1, 4};
    c.offset = {0, 1, 0};
    ShapeDims d = DimsOf(c, {1, 3, 0.5f});
    CHECK(d.kind == ShapeDims::Kind::Box && Near(d.half.x, 1) && Near(d.half.y, 1.5f) && Near(d.half.z, 1));
    CHECK(Near(d.offset.y, 3));  // オフセットも拡大される
    c.shape = Shape::Sphere;
    c.radius = 0.5f;
    d = DimsOf(c, {1, 4, 2});
    CHECK(d.kind == ShapeDims::Kind::Sphere && Near(d.radius, 2));  // 一番大きい軸
    c.shape = Shape::Capsule;
    c.height = 2;
    d = DimsOf(c, {1, 1, 1});
    CHECK(d.kind == ShapeDims::Kind::Capsule && Near(d.radius, 0.5f) && Near(d.halfHeight, 0.5f));
    c.height = 0.5f;  // 両端より短い: 球
    CHECK(Near(DimsOf(c, {1, 1, 1}).halfHeight, 0));
    // 2D: どの 2D の物体も同じ厚さ。
    Collider2D c2;
    c2.size = {3, 1};
    d = DimsOf(c2, {2, 1, 1});
    CHECK(Near(d.half.x, 3) && Near(d.half.y, 0.5f) && Near(d.half.z, kPlaneHalfDepth));
    // キャラクターのカプセルは足で立つ。
    Character ch;
    ch.radius = 0.5f;
    ch.height = 2;
    d = DimsOf(ch);
    CHECK(Near(d.halfHeight, 0.5f) && Near(d.offset.y, 1));
}

RM_TEST(Physics_Shapes_PoseAndOutline) {
    space3d::Transform t;
    t.position = {1, 2, 3};
    t.rotation = {0, 90, 0};
    t.scale = {2, 2, 2};
    const Pose p = PoseOf(space3d::WorldMatrix(space3d::Mat4{}, t));
    CHECK(Near(p.position.x, 1) && Near(p.position.y, 2) && Near(p.position.z, 3));
    CHECK(Near(p.scale.x, 2) && Near(p.scale.y, 2) && Near(p.scale.z, 2));
    // 回転の列は長さ 1。
    CHECK(Near(std::sqrt(p.turn.m[0][0] * p.turn.m[0][0] + p.turn.m[0][1] * p.turn.m[0][1] + p.turn.m[0][2] * p.turn.m[0][2]), 1));
    // 箱: 辺 12 本。2D の箱: 4 本。
    std::vector<Segment> lines;
    ShapeDims box;
    Outline(box, p, false, lines);
    CHECK(lines.size() == 12);
    lines.clear();
    Outline(box, p, true, lines);
    CHECK(lines.size() == 4);
    // 2D: 角度が戻ってくる。
    space2d::Transform2D t2;
    t2.position = {5, 6};
    t2.rotation = 30;
    const Pose p2 = PoseOf(space2d::WorldMatrix(space2d::Mat3{}, t2));
    CHECK(Near(p2.position.x, 5) && Near(p2.position.y, 6) && Near(p2.position.z, 0));
    CHECK(Near(AngleOf(p2), 30, 1e-2f));
    // 親の下の場所: 動かした親の子も動く。Transform の無いものは親の場所にある。
    Scene s = NewScene();
    const int parent = s.Create("parent");
    Add<space3d::Transform>(s, parent).position = {10, 0, 0};
    const int middle = s.Create("middle", parent);
    const int child = s.Create("child", middle);
    Add<space3d::Transform>(s, child).position = {0, 1, 0};
    EntityPlace a, b, c;
    CHECK(PlaceUnder(*s.Find(parent), nullptr, a));
    CHECK(!PlaceUnder(*s.Find(middle), &a, b));
    CHECK(PlaceUnder(*s.Find(child), &b, c));
    CHECK(Near(c.world3.m[3][0], 10) && Near(c.world3.m[3][1], 1));
}

// --- 世界 (Jolt) -----------------------------------------------------------------------------

RM_TEST(Physics_World_FallsAndRestsOnTheFloor) {
    Scene s = NewScene();
    Box3(s, "floor", {0, -0.5f, 0}, {20, 1, 20}, std::nullopt);  // 静的: 上面は y = 0
    const int box = Box3(s, "box", {0, 5, 0}, {1, 1, 1}, Motion::Dynamic);
    PhysicsWorld w;
    Run(w, s, 30);  // 半秒: 落ちている
    const float midway = Get<space3d::Transform>(s, box).position.y;
    CHECK(midway < 5 && midway > 0.5f);
    Run(w, s, 240);  // さらに 4 秒: 床の上で止まる
    const Vec3 at = Get<space3d::Transform>(s, box).position;
    CHECK(Near(at.y, 0.5f, 0.03f) && Near(at.x, 0, 0.01f) && Near(at.z, 0, 0.01f));
    const auto v = w.Velocity(s, box);
    CHECK(v && std::fabs(v->y) < 0.05f);
    CHECK(w.Bodies() == 2);
    // 重力の倍率が 0: 浮く (新しい値で作り直した物体)。
    Get<space3d::Transform>(s, box).position = {0, 3, 0};
    Get<Rigidbody>(s, box).gravityScale = 0;
    w.SetVelocity(s, box, {0, 0, 0});
    Run(w, s, 60);
    CHECK(Near(Get<space3d::Transform>(s, box).position.y, 3, 0.01f));
}

RM_TEST(Physics_World_TriggerEnterAndExit) {
    Scene s = NewScene();
    const int zone = Box3(s, "zone", {0, 0, 0}, {2, 2, 2}, std::nullopt);
    Get<Collider>(s, zone).trigger = true;
    const int ball = Box3(s, "ball", {-5, 0, 0}, {0.5f, 0.5f, 0.5f}, Motion::Dynamic);
    Get<Rigidbody>(s, ball).gravityScale = 0;
    PhysicsWorld w;
    std::vector<HookCall> all, hooks;
    w.Step(s, kStep, kGravity, &hooks, nullptr);
    CHECK(w.SetVelocity(s, ball, {5, 0, 0}));  // 約 2 秒でゾーンを通り抜ける
    for (int i = 0; i < 180; ++i) {
        hooks.clear();
        w.Step(s, kStep, kGravity, &hooks, nullptr);
        all.insert(all.end(), hooks.begin(), hooks.end());
    }
    CHECK(Get<space3d::Transform>(s, ball).position.x > 3);  // 通り抜けた (止まらない)
    CHECK(Count(all, ball, RM_HOOK_TRIGGER_ENTER) == 1 && Count(all, ball, RM_HOOK_TRIGGER_EXIT) == 1);
    CHECK(Count(all, zone, RM_HOOK_TRIGGER_ENTER) == 1 && Count(all, zone, RM_HOOK_TRIGGER_EXIT) == 1);
    CHECK(Count(all, ball, RM_HOOK_HIT) == 0);
    // 出る前に入る。`other` はもう一方。
    const auto enter = std::find_if(all.begin(), all.end(), [&](const HookCall& h) { return h.entity == ball && h.hook == RM_HOOK_TRIGGER_ENTER; });
    const auto exit = std::find_if(all.begin(), all.end(), [&](const HookCall& h) { return h.entity == ball && h.hook == RM_HOOK_TRIGGER_EXIT; });
    CHECK(enter < exit && enter->other == zone);
}

RM_TEST(Physics_World_HitOnceWhileTouching) {
    Scene s = NewScene();
    const int wall = Box3(s, "wall", {0, 0, 0}, {1, 4, 4}, std::nullopt);
    const int bullet = Box3(s, "bullet", {-3, 0, 0}, {0.2f, 0.2f, 0.2f}, Motion::Dynamic);
    Get<Rigidbody>(s, bullet).gravityScale = 0;
    PhysicsWorld w;
    std::vector<HookCall> all, hooks;
    w.Step(s, kStep, kGravity, &hooks, nullptr);
    w.SetVelocity(s, bullet, {20, 0, 0});
    for (int i = 0; i < 120; ++i) {  // 当たって、壁にもたれて止まる (まだ触れている)
        hooks.clear();
        w.Step(s, kStep, kGravity, &hooks, nullptr);
        all.insert(all.end(), hooks.begin(), hooks.end());
        if (std::fabs(w.Velocity(s, bullet).value_or(Vec3{}).x) > 0.01f) continue;
        w.SetVelocity(s, bullet, {1, 0, 0});  // 押し続ける: 接触は 1 回のまま
    }
    CHECK(Count(all, bullet, RM_HOOK_HIT) == 1);
    CHECK(Count(all, wall, RM_HOOK_HIT) == 1);
    CHECK(Get<space3d::Transform>(s, bullet).position.x < -0.5f);  // 壁で止まった (抜けない)
}

RM_TEST(Physics_World_2DStaysOnThePlane) {
    Scene s = NewScene();
    const int ground = s.Create("ground");
    Add<space2d::Transform2D>(s, ground).position = {0, -1};
    Add<Collider2D>(s, ground).size = {20, 1};
    // 斜面の縁に落ちる箱: 着地で回る - Z まわりだけ。
    const int box = s.Create("box");
    auto& t = Add<space2d::Transform2D>(s, box);
    t.position = {0.4f, 3};
    t.rotation = 20;
    Add<Collider2D>(s, box);
    Add<Rigidbody>(s, box);
    PhysicsWorld w;
    for (int i = 0; i < 240; ++i) {
        w.Step(s, kStep, kGravity, nullptr, nullptr);
        if (i == 0) w.AddForce(s, box, {0, 0, 50}, true);  // 平面の外へ押す: 何も起きない
    }
    const auto v = w.Velocity(s, box);
    CHECK(v && Near(v->z, 0, 1e-4f));
    const auto& at = Get<space2d::Transform2D>(s, box);
    CHECK(at.position.y < 0 && at.position.y > -0.6f);  // 地面の上で止まる (上面は -0.5)
    // 倒れて平らになる (90 の倍数)。
    const float r = std::fmod(std::fabs(at.rotation), 90.f);
    CHECK(r < 1.f || r > 89.f);
}

RM_TEST(Physics_World_PlayCopyIsThrownAway) {
    // Play: コピーが動いて世界を持つ。元は値を保ち、世界は持たない。
    Scene original = NewScene();
    Box3(original, "floor", {0, -0.5f, 0}, {20, 1, 20}, std::nullopt);
    const int box = Box3(original, "box", {0, 5, 0}, {1, 1, 1}, Motion::Dynamic);
    {
        Scene playing = original;
        PhysicsWorld& w = WorldOf(playing);
        for (int i = 0; i < 60; ++i) w.Step(playing, kStep, kGravity, nullptr, nullptr);
        CHECK(Get<space3d::Transform>(playing, box).position.y < 5);
        CHECK(WorldOf(playing).Bodies() == 2);  // 毎回同じ世界
        Scene again = playing;  // コピー (例えば次の Play) は世界無しで始まる
        CHECK(WorldOf(again).Bodies() == 0);
    }  // ■: コピーとその世界は消える
    CHECK(Near(Get<space3d::Transform>(original, box).position.y, 5));
    CHECK(WorldOf(original).Bodies() == 0);
}

RM_TEST(Physics_World_SystemAndRaycast) {
    // システム 物理 は、シーンの重力で固定ステップで動く。
    Scene s = NewScene();
    Box3(s, "floor", {0, -0.5f, 0}, {20, 1, 20}, std::nullopt);
    const int box = Box3(s, "box", {0, 2, 0}, {1, 1, 1}, Motion::Dynamic);
    static const game_production::SystemInfo kPhysics{"物理", game_production::SystemPhase::Fixed, 0, &RunPhysics};
    const game_production::SystemInfo* list[] = {&kPhysics};
    game_production::SystemRunner r;
    r.SetSystems(list);
    // シーン自身の 重力 (シーンの設定): どのシーンにもあり、既定値を持つ。
    s.SetSettingTypes({&reflect::TypeOf<PhysicsSettings>()});
    CHECK(s.Setting<PhysicsSettings>() && Near(s.Setting<PhysicsSettings>()->gravity.y, -9.81f));
    // 重力の無い 1 秒: そのまま - そして眠る (半秒より長く止まっている)。
    s.Setting<PhysicsSettings>()->gravity = {0, 0, 0};
    for (int i = 0; i < 60; ++i) r.Advance(s, kStep, kStep);
    CHECK(Near(Get<space3d::Transform>(s, box).position.y, 2, 1e-3f));
    // また重力: 眠っていた物体が起きて落ちる (半秒で約 1.2 m)。
    s.Setting<PhysicsSettings>()->gravity = kGravity;
    for (int i = 0; i < 30; ++i) r.Advance(s, kStep, kStep);
    CHECK(Get<space3d::Transform>(s, box).position.y < 1.0f);
    // 上から下へのレイは箱に最初に当たる。トリガーには当たらない。
    const int zone = Box3(s, "zone", {0, 8, 0}, {4, 1, 4}, std::nullopt);
    Get<Collider>(s, zone).trigger = true;
    r.Advance(s, kStep, kStep);
    PhysicsWorld& w = WorldOf(s);
    const auto hit = w.Raycast({0, 10, 0}, {0, -1, 0}, 100);
    CHECK(hit && hit->entity == box && hit->normal.y > 0.9f);
    CHECK(!w.Raycast({0, 10, 0}, {0, 1, 0}, 100));
    // スクリプト (Transform) で動かした: 物体はそこへ行く。
    Get<space3d::Transform>(s, box).position = {5, 3, 0};
    r.Advance(s, kStep, kStep);
    CHECK(Near(Get<space3d::Transform>(s, box).position.x, 5, 1e-3f));
}

RM_TEST(Physics_World_CharacterWalksAndStands) {
    Scene s = NewScene();
    Box3(s, "floor", {0, -0.5f, 0}, {40, 1, 40}, std::nullopt);
    const int hero = s.Create("hero");
    Add<space3d::Transform>(s, hero).position = {0, 1, 0};
    Add<Character>(s, hero);
    PhysicsWorld w;
    Run(w, s, 60);
    CHECK(w.Characters() == 1 && w.Grounded(hero));
    CHECK(Near(Get<space3d::Transform>(s, hero).position.y, 0, 0.05f));
    CHECK(w.Move(s, hero, {2, 0, 0}));
    Run(w, s, 60);  // 2 m/s で 1 秒
    CHECK(Near(Get<space3d::Transform>(s, hero).position.x, 2, 0.2f));
    CHECK(w.Jump(s, hero, 5));
    Run(w, s, 10);
    CHECK(!w.Grounded(hero) && Get<space3d::Transform>(s, hero).position.y > 0.3f);
}

RM_TEST(Physics_Components_SaveAndLoad) {
    Scene s = NewScene();
    const int id = Box3(s, "box", {1, 2, 3}, {2, 3, 4}, Motion::Kinematic);
    auto& rb = Get<Rigidbody>(s, id);
    rb.mass = 7;
    rb.friction = 0.25f;
    rb.lockRotationY = true;
    auto& c = Get<Collider>(s, id);
    c.shape = Shape::Capsule;
    c.trigger = true;
    c.offset = {0, 1, 0};
    const json::Value saved = s.Save();
    Scene back = NewScene();
    back.Load(saved);
    const auto& rb2 = Get<Rigidbody>(back, id);
    const auto& c2 = Get<Collider>(back, id);
    CHECK(rb2.motion == Motion::Kinematic && Near(rb2.mass, 7) && Near(rb2.friction, 0.25f) && rb2.lockRotationY && !rb2.lockRotationX);
    CHECK(c2.shape == Shape::Capsule && c2.trigger && Near(c2.offset.y, 1) && Near(c2.size.z, 4));
}

RM_TEST(Physics_Speed_1000Boxes) {
    // 1000 個の箱が床に山になって落ちる: 1 ステップにかかる時間 (表示するだけ - 合否ではない)。
    Scene s = NewScene();
    Box3(s, "floor", {0, -0.5f, 0}, {100, 1, 100}, std::nullopt);
    for (int i = 0; i < 1000; ++i) {
        const float x = static_cast<float>(i % 10) * 1.5f - 7, z = static_cast<float>((i / 10) % 10) * 1.5f - 7;
        Box3(s, "box", {x, 1 + static_cast<float>(i / 100) * 1.5f, z}, {1, 1, 1}, Motion::Dynamic);
    }
    PhysicsWorld w;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    w.Step(s, kStep, kGravity, nullptr, nullptr);  // 最初: すべての物体を作る
    const auto t1 = Clock::now();
    constexpr int kSteps = 120;
    double worst = 0;
    for (int i = 0; i < kSteps; ++i) {
        const auto a = Clock::now();
        w.Step(s, kStep, kGravity, nullptr, nullptr);
        worst = std::max(worst, std::chrono::duration<double, std::milli>(Clock::now() - a).count());
    }
    const double total = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();
    std::printf("  physics: 1000 boxes - first step (bodies made) %.2f ms, then %.3f ms a step on average (worst %.3f ms) over %d steps\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(), total / kSteps, worst, kSteps);
    CHECK(w.Bodies() == 1001);
}
