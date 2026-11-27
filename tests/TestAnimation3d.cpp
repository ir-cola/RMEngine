// 3D アニメーション (plugins/rm_engine/animation3d/runtime) のテスト: クリップのサンプル (補間・ループの継ぎ目・
// 止める・四元数の反対側・トラックの無い骨)・名前でつなぐ (日本語・同じ名前)・混ぜる (クロスフェードと重みの和)・
// 親の順の骨の行列・休みの姿勢でスキンの行列が単位行列・時刻を進める・通ったイベント・Player・ルートモーション。

#include "Test.h"

#include "rm_engine/animation3d/runtime/ClipTime.h"
#include "rm_engine/animation3d/runtime/Ik.h"
#include "rm_engine/animation3d/runtime/Player.h"
#include "rm_engine/animation3d/runtime/Pose.h"
#include "rm_engine/animation3d/runtime/RootMotion.h"
#include "rm_engine/animation3d/runtime/Spring.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include <algorithm>
#include <numbers>

using namespace rm;
using namespace rm::space3d;
using namespace rm::animation3d;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

bool Near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
bool NearV(Vec3 a, Vec3 b, float eps = 1e-4f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }
// 同じ回転 (q と -q は同じ)。
bool SameRotation(Quat a, Quat b) { return std::abs(std::abs(QuatDot(a, b)) - 1.f) < 1e-4f; }
bool IsIdentity(const BoneMatrix& m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!Near(m.row[r][c], r == c ? 1.f : 0.f)) return false;
        }
    }
    return true;
}
Quat RotY(float degrees) { return QuatAxisAngle({0, 1, 0}, degrees * kPi / 180.f); }
Quat Neg(Quat q) { return {-q.x, -q.y, -q.z, -q.w}; }

Bone MakeBone(std::string name, int parent, Vec3 t) {
    Bone b;
    b.name = std::move(name);
    b.parent = parent;
    b.rest.t = t;
    return b;
}
// 腰 (根) -> 腕 -> 手 の 3 本。
Skeleton Chain() {
    Skeleton s;
    s.bones.push_back(MakeBone("腰", -1, {0, 1, 0}));
    s.bones.push_back(MakeBone("腕", 0, {1, 0, 0}));
    s.bones.push_back(MakeBone("手", 1, {1, 0, 0}));
    return s;
}
Clip MakeClip(float duration, float rate) {
    Clip c;
    c.name = "テスト";
    c.duration = duration;
    c.rate = rate;
    c.frames = ClipFrames(duration, rate);
    return c;
}

}  // namespace

RM_TEST(Anim3d_SampleInterpolates) {
    const Skeleton sk = Chain();
    Clip c = MakeClip(1.f, 2.f);  // サンプル 3 つ: 0 / 0.5 / 1 秒
    CHECK(c.frames == 3);
    BoneTrack tr;
    tr.bone = "腰";
    tr.t = {{0, 0, 0}, {2, 0, 0}, {4, 0, 0}};
    tr.r = {RotY(0), RotY(90), RotY(180)};
    tr.s = {{2, 2, 2}};  // 1 つ: ずっと同じ
    c.tracks.push_back(tr);
    BoneTrack onlyRot;  // 位置と大きさの列が空: 休みの姿勢のまま
    onlyRot.bone = "手";
    onlyRot.r = {RotY(10)};
    c.tracks.push_back(onlyRot);

    const ClipBinding b = BindClip(c, sk);
    Pose p;
    SampleClip(c, b, sk, 0.25f, false, p);
    CHECK(p.size() == 3);
    CHECK(NearV(p[0].t, {1, 0, 0}));
    CHECK(SameRotation(p[0].r, RotY(45)));
    CHECK(NearV(p[0].s, {2, 2, 2}));
    // トラックの無い骨は休みの姿勢
    CHECK(NearV(p[1].t, {1, 0, 0}) && p[1].r == Quat{} && NearV(p[1].s, {1, 1, 1}));
    // 列ごとに: 空の列は休みの値、1 つの列はその値
    CHECK(NearV(p[2].t, sk.bones[2].rest.t) && NearV(p[2].s, {1, 1, 1}));
    CHECK(SameRotation(p[2].r, RotY(10)));
    // サンプルちょうど
    SampleClip(c, b, sk, 0.5f, false, p);
    CHECK(NearV(p[0].t, {2, 0, 0}));
    CHECK(SameRotation(p[0].r, RotY(90)));
    // 回転は長さ 1
    SampleClip(c, b, sk, 0.4f, false, p);
    CHECK(Near(QuatDot(p[0].r, p[0].r), 1.f));
}

RM_TEST(Anim3d_SampleClampsWhenNotLooping) {
    const Skeleton sk = Chain();
    Clip c = MakeClip(1.f, 2.f);
    BoneTrack tr;
    tr.bone = "腰";
    tr.t = {{0, 0, 0}, {2, 0, 0}, {4, 0, 0}};
    c.tracks.push_back(tr);
    const ClipBinding b = BindClip(c, sk);
    Pose p;
    SampleClip(c, b, sk, 5.f, false, p);
    CHECK(NearV(p[0].t, {4, 0, 0}));
    SampleClip(c, b, sk, 1.f, false, p);  // 最後のサンプルは時刻 duration
    CHECK(NearV(p[0].t, {4, 0, 0}));
    SampleClip(c, b, sk, -1.f, false, p);
    CHECK(NearV(p[0].t, {0, 0, 0}));
    // ループ: 回る (-0.25 = 0.75、1.25 = 0.25)
    SampleClip(c, b, sk, -0.25f, true, p);
    CHECK(NearV(p[0].t, {3, 0, 0}));
    SampleClip(c, b, sk, 1.25f, true, p);
    CHECK(NearV(p[0].t, {1, 0, 0}));
    // duration x rate が整数でないとき (1.05 秒 x 2 = サンプル 3 つ) も、最後のサンプルがちょうど duration
    Clip odd = MakeClip(1.05f, 2.f);
    CHECK(odd.frames == 3);
    odd.tracks.push_back(tr);
    SampleClip(odd, BindClip(odd, sk), sk, 1.05f, false, p);
    CHECK(NearV(p[0].t, {4, 0, 0}));
    // 長さ 0 のクリップ・サンプル 1 つのクリップは最初のサンプル
    Clip still = MakeClip(0.f, 30.f);
    CHECK(still.frames == 1);
    BoneTrack one = tr;
    one.t = {{7, 0, 0}};
    still.tracks.push_back(one);
    SampleClip(still, BindClip(still, sk), sk, 3.f, true, p);
    CHECK(NearV(p[0].t, {7, 0, 0}));
}

