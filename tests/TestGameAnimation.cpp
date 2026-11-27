// ゲーム制作 × アニメーション (plugins/rm_engine/game_x_animation/runtime) のテスト: クリップを名前で探す・
// 何を見せるか (流す / 最初のコマ / プレビュー / バインドの形)・プレビューの時計・姿勢の版。それと、
// アニメーターの姿勢をゲーム制作 × 3D の口 (MeshPose.h の ApplyPoses) で MeshDraw に入れるところ (GPU なし)。

#include "Test.h"

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/game_x_3d/runtime/MeshPose.h"
#include "rm_engine/game_x_animation/runtime/Animator.h"
#include "rm_engine/game_x_animation/runtime/AnimatorLogic.h"
#include "rm_engine/game_x_animation/runtime/AnimatorPoser.h"
#include "rm_engine/game_x_animation/runtime/AnimatorRun.h"
#include "rm_engine/game_x_animation/runtime/AnimationSystem.h"
#include "rm_engine/game_x_animation/runtime/AnimatorIk.h"
#include "rm_engine/game_x_animation/runtime/RootMotionSink.h"
#include "rm_engine/animation3d/runtime/RootMotion.h"
#include "rm_engine/space3d/runtime/View.h"
#include "core/RuntimeRegistry.h"
#include "rm_engine/game_x_3d/runtime/MeshRenderer.h"
#include "rm_engine/game_production/runtime/Scripts.h"
#include "rm_engine/game_production/runtime/Systems.h"
#include "core/asset/ImportSettings.h"
#include "base/Json.h"
#include "rm_engine/animation/runtime/Rules.h"
#include "rm_engine/animation3d/runtime/Pose.h"

#include <cmath>
#include <filesystem>
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

// --- 段階 2: ゲームの間の再生 (AnimatorRun)・クリップのイベント・骨に付ける・システム ----------------

namespace {

// TurningArm に、何もしないクリップ "Still" (1 秒) を足したもの。
game_x_3d::SkinnedModel ArmWithTwoClips() {
    game_x_3d::SkinnedModel m = TurningArm();
    auto clips = std::make_shared<std::vector<space3d::Clip>>(*m.clips);
    clips->push_back(NamedClip("Still", 1));
    return {m.skin, clips};
}

}  // namespace

RM_TEST(GameAnim_RunFiresClipEvents) {
    const game_x_3d::SkinnedModel m = TurningArm();
    auto clips = std::make_shared<std::vector<space3d::Clip>>(*m.clips);
    (*clips)[0].events = {{0.f, "Start", 1, ""}, {0.5f, "Half", 2, "半分"}};
    Animator a;
    AnimatorRun run;
    BindModel(run, m.skin, clips);
    std::vector<const space3d::ClipEvent*> fired;
    StepAnimator(run, a, 0.1f, fired, &NextForTest);
    CHECK(fired.size() == 1 && fired[0]->name == "Start");  // 流し始めは、始めの時刻ちょうどのものも
    CHECK(Near(run.player.Time(), 0.1f));
    CHECK(run.bones.size() == 2 && run.version != 0 && run.moving);
    fired.clear();
    StepAnimator(run, a, 0.5f, fired, &NextForTest);  // 0.1 → 0.6: 0.5 を通る
    CHECK(fired.size() == 1 && fired[0]->name == "Half" && fired[0]->number == 2 && fired[0]->text == "半分");
    fired.clear();
    StepAnimator(run, a, 0.5f, fired, &NextForTest);  // 0.6 → 0.1 (ループ): 継ぎ目の 0 を通る
    CHECK(fired.size() == 1 && fired[0]->name == "Start");
    // 止めている間は進まず、イベントも出ない (骨の行列も作り直さない)
    run.paused = true;
    const uint64_t v = run.version;
    fired.clear();
    StepAnimator(run, a, 0.5f, fired, &NextForTest);
    CHECK(fired.empty() && Near(run.player.Time(), 0.1f) && run.version == v && !run.moving);
}

