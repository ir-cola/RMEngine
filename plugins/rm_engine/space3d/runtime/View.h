#pragma once
// 3D 空間: looking at space - vectors, the camera's matrices, rays and what they hit.
// Pure - tested in tests/TestSpace3d.cpp.
//
// The world is right-handed with Y up (X right, Z towards the viewer); a camera looks along its -Z.
// Projection matrices map depth to 0 (near) .. 1 (far), as Direct3D wants.

#include "rm_engine/space3d/runtime/Transform.h"

#include <optional>

namespace rm::space3d {

Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, float s);
float Dot(Vec3 a, Vec3 b);
Vec3 Cross(Vec3 a, Vec3 b);
float Length(Vec3 v);
Vec3 Normalize(Vec3 v);  // (0,0,0) stays (0,0,0)

// p' = M * (p, w): w = 1 for a point (moves), 0 for a direction (does not).
Vec3 TransformDirection(const Mat4& m, Vec3 d);
std::optional<Mat4> Inverse(const Mat4& m);  // nullopt when it cannot be inverted (a scale of 0)

Mat4 Perspective(float fovYDegrees, float aspect, float nearZ, float farZ);
Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up);

struct Ray {
    Vec3 origin;
    Vec3 dir;  // normalized
};
// The ray through a point of the screen: ndc x / y from -1 (left / bottom) to 1 (right / top).
Ray ScreenRay(const Mat4& inverseViewProjection, float ndcX, float ndcY);
// Distance along the ray to the box (from the ray's origin), nullopt when it misses.
std::optional<float> IntersectBox(const Ray& ray, Vec3 min, Vec3 max);
// The unit cube (-0.5 .. 0.5) placed by `world` (moved, turned, scaled): distance in world units.
std::optional<float> IntersectPlacedCube(const Ray& ray, const Mat4& world);

// Where a point is on the screen: ndc x / y (-1 .. 1, y up) and its depth (0 near .. 1 far);
// nullopt when it is behind the eye.
std::optional<Vec3> ProjectToNdc(const Mat4& viewProjection, Vec3 p);

// An editing camera that turns around a point it looks at (the pivot). Reflected so the editor can
// keep it (the user's way of looking, not project data); never shown in an inspector.
RM_STRUCT()
struct OrbitView {
    RM_PROPERTY() Vec3 pivot{0, 0, 0};
    RM_PROPERTY(Min=-100000, Max=100000) float yaw = 35.f;  // degrees around Y; 0 looks along -Z
    RM_PROPERTY(Min=-89, Max=89) float pitch = 25.f;        // degrees; positive looks down
    RM_PROPERTY(Min=0.05, Max=10000) float distance = 9.f;  // from the eye to the pivot
    RM_PROPERTY(Min=10, Max=120) float fovY = 60.f;         // degrees

    Vec3 Forward() const;  // where the eye looks
    Vec3 Right() const;
    Vec3 Up() const;
    Vec3 Eye() const { return pivot - Forward() * distance; }
    Mat4 ViewMatrix() const { return LookAt(Eye(), pivot, {0, 1, 0}); }
    Mat4 ProjectionMatrix(float aspect) const;
    // How long one pixel of a picture `viewHeight` pixels tall is at point p (things drawn at the same
    // size on screen whatever their distance: the gizmo). 0 when p is behind the eye.
    float WorldPerPixel(Vec3 p, float viewHeight) const;
};

// The camera moves (the editor decides which mouse / keys do which):
void Look(OrbitView& v, float dYaw, float dPitch);       // turn the head: the eye stays, the pivot moves
void Orbit(OrbitView& v, float dYaw, float dPitch);      // go around the pivot: the pivot stays
void Pan(OrbitView& v, float dxPixels, float dyPixels, float viewHeightPixels);  // slide with the picture
void Dolly(OrbitView& v, float notches);                 // nearer to the pivot (> 0) or away
void Fly(OrbitView& v, Vec3 move);                       // x = right, y = up (world), z = forward; eye and pivot move
void Frame(OrbitView& v, Vec3 target, float radius);     // look at a thing from a distance that fits it

}  // namespace rm::space3d