RM_TEST(Anim3d_LoopSeamIsSeamless) {
    // ループするクリップは最後と最初が同じ値: 継ぎ目の前後でなめらかにつながる。
    const Skeleton sk = Chain();
    Clip c = MakeClip(1.f, 2.f);
    BoneTrack tr;
    tr.bone = "腰";
    tr.t = {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}};
    tr.r = {RotY(0), RotY(90), RotY(0)};
    c.tracks.push_back(tr);
    const ClipBinding b = BindClip(c, sk);
    Pose before, at, after, start;
    SampleClip(c, b, sk, 0.999f, true, before);
    SampleClip(c, b, sk, 1.f, true, at);
    SampleClip(c, b, sk, 1.001f, true, after);
    SampleClip(c, b, sk, 0.f, true, start);
    CHECK(NearV(at[0].t, start[0].t));
    CHECK(SameRotation(at[0].r, start[0].r));
    CHECK(NearV(before[0].t, start[0].t, 0.01f));
    CHECK(NearV(after[0].t, start[0].t, 0.01f));
    CHECK(std::abs(QuatDot(before[0].r, start[0].r)) > 0.999f);
    // 負の時刻の継ぎ目も
    SampleClip(c, b, sk, -0.001f, true, before);
    CHECK(NearV(before[0].t, start[0].t, 0.01f));
}

RM_TEST(Anim3d_QuatAntipodalTakesShortPath) {
    // -q は q と同じ回転。そろえないと 0 度と 90 度の間が遠回りになる。
    const Quat a = RotY(0), b = Neg(RotY(90));
    CHECK(QuatDot(a, b) < 0);
    CHECK(SameRotation(QuatNlerp(a, b, 0.5f), RotY(45)));
    CHECK(SameRotation(QuatNlerp(a, b, 0.f), a));
    CHECK(SameRotation(QuatNlerp(a, b, 1.f), b));
    // サンプルの列が反対側を向いていても同じ
    const Skeleton sk = Chain();
    Clip c = MakeClip(1.f, 1.f);
    BoneTrack tr;
    tr.bone = "腕";
    tr.r = {a, b};
    c.tracks.push_back(tr);
    Pose p;
    SampleClip(c, BindClip(c, sk), sk, 0.5f, false, p);
    CHECK(SameRotation(p[1].r, RotY(45)));
    // 混ぜるときも
    Pose pa = RestPose(sk), pb = RestPose(sk), out;
    pa[0].r = RotY(20);
    pb[0].r = Neg(RotY(60));
    Blend(pa, pb, 0.5f, out);
    CHECK(SameRotation(out[0].r, RotY(40)));
}

RM_TEST(Anim3d_BindByNameJapaneseAndDuplicates) {
    Skeleton sk;
    sk.bones.push_back(MakeBone("腰", -1, {}));
    sk.bones.push_back(MakeBone("髪", 0, {}));
    sk.bones.push_back(MakeBone("髪", 0, {}));
    sk.bones.push_back(MakeBone("頭", 0, {}));
    Clip c = MakeClip(1.f, 1.f);
    for (const char* name : {"髪", "頭", "髪", "無い骨"}) {
        BoneTrack t;
        t.bone = name;
        c.tracks.push_back(t);
    }
    const ClipBinding b = BindClip(c, sk);
    CHECK(b.track.size() == 4);
    CHECK(b.track[0] == -1);
    CHECK(b.track[1] == 0);  // 1 本目の「髪」は 1 本目のトラック
    CHECK(b.track[2] == 2);  // 2 本目の「髪」は 2 本目のトラック
    CHECK(b.track[3] == 1);
    CHECK(b.unmatched == 1);  // 「無い骨」
    // その名前のトラックが足りないときは最初のもの
    sk.bones.push_back(MakeBone("髪", 0, {}));
    const ClipBinding b2 = BindClip(c, sk);
    CHECK(b2.track[4] == 0);
    // 名前はバイトで比べる (似た字でも違う骨)
    Skeleton other;
    other.bones.push_back(MakeBone("かみ", -1, {}));
    const ClipBinding b3 = BindClip(c, other);
    CHECK(b3.track[0] == -1 && b3.unmatched == 4);
    // トラックの値を骨に入れる
    c.tracks[2].t = {{5, 0, 0}};
    Pose p;
    SampleClip(c, b, sk, 0.f, false, p);
    CHECK(NearV(p[2].t, {5, 0, 0}) && NearV(p[1].t, {0, 0, 0}));
}

RM_TEST(Anim3d_BlendWeights) {
    const Skeleton sk = Chain();
    Pose a = RestPose(sk), b = RestPose(sk), out;
    a[0].t = {0, 0, 0};
    b[0].t = {2, 4, 6};
    a[0].s = {1, 1, 1};
    b[0].s = {3, 3, 3};
    a[0].r = RotY(0);
    b[0].r = RotY(90);
    Blend(a, b, 0.f, out);
    CHECK(NearV(out[0].t, a[0].t) && SameRotation(out[0].r, a[0].r));
    Blend(a, b, 1.f, out);
    CHECK(NearV(out[0].t, b[0].t) && NearV(out[0].s, b[0].s) && SameRotation(out[0].r, b[0].r));
    Blend(a, b, 0.5f, out);
    CHECK(NearV(out[0].t, {1, 2, 3}) && NearV(out[0].s, {2, 2, 2}) && SameRotation(out[0].r, RotY(45)));
    // out が a と同じでもよい
    Pose self = a;
    Blend(self, b, 0.5f, self);
    CHECK(NearV(self[0].t, {1, 2, 3}));
    // 数が違う: 短い方に無い骨は長い方のまま
    Pose shortPose(1);
    shortPose[0].t = {9, 9, 9};
    Blend(shortPose, b, 0.5f, out);
    CHECK(out.size() == 3 && NearV(out[2].t, b[2].t));
}

