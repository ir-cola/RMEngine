// ゲーム制作 × アニメーション (plugins/rm_engine/game_x_animation/runtime) のテスト: クリップを名前で探す・
// 何を見せるか (流す / 最初のコマ / プレビュー / バインドの形)・プレビューの時計・姿勢の版。それと、
// アニメーターの姿勢をゲーム制作 × 3D の口 (MeshPose.h の ApplyPoses) で MeshDraw に入れるところ (GPU なし)。

#include "Test.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"
#include "rm_engine/game_x_animation/runtime/AnimatorPoser.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace rm;
using namespace rm::game_x_animation;
using game_production::Scene;

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }

space3d::Clip NamedClip(std::string name, float duration) {
    space3d::Clip c;
    c.name = std::move(name);
    c.duration = duration;
    c.rate = 10;
    c.frames = space3d::ClipFrames(duration, c.rate);
    return c;
}

uint64_t g_next = 0;
uint64_t NextForTest() { return ++g_next; }

bool IsIdentity(const space3d::BoneMatrix& m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!Near(m.row[r][c], r == c ? 1.f : 0.f)) return false;
        }
    }
    return true;
}

}  // namespace

RM_TEST(GameAnim_FindClipByName) {
    const std::vector<space3d::Clip> clips{NamedClip("Wave", 2), NamedClip("Idle", 4), NamedClip("Wave", 1), NamedClip("手を振る", 1)};
    CHECK(FindClip(clips, "Idle") == 1);
    CHECK(FindClip(clips, "Wave") == 0);       // 同じ名前は最初のもの
    CHECK(FindClip(clips, "手を振る") == 3);    // 日本語の名前
    CHECK(FindClip(clips, "") == 0);           // 空: 最初のクリップ
    CHECK(FindClip(clips, "wave") == -1);      // 大文字と小文字は別
    CHECK(FindClip({}, "") == -1);             // クリップが無い
    CHECK(FindClip({}, "Wave") == -1);
}

RM_TEST(GameAnim_WhatToShow) {
    // ゲーム: 始めから流す なら流す、外したら最初のコマ。プレビューは関係ない
    CHECK(WhatToShow(true, true, false, true) == Showing::Playing);
    CHECK(WhatToShow(true, true, true, true) == Showing::Playing);
    CHECK(WhatToShow(true, false, false, true) == Showing::FirstFrame);
    // 止まっているエディター: 選んだもの (プレビュー) だけが動く。始めから流す を外しても見られる
    CHECK(WhatToShow(false, true, true, true) == Showing::Preview);
    CHECK(WhatToShow(false, false, true, true) == Showing::Preview);
    CHECK(WhatToShow(false, true, false, true) == Showing::FirstFrame);
    // クリップが無い: どこでもバインドの形
    CHECK(WhatToShow(true, true, false, false) == Showing::BindPose);
    CHECK(WhatToShow(false, true, true, false) == Showing::BindPose);
}

