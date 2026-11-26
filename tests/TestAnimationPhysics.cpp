// つなぎ アニメーション × 物理 (plugins/rm_engine/animation_x_physics) のテスト: ルートモーション (docs/ANIMATION.md 7-1) で
// 物理のキャラクターが歩く。1 秒で歩くクリップの距離 (1 フレーム遅れるので少し短い)・止まったら速さ 0・回転は
// Transform のもの (アニメーションが書く)。

#include "Test.h"

#include "core/RuntimeRegistry.h"
#include "rm_engine/animation_x_physics/runtime/Register.h"
#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_x_animation/runtime/AnimationSystem.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/PhysicsWorld.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace rm;
using game_production::Scene;

namespace {

constexpr float kStep = 1.f / 60.f;

template <class T>
T& Get(Scene& s, int id) {
    return *static_cast<T*>(s.Find(id)->FindComponent(reflect::TypeOf<T>().name)->value.Get());
}
template <class T>
T& Add(Scene& s, int id) {
    s.AddComponent(id, reflect::TypeOf<T>());
    return Get<T>(s, id);
}

// 根 Root が 1 秒で Z へ `meters` 進み、Y のまわりに `degrees` 回るクリップ「Walk」と、動かない「Still」。
game_x_3d::SkinnedModel Walker(float meters, float degrees) {
    auto skin = std::make_shared<space3d::SkinData>();
    space3d::Bone root;
    root.name = "Root";
    skin->skeleton.bones = {root};
    skin->inverseBind = {space3d::Mat4{}};
    auto clips = std::make_shared<std::vector<space3d::Clip>>();
    space3d::Clip walk;
    walk.name = "Walk";
    walk.duration = 1;
    walk.rate = 10;
    walk.frames = space3d::ClipFrames(1, 10);
    space3d::BoneTrack t;
    t.bone = "Root";
    for (uint32_t i = 0; i < walk.frames; ++i) {
        const float f = static_cast<float>(i) / static_cast<float>(walk.frames - 1);
        t.t.push_back({0, 0, meters * f});
        t.r.push_back(space3d::QuatAxisAngle({0, 1, 0}, degrees * f * 3.14159265f / 180.f));
    }
    walk.tracks.push_back(t);
    walk.root.enabled = true;
    clips->push_back(walk);
    space3d::Clip still = walk;
    still.name = "Still";
    still.tracks.clear();
    still.root.enabled = false;
    clips->push_back(still);
    return {skin, clips};
}

struct World {
    Scene scene;
    int hero = 0;
    game_production::SystemRunner runner;
    RuntimeRegistry registry;
    game_x_3d::SkinnedModel model;
};

void Setup(World& w, float meters, float degrees) {
    Scene& s = w.scene;
    s.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<physics::Collider>(), &reflect::TypeOf<physics::Character>(),
                         &reflect::TypeOf<game_x_3d::MeshRenderer>(), &reflect::TypeOf<game_x_animation::Animator>()});
    s.SetSettingTypes({&reflect::TypeOf<physics::PhysicsSettings>()});
    const int floor = s.Create("floor");
    Add<space3d::Transform>(s, floor).position = {0, -0.5f, 0};
    Add<physics::Collider>(s, floor).size = {200, 1, 200};
    w.hero = s.Create("hero");
    Add<space3d::Transform>(s, w.hero).position = {0, 0.05f, 0};
    Add<physics::Character>(s, w.hero);
    auto& mr = Add<game_x_3d::MeshRenderer>(s, w.hero);
    mr.shape = game_x_3d::MeshShape::Model;
    mr.model.guid = "c0c0c0c0d1d1d1d1e2e2e2e2f3f3f3f3";
    auto& an = Add<game_x_animation::Animator>(s, w.hero);
    an.clip = "Walk";
    w.model = Walker(meters, degrees);
    game_x_animation::AnimatorRun& run = game_x_animation::RunOf(an, w.hero);
    run.model = mr.model.guid;  // (読んだものとしてつなぐ)
    game_x_animation::BindModel(run, w.model.skin, w.model.clips);

    static const game_production::SystemInfo kPhysics{"物理", game_production::SystemPhase::Fixed, 0, &physics::RunPhysics};
    static const game_production::SystemInfo kAnim{"アニメーション", game_production::SystemPhase::Frame, 100, &game_x_animation::RunAnimation};
    static const game_production::SystemInfo* list[] = {&kPhysics, &kAnim};
    w.runner.SetSystems(list);
    animation_x_physics::RegisterRuntime(w.registry);
    w.runner.SetRegistry(&w.registry);
}

}  // namespace

RM_TEST(AnimPhysics_CharacterWalksTheClipDistance) {
    World w;
    Setup(w, 1.f, 0.f);
    auto& an = Get<game_x_animation::Animator>(w.scene, w.hero);
    auto& at = Get<space3d::Transform>(w.scene, w.hero);
    for (int i = 0; i < 30; ++i) w.runner.Advance(w.scene, kStep, kStep);  // (立つまで。その間も歩く)
    const float z0 = at.position.z;
    for (int i = 0; i < 60; ++i) w.runner.Advance(w.scene, kStep, kStep);
    // 1 m/s で 1 秒: クリップどおりの距離 (2% の中)
    CHECK(std::fabs((at.position.z - z0) - 1.f) < 0.02f);
    CHECK(std::fabs(at.position.x) < 1e-3f);
    // 止まったら速さ 0 (滑り続けない)
    an.clip = "Still";
    for (int i = 0; i < 3; ++i) w.runner.Advance(w.scene, kStep, kStep);  // (1 フレーム遅れて止まる)
    const float z1 = at.position.z;
    for (int i = 0; i < 30; ++i) w.runner.Advance(w.scene, kStep, kStep);
    CHECK(std::fabs(at.position.z - z1) < 1e-3f);
    CHECK(physics::FindWorldOf(w.scene) && physics::FindWorldOf(w.scene)->Grounded(w.hero));
}

RM_TEST(AnimPhysics_TurnIsTheTransforms) {
    World w;
    Setup(w, 0.f, 90.f);  // その場で 1 秒に 90 度回る
    auto& at = Get<space3d::Transform>(w.scene, w.hero);
    for (int i = 0; i < 30; ++i) w.runner.Advance(w.scene, kStep, kStep);
    const Vec3 p = at.position;
    // 回転はアニメーションが Transform に書く (キャラクターは回転を持たないので、書き戻しで消えない)。0.5 秒で 45 度
    CHECK(std::fabs(std::fabs(at.rotation.y) - 45.f) < 1.f);
    CHECK(std::fabs(at.position.x - p.x) < 1e-3f && std::fabs(at.position.z - p.z) < 1e-3f);
}