RM_TEST(Anim3d_AccumulatorWeights) {
    const Skeleton sk = Chain();
    PoseAccumulator acc;
    Pose out;
    // 重みの合計が 0: 休みの姿勢
    acc.Begin(sk.bones.size());
    Pose moved = RestPose(sk);
    moved[0].t = {5, 5, 5};
    acc.Add(moved, 0.f);
    acc.Add(moved, -1.f);
    acc.Finish(sk, out);
    CHECK(acc.TotalWeight() == 0.f);
    CHECK(out.size() == 3 && NearV(out[0].t, sk.bones[0].rest.t));
    // 1 と 3: 1/4 と 3/4 (合計が 1 でなくてよい)。回転は反対側の符号でもそろえる
    Pose a = RestPose(sk), b = RestPose(sk);
    a[0].t = {0, 0, 0};
    b[0].t = {4, 0, 0};
    a[0].r = RotY(0);
    b[0].r = Neg(RotY(90));
    acc.Begin(sk.bones.size());
    acc.Add(a, 1.f);
    acc.Add(b, 3.f);
    acc.Finish(sk, out);
    CHECK(Near(acc.TotalWeight(), 4.f));
    CHECK(NearV(out[0].t, {3, 0, 0}));
    CHECK(Near(QuatDot(out[0].r, out[0].r), 1.f));
    // 90 度寄りの回転 (nlerp の重み 3/4 と同じ)
    CHECK(SameRotation(out[0].r, QuatNlerp(a[0].r, b[0].r, 0.75f)));
    // 同じ重みなら Blend(0.5) と同じ
    acc.Begin(sk.bones.size());
    acc.Add(a, 2.f);
    acc.Add(b, 2.f);
    acc.Finish(sk, out);
    Pose half;
    Blend(a, b, 0.5f, half);
    CHECK(NearV(out[0].t, half[0].t) && SameRotation(out[0].r, half[0].r));
    // 1 つだけ: そのまま
    acc.Begin(sk.bones.size());
    acc.Add(b, 0.3f);
    acc.Finish(sk, out);
    CHECK(NearV(out[0].t, b[0].t) && SameRotation(out[0].r, b[0].r));
    // 短い姿勢が届かない骨は休みの姿勢
    acc.Begin(sk.bones.size());
    Pose one(1);
    one[0].t = {1, 2, 3};
    acc.Add(one, 1.f);
    acc.Finish(sk, out);
    CHECK(NearV(out[0].t, {1, 2, 3}) && NearV(out[2].t, sk.bones[2].rest.t) && NearV(out[2].s, {1, 1, 1}));
}

RM_TEST(Anim3d_ModelMatricesParentsFirst) {
    const Skeleton sk = Chain();
    std::vector<Mat4> model;
    Pose p = RestPose(sk);
    ModelMatrices(sk, p, model);
    CHECK(model.size() == 3);
    CHECK(NearV(TransformPoint(model[0], {}), {0, 1, 0}));
    CHECK(NearV(TransformPoint(model[1], {}), {1, 1, 0}));
    CHECK(NearV(TransformPoint(model[2], {}), {2, 1, 0}));
    // 腕を Y のまわりに 90 度: 手はその先で回る (親から順に掛けている)
    p[1].r = RotY(90);
    ModelMatrices(sk, p, model);
    const Vec3 arm = TransformPoint(model[1], {});
    const Vec3 tip = QuatRotate(RotY(90), {1, 0, 0});
    CHECK(NearV(arm, {1, 1, 0}));
    CHECK(NearV(TransformPoint(model[2], {}), {1 + tip.x, 1 + tip.y, tip.z}));
    // 大きさも子に効く
    p = RestPose(sk);
    p[0].s = {2, 2, 2};
    ModelMatrices(sk, p, model);
    CHECK(NearV(TransformPoint(model[2], {}), {4, 1, 0}));
    // 姿勢が短い: 足りない骨は休みの姿勢
    ModelMatrices(sk, Pose{}, model);
    CHECK(NearV(TransformPoint(model[2], {}), {2, 1, 0}));
    // 壊れた木 (親が自分より後) は根として扱う
    Skeleton broken = sk;
    broken.bones[1].parent = 2;
    ModelMatrices(broken, RestPose(broken), model);
    CHECK(NearV(TransformPoint(model[1], {}), {1, 0, 0}));
}

RM_TEST(Anim3d_SkinMatricesIdentityAtRest) {
    SkinData skin;
    skin.skeleton = Chain();
    skin.skeleton.bones[1].rest.r = RotY(30);
    skin.skeleton.bones[2].rest.s = {1, 2, 1};
    std::vector<Mat4> model;
    ModelMatrices(skin.skeleton, RestPose(skin.skeleton), model);
    for (const Mat4& m : model) skin.inverseBind.push_back(*Inverse(m));
    std::vector<BoneMatrix> bones;
    SkinMatrices(model, skin.inverseBind, bones);
    CHECK(bones.size() == 3);
    for (const BoneMatrix& m : bones) CHECK(IsIdentity(m));
    // 動かすと単位行列でなくなる: 頂点は骨と一緒に動く
    Pose p = RestPose(skin.skeleton);
    p[0].t = {0, 3, 0};
    ModelMatrices(skin.skeleton, p, model);
    SkinMatrices(model, skin.inverseBind, bones);
    CHECK(!IsIdentity(bones[2]));
    CHECK(Near(bones[2].row[1][3], 2.f));  // 根が Y に +2 動いた
    // 逆バインド行列が足りない骨は骨の行列のまま
    SkinMatrices(model, std::span<const Mat4>{}, bones);
    CHECK(Near(bones[0].row[1][3], 3.f));
}

RM_TEST(Anim3d_AdvanceTime) {
    // 前へ: 継ぎ目をまたがない・またぐ・何周も
    TimeStep s = AdvanceTime(0.2f, 0.1f, 1.f, 1.f, true);
    CHECK(Near(s.time, 0.3f) && s.loops == 0);
    s = AdvanceTime(0.9f, 0.2f, 1.f, 1.f, true);
    CHECK(Near(s.time, 0.1f) && s.loops == 1);
    s = AdvanceTime(0.5f, 1.f, 3.2f, 1.f, true);  // 0.5 + 3.2 = 3.7
    CHECK(Near(s.time, 0.7f) && s.loops == 3);
    s = AdvanceTime(0.75f, 0.25f, 1.f, 1.f, true);  // ちょうど継ぎ目
    CHECK(Near(s.time, 0.f) && s.loops == 1);
    // 後ろへ
    s = AdvanceTime(0.5f, 0.2f, -1.f, 1.f, true);
    CHECK(Near(s.time, 0.3f) && s.loops == 0);
    s = AdvanceTime(0.1f, 0.2f, -1.f, 1.f, true);
    CHECK(Near(s.time, 0.9f) && s.loops == 1);
    s = AdvanceTime(0.1f, 1.f, -2.5f, 1.f, true);  // 0.1 - 2.5 = -2.4
    CHECK(Near(s.time, 0.6f) && s.loops == 3);
    s = AdvanceTime(0.f, 0.25f, -1.f, 1.f, true);
    CHECK(Near(s.time, 0.75f) && s.loops == 1);
    // ループでない: 止める
    s = AdvanceTime(0.9f, 0.5f, 1.f, 1.f, false);
    CHECK(Near(s.time, 1.f) && s.loops == 0);
    s = AdvanceTime(0.1f, 0.5f, -1.f, 1.f, false);
    CHECK(Near(s.time, 0.f) && s.loops == 0);
    // 長さ 0 のクリップ・速さ 0
    s = AdvanceTime(0.5f, 1.f, 1.f, 0.f, true);
    CHECK(s.time == 0.f && s.loops == 0);
    s = AdvanceTime(0.4f, 1.f, 0.f, 1.f, true);
    CHECK(Near(s.time, 0.4f) && s.loops == 0);
    // 範囲の外の時刻から始めても [0, duration) に回る
    s = AdvanceTime(2.5f, 0.f, 1.f, 1.f, true);
    CHECK(Near(s.time, 0.5f) && s.loops == 0);
    // 結果はいつも [0, duration)
    for (float t : {0.f, 0.3f, 0.999f}) {
        for (float d : {0.016f, 0.7f, 5.3f}) {
            for (float sp : {1.f, -1.f, 7.f, -13.f}) {
                const TimeStep r = AdvanceTime(t, d, sp, 1.f, true);
                CHECK(r.time >= 0.f && r.time < 1.f);
            }
        }
    }
}