RM_TEST(GameAnim_PreviewClock) {
    PreviewClock clock;
    FollowSelection(clock, 5);
    CHECK(clock.entity == 5 && !clock.paused);
    // 最初のフレームは始めのコマ (進めない)
    AdvancePreview(clock, 0.5f, 1, 2, true);
    CHECK(Near(clock.time, 0));
    AdvancePreview(clock, 0.5f, 1, 2, true);
    CHECK(Near(clock.time, 0.5f));
    // ループ: 回る
    AdvancePreview(clock, 1.75f, 1, 2, true);
    CHECK(Near(clock.time, 0.25f));
    // 一時停止: 進まない
    TogglePreview(clock, 1, 2, true);
    CHECK(clock.paused);
    AdvancePreview(clock, 1, 1, 2, true);
    CHECK(Near(clock.time, 0.25f));
    // つまみ: そこに置いて止める (範囲の外は端)
    TogglePreview(clock, 1, 2, true);
    SeekPreview(clock, 1.5f, 2);
    CHECK(Near(clock.time, 1.5f) && clock.paused);
    SeekPreview(clock, 9, 2);
    CHECK(Near(clock.time, 2));
    // 同じものを選び続けても、時計はそのまま
    FollowSelection(clock, 5);
    CHECK(Near(clock.time, 2) && clock.paused);
    // 別のものを選ぶ: 始めから流す
    FollowSelection(clock, 7);
    CHECK(clock.entity == 7 && !clock.paused && clock.fresh);

    // ループでない: 終わりで止まり、▶ で始めから
    PreviewClock once;
    FollowSelection(once, 1);
    AdvancePreview(once, 0, 1, 1, false);
    AdvancePreview(once, 0.6f, 1, 1, false);
    CHECK(Near(once.time, 0.6f) && !once.paused);
    AdvancePreview(once, 0.6f, 1, 1, false);
    CHECK(Near(once.time, 1) && once.paused);
    TogglePreview(once, 1, 1, false);
    CHECK(Near(once.time, 0) && !once.paused);
    // 逆に流すループでないもの: 終わりから始めて、0 で止まる
    PreviewClock back;
    FollowSelection(back, 1);
    AdvancePreview(back, 0.1f, -1, 1, false);
    CHECK(Near(back.time, 1));
    AdvancePreview(back, 2, -1, 1, false);
    CHECK(Near(back.time, 0) && back.paused);
    CHECK(Near(StartTime(-1, 1, false), 1) && Near(StartTime(-1, 1, true), 0) && Near(StartTime(1, 1, false), 0));
}

RM_TEST(GameAnim_PoseStampChangesOnlyWithThePose) {
    g_next = 0;
    PoseStamp s;
    int model = 0;
    CHECK(s.Version() == 0);
    CHECK(s.Update(&model, 0, 0.f, true, &NextForTest));  // 初めて: 新しい番号
    const uint64_t first = s.Version();
    CHECK(first != 0);
    CHECK(!s.Update(&model, 0, 0.f, true, &NextForTest));  // 同じ姿勢 (止まっている): 同じ番号 = 影を使い回す
    CHECK(s.Version() == first);
    CHECK(s.Update(&model, 0, 0.1f, true, &NextForTest));  // 時刻が動いた
    const uint64_t second = s.Version();
    CHECK(second != first);
    CHECK(s.Update(&model, 1, 0.1f, true, &NextForTest));  // クリップが変わった
    CHECK(s.Update(&model, 1, 0.1f, false, &NextForTest));  // ループが変わった (終わりのコマが違う)
    int other = 0;
    CHECK(s.Update(&other, 1, 0.1f, false, &NextForTest));  // モデルを読み直した
    s.Reset();
    CHECK(s.Update(&other, 1, 0.1f, false, &NextForTest));  // 忘れた後は必ず新しい
}

namespace {

// 骨 2 本 (根と、その上の腕) と、腕を Y まわりに 0 → 90 度回すクリップ "Turn" (1 秒)。
game_x_3d::SkinnedModel TurningArm() {
    auto skin = std::make_shared<space3d::SkinData>();
    space3d::Bone root;
    root.name = "Root";
    space3d::Bone arm;
    arm.name = "Arm";
    arm.parent = 0;
    arm.rest.t = {0, 1, 0};
    skin->skeleton.bones = {root, arm};
    skin->inverseBind = {space3d::Mat4{}, space3d::Inverse(space3d::TRSMatrix(arm.rest)).value_or(space3d::Mat4{})};
    auto clips = std::make_shared<std::vector<space3d::Clip>>();
    space3d::Clip turn = NamedClip("Turn", 1);
    space3d::BoneTrack t;
    t.bone = "Arm";
    for (uint32_t i = 0; i < turn.frames; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(turn.frames - 1) * 1.5707963f;
        t.r.push_back(space3d::QuatAxisAngle({0, 1, 0}, a));
    }
    turn.tracks.push_back(t);
    clips->push_back(turn);
    return {skin, clips};
}

}  // namespace