RM_TEST(GameAnim_RunCrossfadesAndSwitches) {
    const game_x_3d::SkinnedModel m = ArmWithTwoClips();
    Animator a;
    a.clip = "Turn";
    AnimatorRun run;
    BindModel(run, m.skin, m.clips);
    std::vector<const space3d::ClipEvent*> fired;
    StepAnimator(run, a, 0.5f, fired, &NextForTest);
    CHECK(run.player.ClipIndex() == 0 && Near(run.player.Time(), 0.5f));
    // スクリプトの PlayClip: 欄も書き換え、0.4 秒かけて移る
    CHECK(PlayClip(run, a, "Still", 0.4f));
    CHECK(a.clip == "Still" && run.player.ClipIndex() == 1 && Near(run.player.FadeWeight(), 0.f));
    StepAnimator(run, a, 0.2f, fired, &NextForTest);
    CHECK(Near(run.player.FadeWeight(), 0.5f) && run.moving);
    StepAnimator(run, a, 0.3f, fired, &NextForTest);
    CHECK(Near(run.player.FadeWeight(), 1.f));
    // 無いクリップ: false で、休みの姿勢
    CHECK(!PlayClip(run, a, "Nope", 0));
    CHECK(a.clip == "Nope" && run.player.ClipIndex() == -1);
    // インスペクターで欄を変えた: 次のフレームでそのクリップを始めから
    a.clip = "Turn";
    StepAnimator(run, a, 0.25f, fired, &NextForTest);
    CHECK(run.player.ClipIndex() == 0 && Near(run.player.Time(), 0.25f));
    // 長さと、ループでないクリップの終わり
    CHECK(Near(Duration(run), 1.f) && !Ended(run, a));
    a.loop = false;
    StepAnimator(run, a, 2.f, fired, &NextForTest);
    CHECK(Ended(run, a) && Near(run.player.Time(), 1.f));
}

// 状態とルール (.rmanim): 1 つ目の状態から始まり、パラメーターでルールが状態を変え、そのクリップ・ループ・速さで流す。
RM_TEST(GameAnim_RulesDriveTheClip) {
    const game_x_3d::SkinnedModel m = ArmWithTwoClips();
    animation::AnimController ctl;
    ctl.params = {{"speed", animation::ParamType::Float, 0}, {"wave", animation::ParamType::Trigger, 0}};
    ctl.states = {{"Idle", "Still", true, 1, animation::StateExit::Free},
                  {"Move", "Turn", true, 2, animation::StateExit::Free},
                  {"Wave", "Turn", false, 1, animation::StateExit::OwnUntilEnd}};
    ctl.rules = {{"", "wave", "Wave", 0.f, false, false},
                 {"Wave", "ended", "Idle", 0.f, false, false},
                 {"", "speed > 0.1", "Move", 0.4f, false, false},
                 {"", "", "Idle", 0.2f, false, false}};
    Animator a;
    AnimatorRun run;
    BindModel(run, m.skin, m.clips);
    // スクリプトがルールを読む前に入れた値は、読んだときに入る。
    CHECK(SetRuleParam(run, "speed", 0.f, false));
    BindRules(run, a, std::make_shared<const animation::CompiledController>(animation::Compile(ctl)));
    CHECK(StateName(run) == "Idle" && a.clip == "Still" && a.loop);
    std::vector<const space3d::ClipEvent*> fired;
    StepAnimatorRules(run, a, 0.1f);
    StepAnimator(run, a, 0.1f, fired, &NextForTest);
    CHECK(StateName(run) == "Idle" && run.player.ClipIndex() == 1);
    // 速さ → Move (0.4 秒のフェード)。状態の速さ 2 が欄に入る。
    CHECK(SetRuleParam(run, "speed", 3.f, false) && RuleParam(run, "speed") == 3.f);
    StepAnimatorRules(run, a, 0.1f);
    CHECK(StateName(run) == "Move" && a.clip == "Turn" && a.speed == 2.f && run.rule == 2);
    StepAnimator(run, a, 0.1f, fired, &NextForTest);
    CHECK(run.player.ClipIndex() == 0 && run.player.FadeWeight() < 1.f);
    CHECK(run.checks.size() == 4 && run.checks[2] == animation::RuleCheck::Chosen);
    // トリガー → Wave (1 回)。終わるまで自分のルールだけ: speed があっても Move に戻らない。終わったら Idle。
    CHECK(SetRuleParam(run, "wave", 1.f, true));
    StepAnimatorRules(run, a, 0.1f);
    CHECK(StateName(run) == "Wave" && !a.loop && RuleParam(run, "wave") == 0.f);
    StepAnimator(run, a, 0.5f, fired, &NextForTest);
    StepAnimatorRules(run, a, 0.5f);
    CHECK(StateName(run) == "Wave" && StateFraction(run) > 0.4f && StateFraction(run) < 0.6f);
    StepAnimator(run, a, 1.f, fired, &NextForTest);
    StepAnimatorRules(run, a, 0.016f);
    CHECK(StateName(run) == "Idle");
    StepAnimatorRules(run, a, 0.016f);
    CHECK(StateName(run) == "Move");
    // スクリプトの Play: ルールを飛ばしてその状態へ。無い名前は false。
    CHECK(PlayState(run, a, "Wave", 0.1f) && StateName(run) == "Wave" && !PlayState(run, a, "Fly", 0.f));
    CHECK(!SetRuleParam(run, "nothing", 1.f, false));
    // 外す: 状態は無くなり、欄のクリップのまま。
    BindRules(run, a, nullptr);
    CHECK(StateName(run).empty() && !run.rules);
}