RM_TEST(Anim3d_PassedEventsForward) {
    const float ev[] = {0.f, 0.25f, 0.5f, 1.f};  // 1 = 継ぎ目 (0 と同じ所)
    std::vector<int> out;
    auto run = [&](float prev, float dt, float speed, bool loop, bool includeStart = false) {
        out.clear();
        const TimeStep s = AdvanceTime(prev, dt, speed, 1.f, loop);
        PassedEvents(prev, s, speed, 1.f, ev, out, includeStart);
    };
    // またがない: prev < e <= now
    run(0.1f, 0.15f, 1.f, true);
    CHECK((out == std::vector<int>{1}));
    run(0.25f, 0.1f, 1.f, true);  // 前のフレームで出したものは出さない
    CHECK(out.empty());
    run(0.f, 0.1f, 1.f, true);  // 始めの 0 は出さない ...
    CHECK(out.empty());
    run(0.f, 0.1f, 1.f, true, true);  // ... 始めたばかりなら出す
    CHECK((out == std::vector<int>{0}));
    // 継ぎ目をまたぐ: 0.6 -> 1.1 (= 0.1)。継ぎ目 (0 と 1) は 1 回ずつ、時刻の順
    run(0.6f, 0.5f, 1.f, true);
    CHECK((out == std::vector<int>{0, 3}));
    // 1 フレームで何周も: 0.4 -> 2.6 (2 周)
    run(0.4f, 2.2f, 1.f, true);
    CHECK((out == std::vector<int>{2, 0, 3, 1, 2, 0, 3, 1, 2}));
    // ループでない: 終わり (1) で出して止まる
    run(0.9f, 0.5f, 1.f, false);
    CHECK((out == std::vector<int>{3}));
    run(1.f, 0.5f, 1.f, false);
    CHECK(out.empty());
    // 速さ 0・範囲の外のイベントは出さない
    run(0.1f, 0.5f, 0.f, true);
    CHECK(out.empty());
    const float outside[] = {-0.5f, 1.5f};
    out.clear();
    PassedEvents(0.f, AdvanceTime(0.f, 0.9f, 1.f, 1.f, true), 1.f, 1.f, outside, out, true);
    CHECK(out.empty());
}

RM_TEST(Anim3d_PassedEventsReverse) {
    const float ev[] = {0.f, 0.25f, 0.5f, 0.75f};
    std::vector<int> out;
    auto run = [&](float prev, float dt, float speed, bool loop, bool includeStart = false) {
        out.clear();
        const TimeStep s = AdvanceTime(prev, dt, speed, 1.f, loop);
        PassedEvents(prev, s, speed, 1.f, ev, out, includeStart);
        return s;
    };
    // またがない: now <= e < prev (下へ向かう順)
    run(0.6f, 0.4f, -1.f, true);  // 0.6 -> 0.2
    CHECK((out == std::vector<int>{2, 1}));
    // 0 に着いたら 0 を出し、そこから継ぎ目を越えても 2 度目は出さない
    TimeStep s = run(0.1f, 0.1f, -1.f, true);  // 0.1 -> 0
    CHECK(Near(s.time, 0.f) && s.loops == 0);
    CHECK((out == std::vector<int>{0}));
    s = run(0.f, 0.1f, -1.f, true);  // 0 -> 0.9
    CHECK(s.loops == 1);
    CHECK(out.empty());
    // またぐ: 0.3 -> -0.4 (= 0.6)
    run(0.3f, 0.7f, -1.f, true);
    CHECK((out == std::vector<int>{1, 0, 3}));
    // 何周も: 0.3 -> -1.8 (= 0.2。2 周)
    run(0.3f, 2.1f, -1.f, true);
    CHECK((out == std::vector<int>{1, 0, 3, 2, 1, 0, 3, 2, 1}));
    // 周の数の上限 (0.5 -> 1000 周 -> 0.5)
    out.clear();
    PassedEvents(0.5f, TimeStep{0.5f, 1000}, 1.f, 1.f, ev, out);
    CHECK(out.size() == 1 + 4 * static_cast<size_t>(kMaxEventLoops) + 3);
}