RM_TEST(GameAnim_PoserFillsTheDrawWithTheClip) {
    const reflect::TypeDesc& animator = reflect::TypeOf<Animator>();
    Scene scene;
    scene.SetComponentTypes({&animator});
    const int e = scene.Create("arm");
    const int plain = scene.Create("no animator");
    CHECK(scene.AddComponent(e, animator));
    const game_x_3d::SkinnedModel model = TurningArm();
    space3d::GpuMesh gpu;
    gpu.skinned = true;
    gpu.boneCount = 2;

    std::vector<std::unique_ptr<game_x_3d::MeshPoser>> posers;
    posers.push_back(CreateAnimatorPoser());
    const game_x_3d::PoseOwner owners[] = {{e, &model}, {plain, &model}};
    std::vector<space3d::MeshDraw> draws(2);
    draws[0].mesh = &gpu;
    draws[1].mesh = &gpu;

    // ゲーム: 1 フレーム目は始めのコマ (腕は休みの姿勢 = スキンの行列は単位行列)、それから時間で流れる
    game_x_3d::MeshPoseFrame play{scene, 0.25f, true};
    CHECK(game_x_3d::ApplyPoses(posers, play, owners, draws));
    CHECK(draws[0].bones.size() == 2);
    CHECK(IsIdentity(draws[0].bones[1]));
    CHECK(draws[0].poseVersion != 0);
    CHECK(draws[1].bones.empty());  // アニメーターが無い: 答えない (休みの姿勢のまま)
    const uint64_t v0 = draws[0].poseVersion;
    draws = std::vector<space3d::MeshDraw>(2, space3d::MeshDraw{&gpu});
    game_x_3d::ApplyPoses(posers, play, owners, draws);
    CHECK(draws[0].bones.size() == 2 && !IsIdentity(draws[0].bones[1]));  // 0.25 秒: 腕が回った
    CHECK(draws[0].poseVersion != v0 && draws[0].moving);

    // 止まっているエディター、選んでいない: 最初のコマで止まる (版は変わらない = 影を使い回す)
    game_x_3d::MeshPoseFrame still{scene, 0.25f, false};
    still.picture = "scene";
    draws = std::vector<space3d::MeshDraw>(2, space3d::MeshDraw{&gpu});
    game_x_3d::ApplyPoses(posers, still, owners, draws);
    CHECK(IsIdentity(draws[0].bones[1]) && !draws[0].moving);
    const uint64_t stillVersion = draws[0].poseVersion;
    draws = std::vector<space3d::MeshDraw>(2, space3d::MeshDraw{&gpu});
    CHECK(!game_x_3d::ApplyPoses(posers, still, owners, draws));
    CHECK(draws[0].poseVersion == stillVersion);

    // 選ぶと、シーンビューの時計で流れる (時計はクリップの名前と長さを知る)
    const int selection[] = {e};
    still.selection = selection;
    draws = std::vector<space3d::MeshDraw>(2, space3d::MeshDraw{&gpu});
    game_x_3d::ApplyPoses(posers, still, owners, draws);  // (始めのコマ)
    CHECK(EditorPreview().entity == e && EditorPreview().hasClip && EditorPreview().clip == "Turn" && Near(EditorPreview().duration, 1));
    draws = std::vector<space3d::MeshDraw>(2, space3d::MeshDraw{&gpu});
    CHECK(game_x_3d::ApplyPoses(posers, still, owners, draws));
    CHECK(Near(EditorPreview().time, 0.25f) && !IsIdentity(draws[0].bones[1]));

    // 無いクリップの名前: バインドの形 (答えない)
    static_cast<Animator*>(scene.Find(e)->FindComponent(animator.name)->value.Get())->clip = "Jump";
    draws = std::vector<space3d::MeshDraw>(2, space3d::MeshDraw{&gpu});
    game_x_3d::ApplyPoses(posers, play, owners, draws);
    CHECK(draws[0].bones.empty());
    FollowSelection(EditorPreview(), 0);
}