namespace {

// 腕の骨 (1 番) の Y まわりの角度 (度)。
float ArmDegrees(const AnimatorRun& run) {
    const animation3d::Pose& pose = run.player.CurrentPose();
    if (pose.size() < 2) return -999.f;
    const space3d::Quat q = pose[1].r;
    return 2.f * std::atan2(q.y, q.w) * 57.29578f;
}

}  // namespace

// 混ぜる (1D): パラメーターで Still と Turn を混ぜる。重ねる: 腕だけのレイヤー (マスク) を重みで重ねる・足す。
RM_TEST(GameAnim_BlendAndLayers) {
    // 骨の木と、そのマスク・上書き・足す (Pose.h)。
    {
        const game_x_3d::SkinnedModel m = ArmWithTwoClips();
        const auto& sk = m.skin->skeleton;
        const std::vector<std::string> arm{"Arm"};
        CHECK(animation3d::BoneMask(sk, arm) == std::vector<float>({0.f, 1.f}));
        CHECK(animation3d::BoneMask(sk, {}) == std::vector<float>({1.f, 1.f}));
        animation3d::Pose base = animation3d::RestPose(sk), layer = base;
        layer[0].t = {5, 0, 0};
        layer[1].r = space3d::QuatAxisAngle({0, 1, 0}, 1.5707963f);
        animation3d::Pose over = base;
        animation3d::OverlayPose(over, layer, 0.5f, animation3d::BoneMask(sk, arm));
        CHECK(Near(over[0].t.x, 0.f) && Near(2.f * std::atan2(over[1].r.y, over[1].r.w) * 57.29578f, 45.f));
        animation3d::Pose add = base;
        add[1].r = space3d::QuatAxisAngle({0, 1, 0}, 0.5235988f);  // (30 度から)
        animation3d::AddPose(add, layer, animation3d::RestPose(sk), 1.f, {});
        CHECK(Near(add[0].t.x, 5.f) && Near(2.f * std::atan2(add[1].r.y, add[1].r.w) * 57.29578f, 120.f));
    }
    const game_x_3d::SkinnedModel m = ArmWithTwoClips();
    animation::AnimController ctl;
    ctl.params = {{"speed", animation::ParamType::Float, 0}, {"wave", animation::ParamType::Float, 0}};
    animation::AnimState move{"Move", "", true, 1, animation::StateExit::Free};
    move.kind = animation::StateKind::Blend1D;
    move.paramX = "speed";
    move.motions = {{"Still", 0, 0}, {"Turn", 1, 0}};
    ctl.states = {move};
    Animator a;
    AnimatorRun run;
    BindModel(run, m.skin, m.clips);
    BindRules(run, a, std::make_shared<const animation::CompiledController>(animation::Compile(ctl)));
    std::vector<const space3d::ClipEvent*> fired;
    // speed 0: Still だけ (休みの姿勢)。
    StepAnimatorRules(run, a, 0.f);
    StepAnimator(run, a, 0.5f, fired, &NextForTest);
    CHECK(run.blend.size() == 2 && Near(run.blend[0], 1.f) && Near(ArmDegrees(run), 0.f));
    // speed 1: Turn だけ。足をそろえる時計は Still (1 秒) の割合: 0.5 秒進めると Turn の 0.5 → 45 度。
    SetRuleParam(run, "speed", 1.f, false);
    StepAnimatorRules(run, a, 0.f);
    StepAnimator(run, a, 0.f, fired, &NextForTest);
    CHECK(Near(run.blend[1], 1.f) && Near(ArmDegrees(run), 45.f));
    // speed 0.5: 半分ずつ → 22.5 度。
    SetRuleParam(run, "speed", 0.5f, false);
    StepAnimatorRules(run, a, 0.f);
    StepAnimator(run, a, 0.f, fired, &NextForTest);
    CHECK(Near(run.blend[0], 0.5f) && Near(ArmDegrees(run), 22.5f));

    // 重ねる: 体は Still、腕だけ (マスク Arm) のレイヤーで Turn。重みはパラメーター wave。
    animation::AnimController lay;
    lay.params = ctl.params;
    lay.states = {{"Idle", "Still", true, 1, animation::StateExit::Free}};
    animation::AnimLayer arm;
    arm.name = "腕";
    arm.weightParam = "wave";
    arm.mask = {"Arm"};
    arm.states = {{"Wave", "Turn", true, 1, animation::StateExit::Free}};
    lay.layers = {arm};
    Animator b;
    AnimatorRun lr;
    BindModel(lr, m.skin, m.clips);
    BindRules(lr, b, std::make_shared<const animation::CompiledController>(animation::Compile(lay)));
    CHECK(lr.layers.size() == 1 && lr.layers[0].mask == std::vector<float>({0.f, 1.f}));
    StepAnimatorRules(lr, b, 0.f);
    StepAnimator(lr, b, 0.5f, fired, &NextForTest);
    CHECK(Near(ArmDegrees(lr), 0.f));  // (重み 0: 重ねない)
    SetRuleParam(lr, "wave", 1.f, false);
    StepAnimatorRules(lr, b, 0.f);
    StepAnimator(lr, b, 0.f, fired, &NextForTest);
    CHECK(Near(lr.layers[0].weight, 1.f) && Near(ArmDegrees(lr), 45.f));  // (レイヤーの Turn は 0.5 秒進んでいる)
    SetRuleParam(lr, "wave", 0.5f, false);
    StepAnimatorRules(lr, b, 0.f);
    StepAnimator(lr, b, 0.f, fired, &NextForTest);
    CHECK(Near(ArmDegrees(lr), 22.5f));
}