RM_TEST(Anim3d_PlayerCrossfadeAndEvents) {
    SkinData skin;
    skin.skeleton = Chain();
    {
        std::vector<Mat4> model;
        ModelMatrices(skin.skeleton, RestPose(skin.skeleton), model);
        for (const Mat4& m : model) skin.inverseBind.push_back(*Inverse(m));
    }
    std::vector<Clip> clips(2);
    clips[0] = MakeClip(1.f, 1.f);
    clips[0].name = "歩く";
    BoneTrack walk;
    walk.bone = "腰";
    walk.t = {{0, 1, 0}, {1, 1, 0}};
    clips[0].tracks.push_back(walk);
    clips[1] = MakeClip(1.f, 1.f);
    clips[1].name = "走る";
    BoneTrack run;
    run.bone = "腰";
    run.t = {{10, 1, 0}};
    clips[1].tracks.push_back(run);

    Player p;
    p.Bind(skin.skeleton, clips);
    std::vector<BoneMatrix> bones;
    // 止まっている: 休みの姿勢 (スキンの行列は単位行列)
    CHECK(p.ClipIndex() == -1);
    p.Tick(0.5f);
    p.Evaluate(skin, bones);
    CHECK(bones.size() == 3);
    for (const BoneMatrix& m : bones) CHECK(IsIdentity(m));

    const float events[] = {0.f, 0.25f, 0.5f};
    std::vector<int> fired;
    p.Play(0);
    p.Tick(0.25f);
    p.PassedEvents(events, fired);
    CHECK((fired == std::vector<int>{0, 1}));  // 始めの 0 も
    fired.clear();
    p.Tick(0.25f);
    p.PassedEvents(events, fired);
    CHECK((fired == std::vector<int>{2}));
    p.Evaluate(skin, bones);
    CHECK(NearV(p.CurrentPose()[0].t, {0.5f, 1, 0}));
    CHECK(Near(bones[0].row[0][3], 0.5f));
    CHECK(NearV(TransformPoint(p.ModelMatrices()[2], {}), {2.5f, 1, 0}));

    // クロスフェード: Play した瞬間は前の姿勢、半分で真ん中、終われば新しいクリップだけ
    p.Play(1, 1.f);
    CHECK(p.ClipIndex() == 1 && Near(p.Time(), 0.f));
    CHECK(Near(p.FadeWeight(), 0.f));
    p.Evaluate(skin, bones);
    CHECK(NearV(p.CurrentPose()[0].t, {0.5f, 1, 0}));
    p.Tick(0.5f);
    p.Evaluate(skin, bones);
    CHECK(Near(p.FadeWeight(), 0.5f));
    CHECK(NearV(p.CurrentPose()[0].t, {5.25f, 1, 0}));
    // フェードの途中で切り替えても飛ばない (見えていた姿勢から)
    p.Play(0, 1.f);
    p.Evaluate(skin, bones);
    CHECK(NearV(p.CurrentPose()[0].t, {5.25f, 1, 0}));
    p.Tick(2.f);
    p.Evaluate(skin, bones);
    CHECK(Near(p.FadeWeight(), 1.f));
    CHECK(NearV(p.CurrentPose()[0].t, {0, 1, 0}));  // 2 秒で 2 周して 0

    // ループしない逆再生は終わりから
    p.loop = false;
    p.speed = -1.f;
    p.Play(0);
    CHECK(Near(p.Time(), 1.f));
    p.Tick(0.25f);
    CHECK(Near(p.Time(), 0.75f));
    p.Tick(5.f);
    CHECK(Near(p.Time(), 0.f));
    // 時間のつまみ
    p.SetTime(0.5f);
    p.Evaluate(skin, bones);
    CHECK(NearV(p.CurrentPose()[0].t, {0.5f, 1, 0}));
    // 止める: 休みの姿勢へ
    p.Play(-1);
    p.Evaluate(skin, bones);
    for (const BoneMatrix& m : bones) CHECK(IsIdentity(m));
    // 範囲の外の番号も止める
    p.Play(5);
    CHECK(p.ClipIndex() == -1);
}

// --- ルートモーション (docs/ANIMATION.md 7-1。RootMotion.h) ---

namespace {

// 根 (腰) が 1 秒で Z へ `meters` 進むクリップ (刻み 4: サンプル 5 つ)。腰の下に腕 1 本。
Clip WalkClip(const char* name, float meters) {
    Clip c = MakeClip(1.f, 4.f);
    c.name = name;
    BoneTrack t;
    t.bone = "腰";
    for (uint32_t i = 0; i < c.frames; ++i) t.t.push_back({0, 1, meters * static_cast<float>(i) / static_cast<float>(c.frames - 1)});
    c.tracks.push_back(t);
    c.root.enabled = true;
    return c;
}

// 1 秒で前へ進みながら Y のまわりに 90 度曲がるクリップ (根の位置は四分円)。
Clip TurnClip() {
    Clip c = MakeClip(1.f, 8.f);
    c.name = "曲がる";
    BoneTrack t;
    t.bone = "腰";
    for (uint32_t i = 0; i < c.frames; ++i) {
        const float a = 0.5f * kPi * static_cast<float>(i) / static_cast<float>(c.frames - 1);
        t.t.push_back({1.f - std::cos(a), 1, std::sin(a)});
        t.r.push_back(RotY(90.f * static_cast<float>(i) / static_cast<float>(c.frames - 1)));
    }
    c.tracks.push_back(t);
    c.root.enabled = true;
    return c;
}

bool NearDelta(const RootDelta& a, const RootDelta& b, float eps = 1e-3f) { return NearV(a.move, b.move, eps) && Near(a.turn, b.turn, eps); }

}  // namespace

RM_TEST(Anim3d_RootBoneAndYaw) {
    Skeleton s = Chain();
    CHECK(RootBone(s, "") == 0);
    CHECK(RootBone(s, "腰") == 0);
    CHECK(RootBone(s, "腕") == -1);  // 親がある骨は選べない
    CHECK(RootBone(s, "無い") == -1);
    // 自動は「(骨の外)」を飛ばす
    Skeleton t;
    t.bones.push_back(MakeBone("(骨の外)", -1, {}));
    t.bones.push_back(MakeBone("Hips", -1, {}));
    CHECK(RootBone(t, "") == 1);
    // Y のまわりのひねりだけを取る (傾きは見ない)
    CHECK(Near(YawOf(RotY(90)), kPi / 2));
    CHECK(Near(YawOf(RotY(-30)), -kPi / 6));
    CHECK(Near(YawOf(RotY(40) * QuatAxisAngle({1, 0, 0}, 0.5f)), 40.f * kPi / 180.f));
    CHECK(NearV(RotateY({0, 0, 1}, kPi / 2), QuatRotate(RotY(90), {0, 0, 1})));
}

RM_TEST(Anim3d_RootDeltaStraightAndSeams) {
    const Skeleton s = Chain();
    const Clip walk = WalkClip("歩く", 2.f);  // 2 m / 秒
    const ClipBinding b = BindClip(walk, s);
    const ClipRootMotion& set = walk.root;
    // まっすぐ: 0.25 秒で 0.5 m
    CHECK(NearDelta(RootDeltaBetween(walk, b, s, 0, set, 0.f, AdvanceTime(0.f, 0.25f, 1, 1, true), 1, true), {{0, 0, 0.5f}, 0}));
    // ループの継ぎ目をまたぐ: 0.75 → 0.25 で 1 m (根が始めに戻る所で飛ばない)
    CHECK(NearDelta(RootDeltaBetween(walk, b, s, 0, set, 0.75f, AdvanceTime(0.75f, 0.5f, 1, 1, true), 1, true), {{0, 0, 1.f}, 0}));
    // 1 フレームで何周も: 2.5 秒で 5 m
    CHECK(NearDelta(RootDeltaBetween(walk, b, s, 0, set, 0.f, AdvanceTime(0.f, 2.5f, 1, 1, true), 1, true), {{0, 0, 5.f}, 0}));
    // 逆再生: 0.25 → 0.75 (継ぎ目をまたいで戻る) で -1 m
    CHECK(NearDelta(RootDeltaBetween(walk, b, s, 0, set, 0.25f, AdvanceTime(0.25f, 0.5f, -1, 1, true), -1, true), {{0, 0, -1.f}, 0}));
    // ループでない: 端で止まる
    CHECK(NearDelta(RootDeltaBetween(walk, b, s, 0, set, 0.75f, AdvanceTime(0.75f, 1.f, 1, 1, false), 1, false), {{0, 0, 0.5f}, 0}));
    // 横を外すと動かない・縦は既定で取らない
    ClipRootMotion noH = set;
    noH.horizontal = false;
    CHECK(NearDelta(RootSegment(walk, b, s, 0, noH, 0, 1), {}));
    CHECK(NearDelta(RootTotals(walk, b, s, 0, set), {{0, 0, 2.f}, 0}));
    // 使わない設定・根が無い: 0
    ClipRootMotion off = set;
    off.enabled = false;
    CHECK(NearDelta(RootSegment(walk, b, s, 0, off, 0, 1), {}));
    CHECK(NearDelta(RootSegment(walk, b, s, -1, set, 0, 1), {}));
}

