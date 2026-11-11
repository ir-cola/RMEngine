#pragma once
// 2D 空間: where a thing is on a plane, and the math of it (looking at it is View2D.h, drawing it
// Renderer2D.h - the same plugin). Shared by every software that has a 2D space (the game through the
// connector "ゲーム制作 × 2D", the illustration software later) - design RME_PLATFORM 4-b.
// Pure: no drawing, no files - tested in tests/TestSpace2d.cpp.
//
// The plane has X to the right and Y up (as in Unity's 2D and in math); a positive angle turns
// counter-clockwise. The screen's Y goes down: only the camera (View2D.h) turns it over.
// Instead of depth, a thing has a 並び順 (order): larger is in front. A child's order adds to its
// parent's (as a 3D child's Z adds to its parent's), so a group moves to the front together.

#include "core/reflect/Reflect.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace rm::space2d {

RM_STRUCT(Label="Transform 2D (位置・回転・大きさ・並び順)")
struct Transform2D {
    RM_PROPERTY(Edit, Label="位置") Vec2 position{0, 0};
    RM_PROPERTY(Edit, Label="回転 (度)", Tooltip="反時計回りが + です") float rotation = 0;
    RM_PROPERTY(Edit, Label="大きさ") Vec2 scale{1, 1};
    RM_PROPERTY(Edit, Label="並び順", Tooltip="大きいほど手前に描きます (奥行きの代わり)。子は親の並び順に足した所に描きます", Min=-100000, Max=100000)
    int order = 0;
};

Vec2 operator+(Vec2 a, Vec2 b);
Vec2 operator-(Vec2 a, Vec2 b);
Vec2 operator*(Vec2 a, float s);
float Dot(Vec2 a, Vec2 b);
float Cross(Vec2 a, Vec2 b);  // a.x * b.y - a.y * b.x: > 0 when b is counter-clockwise from a
float Length(Vec2 v);
Vec2 Normalize(Vec2 v);  // (0,0) stays (0,0)
Vec2 Rotate(Vec2 v, float degrees);

// A 2D affine matrix (column vectors): p' = (a*x + c*y + tx, b*x + d*y + ty).
// (a, b) is where the X axis goes, (c, d) the Y axis, (tx, ty) the origin.
struct Mat3 {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};

Mat3 operator*(const Mat3& m, const Mat3& n);  // m after n
Vec2 TransformPoint(const Mat3& m, Vec2 p);
Vec2 TransformDirection(const Mat3& m, Vec2 v);  // no move
float Determinant(const Mat3& m);                // < 0: mirrored
std::optional<Mat3> Inverse(const Mat3& m);      // nullopt when it cannot be inverted (a scale of 0)

Mat3 Translation(Vec2 t);
Mat3 Rotation(float degrees);
Mat3 Scaling(Vec2 s);

// Scale, then turn, then move: T * R * S.
Mat3 LocalMatrix(const Transform2D& t);
// In the parent's space: parentWorld * LocalMatrix(t) (the top level's parent is the identity).
inline Mat3 WorldMatrix(const Mat3& parentWorld, const Transform2D& t) { return parentWorld * LocalMatrix(t); }

// Where a thing is in the world, its parents included - the inspector's ワールド view of a Transform 2D.
RM_STRUCT(Label="ワールドでの値 (親を含む)")
struct WorldPlacement2D {
    RM_PROPERTY(Edit, Label="位置") Vec2 position{0, 0};
    RM_PROPERTY(Edit, Label="回転 (度)", Tooltip="反時計回りが + です") float rotation = 0;
    RM_PROPERTY(Edit, Label="大きさ", Tooltip="親の大きさが軸ごとに違い、しかも回っているときは、ゆがみが出るので近い値です") Vec2 scale{1, 1};
    RM_PROPERTY(Edit, Label="並び順", Tooltip="親の並び順を足した値です", Min=-100000, Max=100000) int order = 0;
};
// A world matrix taken apart into position, rotation (near `hintDegrees`: + 360s, so numbers do not
// jump while dragging) and scale; a mirrored matrix gets a negative Y scale. `order` is not in a matrix: 0.
WorldPlacement2D Decompose(const Mat3& world, float hintDegrees = 0);
// The Transform 2D that puts a thing at `want` in the world under a parent at `parentWorld` with the
// order `parentOrder`, changing only what `want` changes from where `current` is now (typing the world
// X position moves, never turns). Exact unless a parent is scaled unevenly and turned (then the nearest).
Transform2D FromWorld(const Transform2D& current, const Mat3& parentWorld, int parentOrder, const WorldPlacement2D& want);

// --- What is drawn in front of what ---------------------------------------------------------
// One thing to draw: its world order (Transform 2D's, parents' added), the order of its own drawing
// (a sprite's 並び順 within the same world order) and where it is in the tree (the last tie-break:
// later in the 階層 = in front, as in Unity).
struct DrawKey {
    int order = 0;
    int subOrder = 0;
    size_t tree = 0;
};
// The indices of `keys` from the back (drawn first) to the front.
std::vector<size_t> DrawOrder(std::span<const DrawKey> keys);

}  // namespace rm::space2d