RM_TEST(GameAnim_RunWaitsWithoutPlayOnStart) {
    const game_x_3d::SkinnedModel m = TurningArm();
    Animator a;
    a.playOnStart = false;
    AnimatorRun run;
    BindModel(run, m.skin, m.clips);
    std::vector<const space3d::ClipEvent*> fired;
    StepAnimator(run, a, 0.3f, fired, &NextForTest);
    CHECK(run.paused && Near(run.player.Time(), 0.f) && run.version != 0);  // 最初のコマで止まっている
    CHECK(PlayClip(run, a, "Turn", 0));
    StepAnimator(run, a, 0.3f, fired, &NextForTest);
    CHECK(!run.paused && Near(run.player.Time(), 0.3f));
    // モデルを読む前に頼んだ PlayClip: 読んだら (フェードなしで) 流す
    Animator b;
    b.playOnStart = false;
    AnimatorRun early;
    CHECK(PlayClip(early, b, "Turn", 0.5f));
    BindModel(early, m.skin, m.clips);
    StepAnimator(early, b, 0.25f, fired, &NextForTest);
    CHECK(!early.paused && early.player.ClipIndex() == 0 && Near(early.player.Time(), 0.25f) && Near(early.player.FadeWeight(), 1.f));
    // 写した部品 (別のエンティティ) は、自分の再生を作り直す
    Animator mine;
    AnimatorRun& first = RunOf(mine, 3);
    Animator copy = mine;
    CHECK(copy.run == mine.run);
    AnimatorRun& second = RunOf(copy, 7);
    CHECK(&first != &second && copy.run->owner == 7 && mine.run->owner == 3);
    CHECK(&RunOf(mine, 3) == &first);
}

RM_TEST(GameAnim_BoneMatrixAndAttachedTransform) {
    const game_x_3d::SkinnedModel m = TurningArm();
    Animator a;
    AnimatorRun run;
    CHECK(!BoneModelMatrix(run, "Arm"));  // まだ無い
    BindModel(run, m.skin, m.clips);
    std::vector<const space3d::ClipEvent*> fired;
    StepAnimator(run, a, 0, fired, &NextForTest);
    const space3d::Mat4* arm = BoneModelMatrix(run, "Arm");
    CHECK(arm && Near(arm->m[3][0], 0) && Near(arm->m[3][1], 1) && Near(arm->m[3][2], 0));
    CHECK(!BoneModelMatrix(run, "Nope"));
    // 骨 (1, 2, 3) を Y まわりに 90 度。ずれ (0, 0, 1) は骨の向きで: ワールドの (2, 2, 3)
    const space3d::Mat4 bone = space3d::LocalMatrix({{1, 2, 3}, {0, 90, 0}, {1, 1, 1}});
    space3d::Transform now;
    now.scale = {2, 2, 2};
    BoneAttach at;
    at.position = {0, 0, 1};
    space3d::Transform t = AttachedTransform(now, space3d::Mat4{}, bone, at);
    CHECK(Near(t.position.x, 2, 1e-3f) && Near(t.position.y, 2, 1e-3f) && Near(t.position.z, 3, 1e-3f));
    CHECK(Near(t.rotation.x, 0, 1e-2f) && Near(t.rotation.y, 90, 1e-2f) && Near(t.rotation.z, 0, 1e-2f));
    CHECK(Near(t.scale.x, 2) && Near(t.scale.y, 2) && Near(t.scale.z, 2));  // 大きさはそのまま
    // 回転を合わせない: 位置だけ
    at.followRotation = false;
    t = AttachedTransform(now, space3d::Mat4{}, bone, at);
    CHECK(Near(t.position.x, 2, 1e-3f) && Near(t.rotation.y, 0, 1e-2f));
    // 親が (10, 0, 0) にいる: Transform は親から見た値
    const space3d::Mat4 parent = space3d::LocalMatrix({{10, 0, 0}, {0, 0, 0}, {1, 1, 1}});
    t = AttachedTransform(now, parent, bone, at);
    CHECK(Near(t.position.x, -8, 1e-3f) && Near(t.position.y, 2, 1e-3f));
}