RM_TEST(Anim3d_RootDeltaTurningClip) {
    const Skeleton s = Chain();
    const Clip turn = TurnClip();
    const ClipBinding b = BindClip(turn, s);
    // 1 回で 90 度回り、モデルの空間で (1, 0, 1) へ
    const RootDelta whole = RootTotals(turn, b, s, 0, turn.root);
    CHECK(NearDelta(whole, {{1, 0, 1}, kPi / 2}, 2e-3f));
    // 半分ずつ足しても同じ (後の半分は、前の半分で回った向きから見たもの)
    const RootDelta first = RootSegment(turn, b, s, 0, turn.root, 0.f, 0.5f);
    const RootDelta second = RootSegment(turn, b, s, 0, turn.root, 0.5f, 1.f);
    CHECK(Near(first.turn, kPi / 4, 2e-3f));
    CHECK(NearDelta(AppendDelta(first, second), whole, 2e-3f));
    // 後の半分を自分の向きから見ると、前へ進んで右 (+X) へ寄る (左手系: X が右、Z が前)
    CHECK(second.move.z > 0.3f && second.move.x > 0.05f);
    // 回転を取らないと、モデルの空間のまま
    ClipRootMotion noTurn = turn.root;
    noTurn.turn = false;
    CHECK(NearDelta(RootSegment(turn, b, s, 0, noTurn, 0.f, 1.f), {{1, 0, 1}, 0}, 2e-3f));
    // 2 周 (ループ) すると 180 度
    CHECK(Near(RootDeltaBetween(turn, b, s, 0, turn.root, 0.f, AdvanceTime(0.f, 2.f, 1, 1, true), 1, true).turn, kPi, 4e-3f));
}

RM_TEST(Anim3d_StripRootKeepsTilt) {
    Skeleton s = Chain();
    Pose pose = RestPose(s);
    BoneTRS start;
    start.t = {0, 1, 0};
    const Quat tilt = QuatAxisAngle({1, 0, 0}, 0.4f);
    pose[0].t = {3, 1.2f, 4};
    pose[0].r = RotY(70) * tilt;
    ClipRootMotion set;
    set.enabled = true;
    StripRoot(pose, 0, set, start);
    CHECK(NearV(pose[0].t, {0, 1.2f, 0}));        // 横だけ始めの値に (縦はそのまま)
    CHECK(SameRotation(pose[0].r, tilt));         // Y のひねりだけ外れ、傾きは残る
    CHECK(NearV(pose[1].t, s.bones[1].rest.t));   // ほかの骨はそのまま
    set.vertical = true;
    StripRoot(pose, 0, set, start);
    CHECK(Near(pose[0].t.y, 1.f));
}

RM_TEST(Anim3d_MixRootDelta) {
    const RootDelta a{{0, 0, 1}, 0.2f}, b{{0, 0, 3}, 0.f};
    const RootDelta ds[] = {a, b};
    const float half[] = {1, 1};
    CHECK(NearDelta(MixRootDelta(ds, half), {{0, 0, 2}, 0.1f}));
    const float onlyB[] = {0, 2};
    CHECK(NearDelta(MixRootDelta(ds, onlyB), b));
    const float none[] = {0, -1};
    CHECK(NearDelta(MixRootDelta(ds, none), {}));
}

RM_TEST(Anim3d_PlayerRootMotion) {
    SkinData skin;
    skin.skeleton = Chain();
    {
        std::vector<Mat4> model;
        ModelMatrices(skin.skeleton, RestPose(skin.skeleton), model);
        for (const Mat4& m : model) skin.inverseBind.push_back(*Inverse(m));
    }
    std::vector<Clip> clips{WalkClip("歩く", 1.f), WalkClip("走る", 3.f), MakeClip(1.f, 4.f)};
    clips[2].name = "待つ";  // (ルートモーションなし)
    Player p;
    p.Bind(skin.skeleton, clips);
    std::vector<BoneMatrix> bones;
    // 歩く: 0.5 秒で 0.5 m。姿勢の根は始めの所のまま (その場で歩く)
    p.Play(0);
    p.Tick(0.5f);
    CHECK(NearDelta(p.RootStep(), {{0, 0, 0.5f}, 0}) && p.RootActive());
    p.Evaluate(skin, bones);
    CHECK(NearV(p.CurrentPose()[0].t, {0, 1, 0}));
    // 消さないと、根はクリップどおり
    p.stripRoot = false;
    p.Evaluate(skin, bones);
    CHECK(NearV(p.CurrentPose()[0].t, {0, 1, 0.5f}));
    p.stripRoot = true;
    // 時間のつまみは動かさない
    p.SetTime(0.9f);
    CHECK(NearDelta(p.RootStep(), {}));
    // 混ぜる: 歩く (1 m/s) と走る (3 m/s) を半分ずつ → 2 m/s (足をそろえる: 割合は混ぜた長さで進む)
    const Player::Motion mix[] = {{0, 1}, {1, 1}};
    p.Play(0);
    p.SetBlend(mix);
    p.Tick(0.25f);
    CHECK(NearDelta(p.RootStep(), {{0, 0, 0.5f}, 0}));
    p.SetBlend({});
    // フェード: 2 m/s で歩いている所から、動かない「待つ」へ 1 秒で。半分の所の 0.5 秒は 2 x 0.5 x (1 - 重み)
    p.Play(1);
    p.Tick(0.5f);  // 走る: 1.5 m (3 m/s)
    CHECK(NearDelta(p.RootStep(), {{0, 0, 1.5f}, 0}));
    p.Play(2, 1.f);
    p.Tick(0.5f);
    CHECK(Near(p.FadeWeight(), 0.5f));
    CHECK(NearDelta(p.RootStep(), {{0, 0, 3.f * 0.5f * 0.5f}, 0}) && p.RootActive());
    p.Tick(0.5f);  // フェードが終わった: 動かない
    CHECK(NearDelta(p.RootStep(), {}) && !p.RootActive());
    CHECK(p.RootBoneOf(0) == 0 && p.RootBoneOf(2) == -1);
    CHECK(NearDelta(p.RootTotalsOf(1), {{0, 0, 3.f}, 0}));
}

