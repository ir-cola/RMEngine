#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace rm::space3d {

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;

// (x, y, z, w) = M * (p, w)
void Transform4(const Mat4& m, float x, float y, float z, float w, float out[4]) {
    for (int row = 0; row < 4; ++row) out[row] = m.m[0][row] * x + m.m[1][row] * y + m.m[2][row] * z + m.m[3][row] * w;
}

}  // namespace

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
Vec3 Normalize(Vec3 v) {
    const float l = Length(v);
    return l > 0 ? v * (1.f / l) : Vec3{};
}

Vec3 TransformDirection(const Mat4& m, Vec3 d) {
    float o[4];
    Transform4(m, d.x, d.y, d.z, 0.f, o);
    return {o[0], o[1], o[2]};
}

std::optional<Mat4> Inverse(const Mat4& mat) {
    // Cofactors of the 16 numbers as stored (the inverse of the stored layout is the stored inverse).
    const float* m = &mat.m[0][0];
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return std::nullopt;
    Mat4 out;
    float* o = &out.m[0][0];
    for (int i = 0; i < 16; ++i) o[i] = inv[i] / det;
    return out;
}

Mat4 Perspective(float fovYDegrees, float aspect, float nearZ, float farZ) {
    const float f = 1.f / std::tan(fovYDegrees * kDeg * 0.5f);
    Mat4 p;
    p.m[0][0] = f / std::max(aspect, 1e-6f);
    p.m[1][1] = f;
    p.m[2][2] = farZ / (nearZ - farZ);
    p.m[2][3] = -1.f;
    p.m[3][2] = nearZ * farZ / (nearZ - farZ);
    p.m[3][3] = 0.f;
    return p;
}

Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = Normalize(target - eye);
    Vec3 s = Normalize(Cross(f, up));
    if (Length(s) == 0) s = {1, 0, 0};  // looking straight up or down
    const Vec3 u = Cross(s, f);
    Mat4 v;
    v.m[0][0] = s.x, v.m[1][0] = s.y, v.m[2][0] = s.z;
    v.m[0][1] = u.x, v.m[1][1] = u.y, v.m[2][1] = u.z;
    v.m[0][2] = -f.x, v.m[1][2] = -f.y, v.m[2][2] = -f.z;
    v.m[3][0] = -Dot(s, eye), v.m[3][1] = -Dot(u, eye), v.m[3][2] = Dot(f, eye);
    return v;
}

std::optional<Vec3> ProjectToNdc(const Mat4& vp, Vec3 p) {
    float o[4];
    Transform4(vp, p.x, p.y, p.z, 1.f, o);
    if (o[3] <= 1e-6f) return std::nullopt;
    return Vec3{o[0] / o[3], o[1] / o[3], o[2] / o[3]};
}

Ray ScreenRay(const Mat4& inv, float ndcX, float ndcY) {
    float n[4], f[4];
    Transform4(inv, ndcX, ndcY, 0.f, 1.f, n);
    Transform4(inv, ndcX, ndcY, 1.f, 1.f, f);
    const Vec3 nearP{n[0] / n[3], n[1] / n[3], n[2] / n[3]};
    const Vec3 farP{f[0] / f[3], f[1] / f[3], f[2] / f[3]};
    return {nearP, Normalize(farP - nearP)};
}

std::optional<float> IntersectBox(const Ray& ray, Vec3 mn, Vec3 mx) {
    // Slabs: the ray is inside the box between the latest entry and the earliest exit.
    float tMin = 0.f, tMax = std::numeric_limits<float>::infinity();
    const float o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float d[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
    const float lo[3] = {mn.x, mn.y, mn.z};
    const float hi[3] = {mx.x, mx.y, mx.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-12f) {
            if (o[a] < lo[a] || o[a] > hi[a]) return std::nullopt;  // parallel and outside
            continue;
        }
        float t1 = (lo[a] - o[a]) / d[a], t2 = (hi[a] - o[a]) / d[a];
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return std::nullopt;
    }
    return tMin;
}

std::optional<float> IntersectPlacedCube(const Ray& ray, const Mat4& world) {
    const auto inv = Inverse(world);
    if (!inv) return std::nullopt;
    // In the cube's own space the same t reaches the same point (the ray is moved, not re-measured).
    const Ray local{TransformPoint(*inv, ray.origin), TransformDirection(*inv, ray.dir)};
    return IntersectBox(local, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f});
}

Vec3 OrbitView::Forward() const {
    const float y = yaw * kDeg, p = pitch * kDeg;
    return {-std::sin(y) * std::cos(p), -std::sin(p), -std::cos(y) * std::cos(p)};
}
Vec3 OrbitView::Right() const { return Normalize(Cross(Forward(), {0, 1, 0})); }
Vec3 OrbitView::Up() const { return Cross(Right(), Forward()); }
float OrbitView::WorldPerPixel(Vec3 p, float viewHeight) const {
    const float depth = Dot(p - Eye(), Forward());
    if (depth <= 1e-4f) return 0.f;
    return 2.f * depth * std::tan(fovY * kDeg * 0.5f) / std::max(viewHeight, 1.f);
}

Mat4 OrbitView::ProjectionMatrix(float aspect) const {
    // The near plane follows the distance, so small and large scenes both keep their depth precision.
    const float nearZ = std::clamp(distance * 0.01f, 0.01f, 1.f);
    return Perspective(fovY, aspect, nearZ, std::max(1000.f, distance * 50.f));
}

void Look(OrbitView& v, float dYaw, float dPitch) {
    const Vec3 eye = v.Eye();
    v.yaw += dYaw;
    v.pitch = std::clamp(v.pitch + dPitch, -89.f, 89.f);
    v.pivot = eye + v.Forward() * v.distance;
}

void Orbit(OrbitView& v, float dYaw, float dPitch) {
    v.yaw += dYaw;
    v.pitch = std::clamp(v.pitch + dPitch, -89.f, 89.f);
}

void Pan(OrbitView& v, float dx, float dy, float viewHeight) {
    // One pixel at the pivot's distance: the picture follows the mouse there.
    const float perPixel = 2.f * v.distance * std::tan(v.fovY * kDeg * 0.5f) / std::max(viewHeight, 1.f);
    v.pivot = v.pivot - v.Right() * (dx * perPixel) + v.Up() * (dy * perPixel);
}

void Dolly(OrbitView& v, float notches) { v.distance = std::clamp(v.distance * std::pow(0.85f, notches), 0.05f, 10000.f); }

void Fly(OrbitView& v, Vec3 move) { v.pivot = v.pivot + v.Right() * move.x + Vec3{0, 1, 0} * move.y + v.Forward() * move.z; }

void Frame(OrbitView& v, Vec3 target, float radius) {
    v.pivot = target;
    v.distance = std::max(radius * 2.5f, 2.f);
}

}  // namespace rm::space3d