// 見本のモデル (skinned_arm.gltf) を、.meta にイベントを書いて、システム「アニメーション」「骨に付ける」で動かす。
RM_TEST(GameAnim_SystemsPlayTheSampleAndAttach) {
    namespace fs = std::filesystem;
    const fs::path src = fs::path(__FILE__).parent_path().parent_path() / "docs" / "samples" / "models" / "skinned_arm.gltf";
    const fs::path dir = fs::temp_directory_path() / L"rm_anim_system_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path file = dir / L"arm.gltf";
    fs::copy_file(src, file, ec);
    CHECK(!ec);
    const auto import = json::Parse(R"({"clips": {"Wave": {"events": [{"time": 0.25, "name": "Step", "number": 3, "text": "左"}]}}})");
    CHECK(import && asset::WriteImportJson(file, *import, "a0a0a0a0b1b1b1b1c2c2c2c2d3d3d3d3"));

    const reflect::TypeDesc& transform = reflect::TypeOf<space3d::Transform>();
    const reflect::TypeDesc& mesh = reflect::TypeOf<game_x_3d::MeshRenderer>();
    const reflect::TypeDesc& animator = reflect::TypeOf<Animator>();
    const reflect::TypeDesc& attach = reflect::TypeOf<BoneAttach>();
    Scene scene;
    scene.SetComponentTypes({&transform, &mesh, &animator, &attach});
    const int arm = scene.Create("arm");
    CHECK(scene.AddComponent(arm, transform) && scene.AddComponent(arm, mesh) && scene.AddComponent(arm, animator));
    auto& mr = *static_cast<game_x_3d::MeshRenderer*>(scene.Find(arm)->FindComponent(mesh.name)->value.Get());
    mr.shape = game_x_3d::MeshShape::Model;
    mr.model.guid = "a0a0a0a0b1b1b1b1c2c2c2c2d3d3d3d3";
    auto& an = *static_cast<Animator*>(scene.Find(arm)->FindComponent(animator.name)->value.Get());
    an.clip = "Wave";
    auto& armAt = *static_cast<space3d::Transform*>(scene.Find(arm)->FindComponent(transform.name)->value.Get());
    armAt.position = {5, 0, 0};
    const int hat = scene.Create("hat", arm);
    CHECK(scene.AddComponent(hat, transform) && scene.AddComponent(hat, attach));
    static_cast<BoneAttach*>(scene.Find(hat)->FindComponent(attach.name)->value.Get())->bone = "Top";

    static const game_production::SystemInfo kAnim{"アニメーション", game_production::SystemPhase::Frame, 100, &RunAnimation};
    static const game_production::SystemInfo kAttach{"骨に付ける", game_production::SystemPhase::Frame, 110, &RunBoneAttach};
    const game_production::SystemInfo* systems[] = {&kAttach, &kAnim};  // (順は order が決める)
    game_production::SystemRunner runner;
    runner.SetSystems(systems);
    game_production::AssetAccess access;
    access.file = [&](const AssetRef& ref) { return ref.guid == "a0a0a0a0b1b1b1b1c2c2c2c2d3d3d3d3" ? file : fs::path(); };
    runner.SetAssets(access);

    runner.Advance(scene, 0.1, 0);
    CHECK(an.run && an.run->skin && an.run->skin->skeleton.bones.size() >= 3);
    CHECK(runner.Events().empty());  // 0 → 0.1 (Step は 0.25)
    runner.Advance(scene, 0.2, 0);   // 0.1 → 0.3
    CHECK(runner.Events().size() == 1);
    if (runner.Events().size() == 1) {
        const game_production::SystemEvent& ev = runner.Events()[0];
        CHECK(ev.name == "Step" && ev.number == 3 && ev.text == "左" && ev.target == arm && ev.entity == arm);
        // ホストはそれをスクリプトのイベントの列へ (送り手はエンジン・そのエンティティだけへ)
        game_production::ScriptWorld world;
        game_production::QueueSystemEvents(world, runner.Events());
        CHECK(world.events.size() == 1 && world.events[0].name == "Step" && world.events[0].scope == 1 && world.events[0].target == arm &&
              world.events[0].sender == 0 && world.events[0].text == "左");
    }
    // 帽子は骨 Top の所: 親 (arm) から見た値 = 骨のモデルの空間の位置
    const space3d::Mat4* top = an.run ? BoneModelMatrix(*an.run, "Top") : nullptr;
    CHECK(top != nullptr);
    if (top) {
        const auto& hatAt = *static_cast<const space3d::Transform*>(scene.Find(hat)->FindComponent(transform.name)->value.Get());
        CHECK(Near(hatAt.position.x, top->m[3][0], 1e-3f) && Near(hatAt.position.y, top->m[3][1], 1e-3f) &&
              Near(hatAt.position.z, top->m[3][2], 1e-3f));
        CHECK(top->m[3][1] > 0.5f);
    }
    fs::remove_all(dir, ec);
}