// --- IK (docs/ANIMATION.md 7-2-1。Ik.h) ---

namespace {

// 腰 (根、高さ 1) → 腿 → 膝 → 足首 (下へ 0.45 ずつ)。
Skeleton Leg() {
    Skeleton s;
    s.bones.push_back(MakeBone("腰", -1, {0, 1, 0}));
    s.bones.push_back(MakeBone("腿", 0, {0.1f, 0, 0}));
    s.bones.push_back(MakeBone("膝", 1, {0, -0.45f, 0}));
    s.bones.push_back(MakeBone("足首", 2, {0, -0.45f, 0}));
    return s;
}

float Dist(Vec3 a, Vec3 b) { return Length(a - b); }

}  // namespace

RM_TEST(Anim3d_FromToAndLimit) {
    CHECK(NearV(QuatRotate(FromTo({1, 0, 0}, {0, 1, 0}), {1, 0, 0}), {0, 1, 0}));
    CHECK(NearV(QuatRotate(FromTo({0, 0, 1}, {0, 0, -1}), {0, 0, 1}), {0, 0, -1}));  // 反対向き
    CHECK(SameRotation(FromTo({0, 0, 0}, {1, 0, 0}), Quat{}));
    const Quat big = QuatAxisAngle({0, 1, 0}, 2.f);
    CHECK(SameRotation(LimitAngle(big, 0.5f), QuatAxisAngle({0, 1, 0}, 0.5f)));
    CHECK(SameRotation(Partial(big, 0.f), Quat{}) && SameRotation(Partial(big, 1.f), big));
}

RM_TEST(Anim3d_SolveTwoBone) {
    const Vec3 a{0, 1, 0}, b{0, 0.5f, 0.01f}, c{0, 0, 0};
    // 届く目標: 先がちょうど着き、骨の長さは変わらない
    const TwoBonePoints p = SolveTwoBone(a, b, c, {0, 0.4f, 0.3f}, {0, 0, 1});
    CHECK(NearV(p.end, {0, 0.4f, 0.3f}, 1e-3f));
    CHECK(Near(Dist(p.mid, a), Dist(b, a), 1e-4f) && Near(Dist(p.end, p.mid), Dist(c, b), 1e-3f));
    // 曲げる向き (pole) の側に曲がる
    CHECK(SolveTwoBone(a, b, c, {0, 0.3f, 0}, {0, 0, 1}).mid.z > 0.1f);
    CHECK(SolveTwoBone(a, b, c, {0, 0.3f, 0}, {0, 0, -1}).mid.z < -0.1f);
    // 届かない: まっすぐ伸ばす (長さは変えない)
    const TwoBonePoints far = SolveTwoBone(a, b, c, {0, -5, 0}, {0, 0, 1});
    CHECK(Near(Dist(far.end, a), Dist(b, a) + Dist(c, b), 1e-3f) && far.end.y < 0);
}

RM_TEST(Anim3d_ApplyTwoBoneAndLookAt) {
    const Skeleton s = Leg();
    Pose pose = RestPose(s);
    std::vector<Mat4> model;
    // 足首を前と上へ: 着いて、膝は前に曲がる
    CHECK(ApplyTwoBone(pose, s, model, 3, {0.1f, 0.3f, 0.2f}, {0, 0, 1}, 1.f));
    CHECK(NearV(BonePosition(model, 3), {0.1f, 0.3f, 0.2f}, 2e-3f));
    CHECK(BonePosition(model, 2).z > 0.1f);
    // 重み 0: 変わらない。骨が足りない (腿は親の親が無い): false
    Pose still = RestPose(s);
    CHECK(!ApplyTwoBone(still, s, model, 3, {0, 0.5f, 0}, {0, 0, 1}, 0.f));
    CHECK(!ApplyTwoBone(still, s, model, 1, {0, 0.5f, 0}, {0, 0, 1}, 1.f));
    for (size_t i = 0; i < still.size(); ++i) CHECK(SameRotation(still[i].r, s.bones[i].rest.r));
    // 向ける: 前 (+Z) を右 (+X) へ。上限 30 度なら 30 度だけ
    Pose head = RestPose(s);
    const Vec3 fwd = RestForward(s, 0);
    CHECK(NearV(fwd, {0, 0, 1}));
    CHECK(ApplyLookAt(head, s, model, 0, fwd, {5, 1, 0}, 3.f, 1.f));
    CHECK(NearV(QuatRotate(head[0].r, {0, 0, 1}), {1, 0, 0}, 1e-3f));
    head = RestPose(s);
    ApplyLookAt(head, s, model, 0, fwd, {5, 1, 0}, 30.f * kPi / 180.f, 1.f);
    CHECK(Near(std::acos(std::clamp(Dot(QuatRotate(head[0].r, {0, 0, 1}), {0, 0, 1}), -1.f, 1.f)), 30.f * kPi / 180.f, 1e-3f));
    // 足を面に: 上を傾いた法線へ (上限まで)
    Pose foot = RestPose(s);
    const Vec3 n = Normalize({0, 1, 1});
    CHECK(AlignToNormal(foot, s, model, 3, n, 1.f, 1.f));
    CHECK(NearV(TransformDirection(model[3], {0, 1, 0}), n, 1e-3f));
}

// ブレンドシェイプの重み (docs/ANIMATION.md 7-2-2): クリップの曲線を姿勢と一緒に読み、混ぜる・フェードも同じ重みで。
RM_TEST(Anim3d_PlayerMorphWeights) {
    SkinData skin;
    skin.skeleton = Chain();
    std::vector<Clip> clips(3);
    clips[0] = MakeClip(1.f, 4.f);
    clips[0].name = "笑う";
    clips[0].morphs = {{"口", {0, 0.25f, 0.5f, 0.75f, 1.f}}};
    clips[1] = MakeClip(1.f, 4.f);
    clips[1].name = "怒る";
    clips[1].morphs = {{"眉", {1.f}}};
    clips[2] = MakeClip(1.f, 4.f);
    clips[2].name = "何もしない";
    const std::string names[] = {"口", "眉", "目"};
    const float defaults[] = {0.f, 0.f, 0.3f};
    Player p;
    p.Bind(skin.skeleton, clips);
    p.BindMorphs(names, defaults);
    p.Play(0);
    p.Tick(0.5f);
    p.EvaluatePose(skin.skeleton);
    CHECK(Near(p.MorphWeights()[0], 0.5f) && Near(p.MorphWeights()[1], 0.f) && Near(p.MorphWeights()[2], 0.3f));  // 曲線の無い形は初めの値
    CHECK(p.MorphAnimated(0) && !p.MorphAnimated(1));
    // 混ぜる: 同じ割合の時刻で読み、重みで混ぜる (笑う 0.5 と 怒る の眉 1 を半分ずつ)
    const Player::Motion mix[] = {{0, 1}, {1, 1}};
    p.SetBlend(mix);
    p.EvaluatePose(skin.skeleton);
    CHECK(Near(p.MorphWeights()[0], 0.25f) && Near(p.MorphWeights()[1], 0.5f));
    p.SetBlend({});
    // フェード: 止めた重みから新しいクリップの重みへ
    p.Play(0);
    p.Tick(1.f);  // (ループで 0 に戻る手前ではなく、時刻 0)
    p.SetTime(1.f - 1e-4f);
    p.EvaluatePose(skin.skeleton);
    const float start = p.MorphWeights()[0];
    p.Play(2, 1.f);
    p.Tick(0.5f);
    p.EvaluatePose(skin.skeleton);
    CHECK(Near(p.MorphWeights()[0], start * 0.5f, 1e-3f));
    // 形の無いモデル: 空
    Player none;
    none.Bind(skin.skeleton, clips);
    none.Play(0);
    none.EvaluatePose(skin.skeleton);
    CHECK(none.MorphWeights().empty());
}

