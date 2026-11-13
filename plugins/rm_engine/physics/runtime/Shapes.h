#pragma once
// 物理 - where a collider is in the world and how big (the entity's world scale taken in), written once:
// the physics world makes its bodies' shapes from it, and the scene view draws its lines from it.
// Pure - tested in tests/TestPhysics.cpp.

#include "rm_engine/game_production/runtime/Scene.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <vector>

namespace rm::physics {

// Where an entity is: its world matrix (3D, or on the plane) and its parent's (for writing back).
struct EntityPlace {
    bool flat = false;  // on the plane (a Transform 2D)
    space3d::Mat4 world3, parent3;
    space2d::Mat3 world2, parent2;
    int parentOrder = 0;  // 2D: the parents' 並び順 added up
    int order = 0;        // ... and its own added
};
// The place of `e` under its parent's place `up` (null: at the top). False: it has no place of its own
// (no Transform / Transform 2D): `out` is then where its children are (the parent's place).
bool PlaceUnder(const game_production::SceneEntity& e, const EntityPlace* up, EntityPlace& out);

// A world matrix taken apart: where, which way (a pure turn, columns of length 1) and how big.
struct Pose {
    Vec3 position{0, 0, 0};
    space3d::Mat4 turn;  // rotation only
    Vec3 scale{1, 1, 1};
};
Pose PoseOf(const space3d::Mat4& world);
// A 2D world matrix on the plane: z = 0, turned about Z.
Pose PoseOf(const space2d::Mat3& world);
// The turn's angle about Z (degrees, counter-clockwise +) of a 2D pose.
float AngleOf(const Pose& pose);

// A collider's shape as the physics makes it: half sizes in the world (the entity's scale taken in) and
// where its middle is from the entity (in the entity's turned axes).
struct ShapeDims {
    enum class Kind { Box, Sphere, Capsule, Mesh } kind = Kind::Box;
    Vec3 half{0.5f, 0.5f, 0.5f};  // box: half the size on each axis; also a 2D box's (z: kPlaneHalfDepth)
    float radius = 0.5f;           // sphere / capsule / circle
    float halfHeight = 0.5f;       // capsule: half its straight part (Y); 0: a sphere
    Vec3 offset{0, 0, 0};          // from the entity, scaled
    Vec3 meshScale{1, 1, 1};       // mesh: the entity's scale (the model's own points times this)
};
// 2D bodies are this thick (half: Z -0.5 .. 0.5): every one is on the plane z = 0, so they meet like flat ones.
inline constexpr float kPlaneHalfDepth = 0.5f;
ShapeDims DimsOf(const Collider& c, Vec3 scale);
ShapeDims DimsOf(const Collider2D& c, Vec3 scale);
// A character's body: a capsule standing on the entity's position (its feet).
ShapeDims DimsOf(const Character& c);

// The lines of a shape placed at `pose` (pairs of points): what the scene view draws.
struct Segment {
    Vec3 a, b;
};
void Outline(const ShapeDims& dims, const Pose& pose, bool flat, std::vector<Segment>& out);

}  // namespace rm::physics
