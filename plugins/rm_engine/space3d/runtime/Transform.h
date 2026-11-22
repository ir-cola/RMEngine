#pragma once
// 3D 空間: 3D 空間でものがどこにあるかと、その計算 (描くのは同じプラグインの Renderer3D.h)。
// 3D 空間を持つソフト全部で共有する (コネクター「ゲーム制作 × 3D」を通したゲーム、あとの 3D モデルのソフト) -
// 設計 RME_PLATFORM 4-b: "3D 空間プラグイン = Transform (値と計算)・親子・レイ / 当たり / 投影".
// 純粋: 描画もファイルもない - tests/TestSpace3d.cpp でテストする。

#include "core/reflect/Reflect.h"

namespace rm::space3d {

RM_STRUCT(Label="Transform (位置・回転・大きさ)")
struct Transform {
    RM_PROPERTY(Edit, Label="位置", Icon="Position") Vec3 position{0, 0, 0};
    RM_PROPERTY(Edit, Label="回転 (度)", Icon="Rotation", Tooltip="X 軸 → Y 軸 → Z 軸 の順に回します (オイラー角)") Vec3 rotation{0, 0, 0};
    RM_PROPERTY(Edit, Label="大きさ", Icon="Scale") Vec3 scale{1, 1, 1};
};

// 4x4 行列、列ベクトル (p' = M * p)、m[列][行]。
struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};

Mat4 operator*(const Mat4& a, const Mat4& b);  // a を b の後に: (a * b) * p = a * (b * p)
Vec3 TransformPoint(const Mat4& m, Vec3 p);

// スケール、次に X -> Y -> Z の順に回転、次に移動: T * Rz * Ry * Rx * S。
Mat4 LocalMatrix(const Transform& t);
// Transform::rotation だけの回転: Rz * Ry * Rx (度)。
Mat4 RotationMatrix(Vec3 degrees);
// `axis` まわりに `degrees` 回す (軸の長さは何でもよい。右手系: 先端から見て反時計回り)。
Mat4 AxisAngle(Vec3 axis, float degrees);
// 純粋な回転の角度 (RotationMatrix の逆)。どの回転にも 2 組の角度がある (と、どの角度にも +
// 360 がある): `hint` に一番近いものを選ぶので、ドラッグ中にユーザーが見る値がなめらかに変わる。
Vec3 EulerFromRotation(const Mat4& r, Vec3 hint = {});
// 親の空間で: parentWorld * LocalMatrix(t) (一番上の親は単位行列)。
inline Mat4 WorldMatrix(const Mat4& parentWorld, const Transform& t) { return parentWorld * LocalMatrix(t); }

// 親も含めて、ものがワールドのどこにあるか - Transform のインスペクターの ワールド 表示。
RM_STRUCT(Label="ワールドでの値 (親を含む)")
struct WorldPlacement {
    RM_PROPERTY(Edit, Label="位置", Icon="Position") Vec3 position{0, 0, 0};
    RM_PROPERTY(Edit, Label="回転 (度)", Icon="Rotation", Tooltip="X 軸 → Y 軸 → Z 軸 の順に回します (オイラー角)") Vec3 rotation{0, 0, 0};
    RM_PROPERTY(Edit, Label="大きさ", Icon="Scale", Tooltip="親の大きさが軸ごとに違い、しかも回っているときは、ゆがみが出るので近い値です") Vec3 scale{1, 1, 1};
};
// ワールド行列を、位置・回転・(正の) スケールに分ける。鏡写しの行列は X のスケールが負になる。
// `hint`: EulerFromRotation と同じ。
WorldPlacement Decompose(const Mat4& world, Vec3 hint = {});
// `parentWorld` にある親の下で、ものをワールドの `want` に置く Transform。今 `current` が置く
// 場所から `want` が変えるものだけを変える (ワールドの X 位置を打っても動くだけで、回らない)。
// 親が不均一にスケールされて回っている場合を除き厳密 (その場合は一番近いもの)。
Transform FromWorld(const Transform& current, const Mat4& parentWorld, const WorldPlacement& want);

}  // namespace rm::space3d
