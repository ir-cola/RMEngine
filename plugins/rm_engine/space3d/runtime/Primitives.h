#pragma once
// 3D 空間 - simple shapes made from numbers, without a file: 箱, 球, 円柱, カプセル, 平面. The box is
// what the renderer draws for a thing without a model (Box); ゲーム制作 × 3D's メッシュ draws any of
// them (its 形). Each stands around (0, 0, 0), Y up:
//
//   Box       1 x 1 x 1
//   Sphere    1 across (radius 0.5)
//   Cylinder  1 across, 1 high (a lid on each end)
//   Capsule   1 across, 2 high (a half sphere on each end)
//   Plane     10 x 10, flat at y = 0, its front up (a ground: what Unity's plane is)
//
// Normals point out (smooth on the round faces; the box's faces, the lids and the plane flat); the
// front of each triangle is counter-clockwise seen from outside; UV 0..1 over each face (round faces:
// once around). One part, no material (drawn in the draw's colour).
// Pure - tested in tests/TestSpace3d.cpp.

#include "rm_engine/space3d/runtime/Mesh.h"

namespace rm::space3d {

enum class Shape { Box, Sphere, Cylinder, Capsule, Plane };

MeshData ShapeMesh(Shape shape);

// Every triangle turned so its front (counter-clockwise) faces the way its corners' normals do.
void FaceOut(MeshData& mesh);

}  // namespace rm::space3d
