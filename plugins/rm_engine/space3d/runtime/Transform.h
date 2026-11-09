#pragma once
// 3D 空間: where a thing is in 3D space, and the math of it (its drawing is Renderer3D.h, the same
// plugin). Shared by every software that has a 3D space (the game through the connector "ゲーム制作 × 3D", the 3D model software later) -
// design RME_PLATFORM 4-b: "3D 空間プラグイン = Transform (値と計算)・親子・レイ / 当たり / 投影".
// Pure: no drawing, no files - tested in tests/TestSpace3d.cpp.

#include "core/reflect/Reflect.h"

namespace rm::space3d {

RM_STRUCT(Label="Transform (位置・回転・大きさ)")
struct Transform {
    RM_PROPERTY(Edit, Label="位置") Vec3 position{0, 0, 0};
    RM_PROPERTY(Edit, Label="回転 (度)", Tooltip="X 軸 → Y 軸 → Z 軸 の順に回します (オイラー角)") Vec3 rotation{0, 0, 0};
    RM_PROPERTY(Edit, Label="大きさ") Vec3 scale{1, 1, 1};
};

// 4x4 matrix, column vectors (p' = M * p), m[column][row].
struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
};

Mat4 operator*(const Mat4& a, const Mat4& b);  // a after b: (a * b) * p = a * (b * p)
Vec3 TransformPoint(const Mat4& m, Vec3 p);

// Scale, then rotate X -> Y -> Z, then move: T * Rz * Ry * Rx * S.
Mat4 LocalMatrix(const Transform& t);
// The turn of Transform::rotation alone: Rz * Ry * Rx (degrees).
Mat4 RotationMatrix(Vec3 degrees);
// A turn of `degrees` about `axis` (any length; right hand: counter-clockwise looking from its tip).
Mat4 AxisAngle(Vec3 axis, float degrees);
// The angles of a pure rotation (RotationMatrix's inverse). Every turn has two sets of angles (and any
// angle + 360): the one nearest `hint` is chosen, so values the user sees change smoothly while dragging.
Vec3 EulerFromRotation(const Mat4& r, Vec3 hint = {});
// In the parent's space: parentWorld * LocalMatrix(t) (the top level's parent is the identity).
inline Mat4 WorldMatrix(const Mat4& parentWorld, const Transform& t) { return parentWorld * LocalMatrix(t); }

// Where a thing is in the world, its parents included - shown read-only under its Transform.
RM_STRUCT(Label="ワールドでの値 (親を含む)")
struct WorldPlacement {
    RM_PROPERTY(ReadOnly, Label="位置") Vec3 position{0, 0, 0};
    RM_PROPERTY(ReadOnly, Label="回転 (度)") Vec3 rotation{0, 0, 0};
    RM_PROPERTY(ReadOnly, Label="大きさ", Tooltip="親の大きさが軸ごとに違い、しかも回っているときは、ゆがみが出るので近い値です") Vec3 scale{1, 1, 1};
};
// A world matrix taken apart into position, rotation and (positive) scale; a mirrored matrix gets
// a negative X scale. `hint`: as EulerFromRotation.
WorldPlacement Decompose(const Mat4& world, Vec3 hint = {});

}  // namespace rm::space3d