// --- ルートモーション (docs/ANIMATION.md 7-1) ---

namespace {

// 根 Root が 1 秒で Z へ 1 m 進む「Walk」(ルートモーションを使う) と、動かない「Still」。
game_x_3d::SkinnedModel WalkingBody() {
    game_x_3d::SkinnedModel m = TurningArm();
    auto clips = std::make_shared<std::vector<space3d::Clip>>();
    space3d::Clip walk = NamedClip("Walk", 1);
    space3d::BoneTrack t;
    t.bone = "Root";
    for (uint32_t i = 0; i < walk.frames; ++i) t.t.push_back({0, 0, static_cast<float>(i) / static_cast<float>(walk.frames - 1)});
    walk.tracks.push_back(t);
    walk.root.enabled = true;
    clips->push_back(walk);
    clips->push_back(NamedClip("Still", 1));
    return {m.skin, clips};
}

std::vector<Vec3> g_taken;
bool g_takes = false;
bool TakeForTest(Scene&, int, Vec3 move, float) {
    g_taken.push_back(move);
    return g_takes;
}

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
bool NearV(Vec3 a, Vec3 b, float eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

}  // namespace

RM_TEST(GameAnim_RootMotionTransform) {
    space3d::Transform t;
    t.position = {1, 2, 3};
    t.rotation = {0, 30, 0};
    const space3d::Transform r = RootMotionTransform(t, space3d::Mat4{}, {1, 0, 0}, 1.5707963f);
    CHECK(NearV(r.position, {2, 2, 3}));
    // 自分の Y のまわりに、クリップの根と同じ向きに 90 度 (前が、前の向きで右へ回る)
    const Vec3 fwd = space3d::TransformDirection(space3d::LocalMatrix(r), {0, 0, 1});
    CHECK(NearV(fwd, space3d::TransformDirection(space3d::LocalMatrix(t), animation3d::RotateY({0, 0, 1}, 1.5707963f))));
    CHECK(NearV(r.scale, t.scale));
    // 位置を書かない (物理のキャラクターが受け取った)
    CHECK(NearV(RootMotionTransform(t, space3d::Mat4{}, {5, 0, 0}, 0.f, false).position, t.position));
}

RM_TEST(GameAnim_RootMotionMovesTheEntity) {
    const game_x_3d::SkinnedModel m = WalkingBody();
    const reflect::TypeDesc& transform = reflect::TypeOf<space3d::Transform>();
    const reflect::TypeDesc& mesh = reflect::TypeOf<game_x_3d::MeshRenderer>();
    const reflect::TypeDesc& animator = reflect::TypeOf<Animator>();
    Scene scene;
    scene.SetComponentTypes({&transform, &mesh, &animator});
    const int body = scene.Create("body");
    CHECK(scene.AddComponent(body, transform) && scene.AddComponent(body, mesh) && scene.AddComponent(body, animator));
    auto& mr = *static_cast<game_x_3d::MeshRenderer*>(scene.Find(body)->FindComponent(mesh.name)->value.Get());
    mr.shape = game_x_3d::MeshShape::Model;
    mr.model.guid = "e0e0e0e0f1f1f1f1a2a2a2a2b3b3b3b3";
    auto& an = *static_cast<Animator*>(scene.Find(body)->FindComponent(animator.name)->value.Get());
    an.clip = "Walk";
    auto& at = *static_cast<space3d::Transform*>(scene.Find(body)->FindComponent(transform.name)->value.Get());
    at.rotation = {0, 90, 0};
    at.scale = {2, 2, 2};
    // (モデルはもう読んだものとしてつなぐ: ファイルを読まない)
    AnimatorRun& run = RunOf(an, body);
    run.model = mr.model.guid;
    BindModel(run, m.skin, m.clips);

    static const game_production::SystemInfo kAnim{"アニメーション", game_production::SystemPhase::Frame, 100, &RunAnimation};
    const game_production::SystemInfo* systems[] = {&kAnim};
    game_production::SystemRunner runner;
    runner.SetSystems(systems);
    RuntimeRegistry registry;
    runner.SetRegistry(&registry);

    // 動かす: 0.5 秒で 0.5 m (エンティティの向きと大きさで)。姿勢の根は動かない
    const space3d::Transform before = at;
    runner.Advance(scene, 0.5, 0);
    const Vec3 want = space3d::TransformDirection(space3d::LocalMatrix(before), {0, 0, 0.5f});
    CHECK(NearV(at.position, Add(before.position, want)));
    CHECK(Near(std::hypot(want.x, want.z), 1.f));  // (大きさ 2)
    CHECK(an.run && NearV(an.run->rootWorld, want) && an.run->rootActive);
    CHECK(an.run && Near(an.run->player.CurrentPose()[0].t.z, 0.f));
    CHECK(DrivenByRootMotion(scene, body));
    // その場で: 取り出して捨てる
    an.rootMotion = RootMotionUse::InPlace;
    const Vec3 p1 = at.position;
    runner.Advance(scene, 0.25, 0);
    CHECK(NearV(at.position, p1) && !DrivenByRootMotion(scene, body));
    // スクリプトに渡す: 動かさず、動いた分だけ持つ
    an.rootMotion = RootMotionUse::Script;
    runner.Advance(scene, 0.25, 0);
    CHECK(NearV(at.position, p1) && an.run && NearV(an.run->rootWorld, space3d::TransformDirection(space3d::LocalMatrix(at), {0, 0, 0.25f})));
    // 行き先 (物理のキャラクター) が受け取ると、位置は書かない
    static const RootMotionSinkInfo kSink{&TakeForTest};
    registry.Contribute(kRootMotionPoint, kSink);
    an.rootMotion = RootMotionUse::Move;
    g_taken.clear();
    g_takes = true;
    runner.Advance(scene, 0.25, 0);
    CHECK(g_taken.size() == 1 && g_taken.size() == 1 && std::hypot(g_taken[0].x, g_taken[0].z) > 0.4f);
    CHECK(NearV(at.position, p1));
    // 動かすクリップが止まった: 1 度だけ 0 を渡す (キャラクターが滑り続けない)、その後は尋ねない
    an.clip = "Still";
    runner.Advance(scene, 0.25, 0);
    CHECK(g_taken.size() == 2 && NearV(g_taken[1], {0, 0, 0}));
    runner.Advance(scene, 0.25, 0);
    CHECK(g_taken.size() == 2);
    g_takes = false;
}

RM_TEST(GameAnim_LayerWeightFromScript) {
    AnimatorRun run;
    SetLayerWeight(run, "上半身", 0.3f);
    CHECK(Near(LayerWeightOf(run, "上半身"), 0.3f));
    SetLayerWeight(run, "上半身", 2.f);  // (1 まで)
    CHECK(Near(LayerWeightOf(run, "上半身"), 1.f));
    SetLayerWeight(run, "上半身", -1.f);  // 外す
    CHECK(run.layerWeights.empty() && Near(LayerWeightOf(run, "上半身"), 0.f));
}

// --- IK (docs/ANIMATION.md 7-2-1。AnimatorIk.h) ---

namespace {

// 腰 → 腿 → 膝 → 足首 (左右 2 本)。足首は高さ 0.1。
game_x_3d::SkinnedModel Legs() {
    auto skin = std::make_shared<space3d::SkinData>();
    auto bone = [](const char* name, int parent, Vec3 t) {
        space3d::Bone b;
        b.name = name;
        b.parent = parent;
        b.rest.t = t;
        return b;
    };
    skin->skeleton.bones = {bone("Hips", -1, {0, 1, 0}),         bone("LThigh", 0, {-0.1f, 0, 0}), bone("LKnee", 1, {0, -0.45f, 0}),
                            bone("LFoot", 2, {0, -0.45f, 0}),    bone("RThigh", 0, {0.1f, 0, 0}),  bone("RKnee", 4, {0, -0.45f, 0}),
                            bone("RFoot", 5, {0, -0.45f, 0}),    bone("Head", 0, {0, 0.6f, 0})};
    skin->inverseBind.assign(skin->skeleton.bones.size(), space3d::Mat4{});
    auto clips = std::make_shared<std::vector<space3d::Clip>>();
    clips->push_back(NamedClip("Stand", 1));
    return {skin, clips};
}

IkChain Foot(const char* name, const char* bone) {
    IkChain c;
    c.name = name;
    c.bone = bone;
    c.goal = IkGoal::Ground;
    return c;
}

Vec3 Pos(const std::vector<space3d::Mat4>& m, int bone) { return {m[bone].m[3][0], m[bone].m[3][1], m[bone].m[3][2]}; }

}  // namespace

RM_TEST(GameAnim_IkFeetOnStepsAndPelvis) {
    const game_x_3d::SkinnedModel m = Legs();
    AnimatorRun run;
    BindModel(run, m.skin, m.clips);
    AnimatorIk ik;
    ik.chains = {Foot("左足", "LFoot"), Foot("右足", "RFoot")};
    ik.pelvis = "Hips";
    IkWorld world;
    world.dt = 1.f;  // (ならさずに、すぐ目標へ)
    // 左の下は段 (高さ 0.2)、右の下は穴 (高さ -0.15)
    world.ground = [](Vec3 from, Vec3, float) -> std::optional<GroundHit> { return GroundHit{{from.x, from.x < 0 ? 0.2f : -0.15f, from.z}, {0, 1, 0}}; };
    animation3d::Pose pose = animation3d::RestPose(m.skin->skeleton);
    CHECK(ApplyAnimatorIk(run, ik, world, pose));
    std::vector<space3d::Mat4> model;
    animation3d::ModelMatrices(m.skin->skeleton, pose, model);
    CHECK(Near(Pos(model, 3).y, 0.3f, 2e-3f));    // 左の足首: 段の分だけ上がる
    CHECK(Near(Pos(model, 6).y, -0.05f, 2e-3f));  // 右の足首: 穴の分だけ下がる
    CHECK(Near(Pos(model, 0).y, 0.85f, 1e-3f));   // 腰は低い方の足の分 (0.15) 下がる
    // 平らな地面: 何も変わらない
    world.ground = [](Vec3 from, Vec3, float) -> std::optional<GroundHit> { return GroundHit{{from.x, 0.f, from.z}, {0, 1, 0}}; };
    AnimatorRun flat;
    BindModel(flat, m.skin, m.clips);
    pose = animation3d::RestPose(m.skin->skeleton);
    ApplyAnimatorIk(flat, ik, world, pose);
    animation3d::ModelMatrices(m.skin->skeleton, pose, model);
    CHECK(Near(Pos(model, 3).y, 0.1f, 1e-3f) && Near(Pos(model, 0).y, 1.f, 1e-3f));
    // 地面が分からない (物理が無い): 何もしない
    world.ground = nullptr;
    pose = animation3d::RestPose(m.skin->skeleton);
    CHECK(!ApplyAnimatorIk(flat, ik, world, pose));
    // 重み 0 (スクリプトの SetIkWeight): かけない
    world.ground = [](Vec3 from, Vec3, float) -> std::optional<GroundHit> { return GroundHit{{from.x, 0.3f, from.z}, {0, 1, 0}}; };
    SetIkWeight(flat, "左足", 0.f);
    SetIkWeight(flat, "右足", 0.f);
    pose = animation3d::RestPose(m.skin->skeleton);
    CHECK(!ApplyAnimatorIk(flat, ik, world, pose));
}

RM_TEST(GameAnim_IkScriptAndEntityTargets) {
    const game_x_3d::SkinnedModel m = Legs();
    AnimatorRun run;
    BindModel(run, m.skin, m.clips);
    AnimatorIk ik;
    IkChain hand;
    hand.name = "手";
    hand.bone = "RFoot";  // (2 本の骨なら何でも)
    hand.goal = IkGoal::Script;
    IkChain look;
    look.name = "頭";
    look.kind = IkKind::LookAt;
    look.bone = "Head";
    look.goal = IkGoal::Script;
    look.maxAngle = 90;
    ik.chains = {hand, look};
    IkWorld world;
    space3d::Transform at;
    at.position = {10, 0, 0};  // エンティティはワールドの x = 10
    world.entityWorld = space3d::LocalMatrix(at);
    animation3d::Pose pose = animation3d::RestPose(m.skin->skeleton);
    CHECK(!ApplyAnimatorIk(run, ik, world, pose));  // 目標がまだ無い
    SetIkTarget(run, "手", {10.1f, 0.4f, 0.3f}, 1.f);  // ワールド
    SetLookAt(run, {10, 1.6f, 5}, 1.f);
    CHECK(ApplyAnimatorIk(run, ik, world, pose));
    std::vector<space3d::Mat4> model;
    animation3d::ModelMatrices(m.skin->skeleton, pose, model);
    CHECK(Near(Pos(model, 6).x, 0.1f, 2e-3f) && Near(Pos(model, 6).y, 0.4f, 2e-3f) && Near(Pos(model, 6).z, 0.3f, 2e-3f));
    CHECK(Near(IkWeight(run, look), 1.f));
    // 目標がエンティティ: その位置 (ずれはそのエンティティの向きで)
    ik.chains[0].goal = IkGoal::Entity;
    ik.chains[0].target.id = 7;
    ik.chains[0].offset = {0, 0.1f, 0};
    world.entity = [](const EntityRef& t, Vec3 offset) -> std::optional<Vec3> {
        if (t.id != 7) return std::nullopt;
        return Vec3{10.1f + offset.x, 0.3f + offset.y, 0.2f + offset.z};
    };
    pose = animation3d::RestPose(m.skin->skeleton);
    CHECK(ApplyAnimatorIk(run, ik, world, pose));
    animation3d::ModelMatrices(m.skin->skeleton, pose, model);
    CHECK(Near(Pos(model, 6).y, 0.4f, 2e-3f) && Near(Pos(model, 6).z, 0.2f, 2e-3f));
}
