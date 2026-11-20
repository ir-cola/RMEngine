// スクリプト用の数学 (sdk/RMMath.h、ヘッダーのみ) のテスト: ベクトル、rm::Math、
// rm::Quat (エンジンの Transform の角度と同じ回転 - space3d::RotationMatrix)、rm::Random。
// RMMath.h は渡された型をそのまま使う (rm::Vec2 / Vec3 / Color: ここではエンジンのもので、
// RM.h のものと同じメンバーを持つ)。

#include "Test.h"

#include "base/Math.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"
// (エンジンのヘッダーの後に: ヘッダー側の Vec3 の演算子は rm::space3d にある。ここで先に宣言する。
// スクリプト側のものを rm に置くと、ヘッダーのインラインコードがあいまいになる)
#include "rm_engine/game_production/runtime/sdk/RMMath.h"

#include <cmath>

namespace {

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool Near(rm::Vec3 a, rm::Vec3 b, float eps = 1e-4f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

}  // namespace

RM_TEST(ScriptMath_Vectors) {
    using rm::Vec2;
    using rm::Vec3;
    const Vec3 a{1, 2, 3}, b{4, 5, 6};
    CHECK((a + b == Vec3{5, 7, 9}) && (b - a == Vec3{3, 3, 3}) && (a * 2.0f == Vec3{2, 4, 6}) && (2.0f * a == a * 2.0f) && (-a == Vec3{-1, -2, -3}));
    Vec3 c = a;
    c += b;
    c /= 5.0f;
    CHECK(Near(c, Vec3{1, 1.4f, 1.8f}));
    CHECK(Near(rm::Dot(a, b), 32) && (rm::Cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3{0, 0, 1}));
    CHECK(Near(rm::Length(Vec3{3, 0, 4}), 5) && Near(rm::Normalize(Vec3{0, 0, 9}), Vec3{0, 0, 1}) && (rm::Normalize(Vec3{}) == Vec3{}));
    CHECK(Near(rm::Distance(Vec2{0, 0}, Vec2{3, 4}), 5) && Near(rm::Cross(Vec2{1, 0}, Vec2{0, 1}), 1));
    CHECK(Near(rm::Lerp(a, b, 0.5f), Vec3{2.5f, 3.5f, 4.5f}) && Near(rm::Lerp(a, b, 2.0f), b) && Near(rm::LerpUnclamped(a, b, 2.0f), Vec3{7, 8, 9}));
    CHECK(Near(rm::MoveTowards(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 3), Vec3{3, 0, 0}) && Near(rm::MoveTowards(Vec3{0, 0, 0}, Vec3{1, 0, 0}, 3), Vec3{1, 0, 0}));
    CHECK(Near(rm::Reflect(Vec3{1, -1, 0}, Vec3{0, 1, 0}), Vec3{1, 1, 0}) && Near(rm::Angle(Vec3{1, 0, 0}, Vec3{0, 0, 1}), 90));
    CHECK(Near(rm::SignedAngle(Vec2{1, 0}, Vec2{0, -1}), -90) && Near(rm::ToAngle(rm::FromAngle(135)), 135));
    const Vec2 r = rm::Rotate(Vec2{1, 0}, 90);
    CHECK(Near(r.x, 0) && Near(r.y, 1) && Near(rm::Length(rm::ClampLength(Vec2{30, 40}, 5)), 5));
    CHECK(Near(rm::ProjectOnPlane(Vec3{1, 2, 3}, Vec3{0, 1, 0}), Vec3{1, 0, 3}));
}

RM_TEST(ScriptMath_Numbers) {
    namespace M = rm::Math;
    CHECK(Near(M::Lerp(0, 10, 0.25f), 2.5f) && Near(M::InverseLerp(10, 20, 15), 0.5f) && Near(M::Clamp01(3), 1) && M::Clamp(7, 0, 5) == 5);
    CHECK(Near(M::Repeat(5.5f, 2), 1.5f) && Near(M::Repeat(-0.5f, 2), 1.5f) && Near(M::PingPong(3, 2), 1));
    CHECK(Near(M::DeltaAngle(350, 10), 20) && Near(M::DeltaAngle(10, 350), -20) && Near(M::MoveTowardsAngle(350, 10, 5), 355));
    CHECK(Near(M::MoveTowards(0, 10, 4), 4) && Near(M::SmoothStep(0, 1, 0.5f), 0.5f) && M::Approximately(0.1f + 0.2f, 0.3f));
    // SmoothDamp は行き過ぎずに着く。
    float x = 0, v = 0;
    bool past = false;
    for (int i = 0; i < 300; ++i) {
        x = M::SmoothDamp(x, 10, v, 0.3f, 1.0f / 60);
        past = past || x > 10.0001f;
    }
    CHECK(!past && Near(x, 10, 1e-2f));
}

RM_TEST(ScriptMath_QuatIsTheEnginesTurn) {
    using rm::Quat;
    using rm::Vec3;
    // オイラー角: Transform の回転と同じ回し方 (X、Y、Z の順) - 全部の軸を確かめる。
    const Vec3 angles[] = {{30, 0, 0}, {0, 45, 0}, {0, 0, 60}, {10, 20, 30}, {-70, 120, 15}};
    for (const Vec3& deg : angles) {
        const Quat q = Quat::Euler(deg);
        const rm::space3d::Mat4 m = rm::space3d::RotationMatrix(deg);
        for (const Vec3 v : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{1, 2, 3}}) {
            CHECK(Near(q * v, rm::space3d::TransformDirection(m, v)));
        }
        // ... 戻しても: 回転の角度から同じ回転が得られる。
        const Vec3 back = q.ToEuler();
        CHECK(Near(Quat::Euler(back) * Vec3{1, 2, 3}, q * Vec3{1, 2, 3}));
    }
    // 物は自分の +Z の向きを見る (左手系): LookRotation は頼んだ向きに合わせ、Y を上にする。
    const Quat look = Quat::LookRotation(Vec3{1, 0, 0});
    CHECK(Near(look.Forward(), Vec3{1, 0, 0}) && Near(look.Up(), Vec3{0, 1, 0}) && Near(look.ToEuler(), Vec3{0, 90, 0}, 1e-3f));
    CHECK(Near(Quat::FromToRotation(Vec3{1, 0, 0}, Vec3{0, 1, 0}) * Vec3{1, 0, 0}, Vec3{0, 1, 0}));
    CHECK(Near(Quat::FromToRotation(Vec3{1, 0, 0}, Vec3{-1, 0, 0}) * Vec3{1, 0, 0}, Vec3{-1, 0, 0}));
    const Quat half = rm::Slerp(Quat::Identity(), Quat::AngleAxis(90, {0, 1, 0}), 0.5f);
    CHECK(Near(rm::Angle(Quat::Identity(), half), 45, 1e-2f) && Near((half * half.Inverse()) * Vec3{1, 2, 3}, Vec3{1, 2, 3}));
    CHECK(Near(rm::Angle(rm::RotateTowards(Quat::Identity(), Quat::AngleAxis(90, {0, 0, 1}), 30), Quat::Identity()), 30, 1e-2f));
}

RM_TEST(ScriptMath_Random) {
    rm::Random::Seed(7);
    const float first = rm::Random::Value();
    rm::Random::Seed(7);
    CHECK(rm::Random::Value() == first);  // 同じシード: 同じ数
    bool inside = true;
    int faces[7] = {};
    for (int i = 0; i < 6000; ++i) {
        const float f = rm::Random::Range(2.0f, 3.0f);
        inside = inside && f >= 2.0f && f < 3.0f;
        const int d = rm::Random::Range(1, 7);
        inside = inside && d >= 1 && d <= 6;
        if (d >= 1 && d <= 6) ++faces[d];
        inside = inside && rm::Length(rm::Random::InsideUnitCircle()) <= 1.0001f && Near(rm::Length(rm::Random::OnUnitSphere()), 1, 1e-3f);
    }
    CHECK(inside);
    for (int d = 1; d <= 6; ++d) CHECK(faces[d] > 800 && faces[d] < 1200);  // 各 1000 ほど
}