// 足すレイヤーの基準「クリップの最初のコマ」(7-5): 今のクリップの始めの姿勢と形の重み。
RM_TEST(Anim3d_PlayerFirstFrame) {
    SkinData skin;
    skin.skeleton = Chain();
    std::vector<Clip> clips{WalkClip("歩く", 2.f)};
    clips[0].root.enabled = false;  // (根を消さない: 始めの姿勢そのもの)
    clips[0].morphs = {{"口", {0.2f, 0.4f, 0.6f, 0.8f, 1.f}}};
    const std::string names[] = {"口"};
    const float defaults[] = {0.f};
    Player p;
    p.Bind(skin.skeleton, clips);
    p.BindMorphs(names, defaults);
    Pose first;
    std::vector<float> morphs;
    p.FirstFrame(skin.skeleton, first, morphs);  // クリップが無い: 休みの姿勢
    CHECK(NearV(first[0].t, {0, 1, 0}) && morphs.size() == 1 && Near(morphs[0], 0.f));
    p.Play(0);
    p.Tick(0.5f);
    p.FirstFrame(skin.skeleton, first, morphs);
    CHECK(NearV(first[0].t, {0, 1, 0}) && Near(morphs[0], 0.2f));  // 今の時刻 (0.5 秒) ではなく始め
}

// --- 揺れる骨 (docs/ANIMATION.md 7-2-3。Spring.h) ---

namespace {

// 根 → 1 → 2 (+X へ 1 ずつ、横に寝た鎖)。
Skeleton Tail() {
    Skeleton s;
    s.bones.push_back(MakeBone("根", -1, {0, 0, 0}));
    s.bones.push_back(MakeBone("一", 0, {1, 0, 0}));
    s.bones.push_back(MakeBone("二", 1, {1, 0, 0}));
    return s;
}

Vec3 Tip(const Skeleton& s, const Pose& pose) {
    std::vector<Mat4> m;
    ModelMatrices(s, pose, m);
    return BonePosition(m, 2);
}

}  // namespace

RM_TEST(Anim3d_SpringsSagAndKeepLength) {
    const Skeleton s = Tail();
    SpringState st;
    SpringJoint set;
    set.stiffness = 0;
    set.gravity = 9.8f;
    AddSpringChain(s, 0, set, st.joints);
    CHECK(st.joints.size() == 2 && st.joints[0].bone == 1 && st.joints[1].parent == 1);
    std::vector<Mat4> model;
    Pose pose;
    for (int i = 0; i < 120; ++i) {
        pose = RestPose(s);
        StepSprings(st, s, pose, model, Mat4{}, 1.f / 60.f, false);
    }
    const Vec3 tip = Tip(s, pose);
    CHECK(tip.y < -1.f);                                              // 重力で垂れる
    CHECK(Near(Length(BonePosition(model, 1)), 1.f, 1e-3f));          // 長さは変わらない
    CHECK(Near(Length(tip - BonePosition(model, 1)), 1.f, 1e-3f));
    // 戻る強さだけにすると、アニメーションの向き (横) へ戻っていく
    for (SpringJoint& j : st.joints) j.gravity = 0, j.stiffness = 4.f;
    for (int i = 0; i < 240; ++i) {
        pose = RestPose(s);
        StepSprings(st, s, pose, model, Mat4{}, 1.f / 60.f, false);
    }
    CHECK(Tip(s, pose).y > tip.y + 0.5f);
}

RM_TEST(Anim3d_SpringsCollideWarpAndFrameRate) {
    const Skeleton s = Tail();
    SpringJoint set;
    set.stiffness = 0;
    set.gravity = 0;
    // 当たりの球: 先 (2, 0, 0) を囲む。押し出されて、球の外 (半径 + 太さ) に出る
    SpringState st;
    AddSpringChain(s, 0, set, st.joints);
    st.spheres.push_back({0, {2, 0, 0.2f}, 0.5f});
    std::vector<Mat4> model;
    Pose pose = RestPose(s);
    StepSprings(st, s, pose, model, Mat4{}, 1.f / 60.f, false);
    CHECK(Length(Tip(s, pose) - Vec3{2, 0, 0.2f}) > 0.45f);  // (長さに戻すので、ほぼ球の外)
    // ワープ: エンティティが遠くへ飛んだら、始め直す (アニメーションの形のまま)
    SpringState w;
    SpringJoint g = set;
    g.gravity = 9.8f;
    AddSpringChain(s, 0, g, w.joints);
    for (int i = 0; i < 30; ++i) {
        pose = RestPose(s);
        StepSprings(w, s, pose, model, Mat4{}, 1.f / 60.f, false);
    }
    const Mat4 far = space3d::LocalMatrix({{50, 0, 0}, {0, 0, 0}, {1, 1, 1}});
    pose = RestPose(s);
    StepSprings(w, s, pose, model, far, 0.f, false);
    CHECK(NearV(Tip(s, pose), {2, 0, 0}, 1e-3f));
    // 30 fps と 60 fps でほぼ同じ形 (刻みは 1/60 秒ずつ)
    SpringState a, b;
    AddSpringChain(s, 0, g, a.joints);
    AddSpringChain(s, 0, g, b.joints);
    Pose pa, pb;
    for (int i = 0; i < 30; ++i) {
        pa = RestPose(s);
        StepSprings(a, s, pa, model, Mat4{}, 1.f / 30.f, false);
    }
    for (int i = 0; i < 60; ++i) {
        pb = RestPose(s);
        StepSprings(b, s, pb, model, Mat4{}, 1.f / 60.f, false);
    }
    CHECK(Length(Tip(s, pa) - Tip(s, pb)) < 0.02f);
}
