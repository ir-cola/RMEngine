// 骨のあるモデルのデータ (plugins/rm_engine/space3d/runtime/Skeleton) のテスト: 四元数・位置 / 回転 /
// 大きさの行列とその逆・骨の木の順・ウェイトを 4 本にする・骨ごとの範囲・クリップのサンプルの数。

#include "Test.h"

#include "rm_engine/space3d/runtime/Skeleton.h"
#include "rm_engine/space3d/runtime/View.h"

#include <cmath>
#include <numbers>

using namespace rm;
using namespace rm::space3d;

namespace {

bool NearV(Vec3 a, Vec3 b, float eps = 1e-4f) { return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps; }
// 同じ回転 (q と -q は同じ)。
bool SameRotation(Quat a, Quat b) { return std::abs(std::abs(QuatDot(a, b)) - 1.f) < 1e-4f; }
bool NearM(const Mat4& a, const Mat4& b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(a.m[c][r] - b.m[c][r]) > eps) return false;
        }
    }
    return true;
}
constexpr float kPi = std::numbers::pi_v<float>;

}  // namespace

RM_TEST(Skeleton_QuatMatchesAxisAngle) {
    // 四元数の行列は、同じ軸と角度の AxisAngle (度) と同じ。
    const Vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 2, -3}};
    for (const Vec3& a : axes) {
        for (float deg : {0.f, 30.f, 90.f, 179.f, -120.f}) {
            CHECK(NearM(QuatMatrix(QuatAxisAngle(a, deg * kPi / 180.f)), AxisAngle(a, deg)));
        }
    }
    // Rotate と行列は同じ点に動かす。
    const Quat q = QuatAxisAngle({0.3f, 1, 0.2f}, 1.1f);
    CHECK(NearV(QuatRotate(q, {1, 2, 3}), TransformPoint(QuatMatrix(q), {1, 2, 3})));
    // 掛ける順: (a * b) は b の後に a。
    const Quat a = QuatAxisAngle({0, 1, 0}, kPi / 2), b = QuatAxisAngle({1, 0, 0}, kPi / 2);
    CHECK(NearM(QuatMatrix(a * b), QuatMatrix(a) * QuatMatrix(b)));
    CHECK(NearV(QuatRotate(a * QuatConjugate(a), {1, 2, 3}), {1, 2, 3}));
    // 長さ 0 は回らない。
    CHECK(QuatNormalize(Quat{0, 0, 0, 0}) == Quat{});
}

RM_TEST(Skeleton_QuatFromMatrixRoundTrip) {
    // 4 つの道 (trace > 0 / x / y / z が一番大きい) を全部通る角度。
    const Quat cases[] = {QuatAxisAngle({0, 1, 0}, 0.4f), QuatAxisAngle({1, 0, 0}, 3.0f), QuatAxisAngle({0, 1, 0}, 3.0f),
                          QuatAxisAngle({0, 0, 1}, 3.0f), QuatAxisAngle({1, -1, 2}, 2.5f), Quat{}};
    for (const Quat& q : cases) {
        const Quat back = QuatFromMatrix(QuatMatrix(q));
        CHECK(SameRotation(back, q));
        CHECK(back.w >= 0.f);
    }
}

RM_TEST(Skeleton_TRSRoundTrip) {
    BoneTRS p;
    p.t = {1, -2, 3};
    p.r = QuatAxisAngle({1, 1, 0}, 0.7f);
    p.s = {2, 0.5f, 1.5f};
    const Mat4 m = TRSMatrix(p);
    // T * R * S: 点は大きさ -> 回転 -> 移動
    const Vec3 v{1, 1, 1};
    CHECK(NearV(TransformPoint(m, v), [&] {
        const Vec3 r = QuatRotate(p.r, {v.x * 2, v.y * 0.5f, v.z * 1.5f});
        return Vec3{r.x + 1, r.y - 2, r.z + 3};
    }()));
    const BoneTRS back = DecomposeTRS(m);
    CHECK(NearV(back.t, p.t));
    CHECK(NearV(back.s, p.s));
    CHECK(SameRotation(back.r, p.r));
    // 鏡写し: X の大きさが負になり、同じ行列に戻る。
    BoneTRS mirror = p;
    mirror.s.x = -2;
    const BoneTRS mb = DecomposeTRS(TRSMatrix(mirror));
    CHECK(mb.s.x < 0);
    CHECK(NearM(TRSMatrix(mb), TRSMatrix(mirror)));
}

RM_TEST(Skeleton_BoneMatrixRows) {
    BoneTRS p;
    p.t = {4, 5, 6};
    p.r = QuatAxisAngle({0, 0, 1}, kPi / 2);
    const Mat4 m = TRSMatrix(p);
    const BoneMatrix b = ToBoneMatrix(m);
    // 行 i と float4(p, 1) の dot が、点の成分 i。
    const Vec3 v{1, 2, 3};
    const Vec3 want = TransformPoint(m, v);
    const float got[3] = {b.row[0][0] * v.x + b.row[0][1] * v.y + b.row[0][2] * v.z + b.row[0][3],
                          b.row[1][0] * v.x + b.row[1][1] * v.y + b.row[1][2] * v.z + b.row[1][3],
                          b.row[2][0] * v.x + b.row[2][1] * v.y + b.row[2][2] * v.z + b.row[2][3]};
    CHECK(NearV({got[0], got[1], got[2]}, want));
}

RM_TEST(Skeleton_ParentsFirstAndFind) {
    Skeleton s;
    s.bones = {{"Hips", -1, {}}, {"Spine", 0, {}}, {"頭", 1, {}}, {"Spine", 0, {}}};
    CHECK(ParentsFirst(s));
    CHECK(s.Find("頭") == 2);
    CHECK(s.Find("Spine") == 1);  // 同じ名前は最初のもの
    CHECK(s.Find("Tail") == -1);
    s.bones[1].parent = 2;  // 親が後ろ
    CHECK(!ParentsFirst(s));
    s.bones[1].parent = 1;  // 自分
    CHECK(!ParentsFirst(s));
}

RM_TEST(Skeleton_PackInfluences) {
    bool dropped = true;
    // 2 本: 合計 1 にし直す。
    SkinVertex v = PackInfluences({{3, 0.25f}, {7, 0.25f}}, 0, &dropped);
    CHECK(!dropped);
    CHECK(v.weight[0] + v.weight[1] == 65535);
    CHECK(v.weight[2] == 0 && v.weight[3] == 0);
    // 6 本: 重い 4 本を残す (重い順)。
    v = PackInfluences({{1, 0.05f}, {2, 0.3f}, {3, 0.1f}, {4, 0.2f}, {5, 0.25f}, {6, 0.1f}}, 0, &dropped);
    CHECK(dropped);
    CHECK(v.bone[0] == 2 && v.bone[1] == 5 && v.bone[2] == 4);
    CHECK(v.bone[3] == 3 || v.bone[3] == 6);
    CHECK(v.weight[0] + v.weight[1] + v.weight[2] + v.weight[3] == 65535);
    // 同じ骨は足す。0 と負は捨てる。
    v = PackInfluences({{9, 0.2f}, {9, 0.2f}, {4, 0.f}, {5, -1.f}}, 0, &dropped);
    CHECK(!dropped);
    CHECK(v.bone[0] == 9 && v.weight[0] == 65535 && v.weight[1] == 0);
    // 何も無い: 代わりの骨にウェイト 1。
    v = PackInfluences({}, 12, &dropped);
    CHECK(v.bone[0] == 12 && v.weight[0] == 65535);
}

RM_TEST(Skeleton_BoneBounds) {
    SkinData skin;
    skin.skeleton.bones = {{"a", -1, {}}, {"b", 0, {}}, {"c", 0, {}}};
    // 骨 b は (0, 1, 0) にバインドされている: 逆バインドは -1 動かす。
    BoneTRS bind;
    bind.t = {0, 1, 0};
    skin.inverseBind = {Mat4{}, *Inverse(TRSMatrix(bind)), Mat4{}};
    const std::vector<Vec3> pos = {{1, 1, 0}, {-1, 3, 2}, {5, 5, 5}};
    skin.vertices = {PackInfluences({{1, 1.f}}, 0), PackInfluences({{1, 0.5f}, {0, 0.5f}}, 0), PackInfluences({{0, 1.f}}, 0)};
    ComputeBoneBounds(skin, pos);
    CHECK(NearV(skin.boundsMin[1], {-1, 0, 0}));
    CHECK(NearV(skin.boundsMax[1], {1, 2, 2}));
    CHECK(NearV(skin.boundsMin[0], {-1, 3, 2}));
    CHECK(NearV(skin.boundsMax[0], {5, 5, 5}));
    CHECK(skin.boundsMin[2].x > skin.boundsMax[2].x);  // 動かす頂点が無い
}

RM_TEST(Skeleton_ClipFrames) {
    CHECK(ClipFrames(1.f, 30.f) == 31);
    CHECK(ClipFrames(0.5f, 24.f) == 13);
    CHECK(ClipFrames(0.f, 30.f) == 1);
    CHECK(ClipFrames(2.f, 0.f) == 1);
}
