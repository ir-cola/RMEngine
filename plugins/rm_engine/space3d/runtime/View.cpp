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
    // Cofactors of the 16 numbers as stored (the inverse of the stored layout is the stored inverse),
    // in double: a thing 1e-5 across has a determinant of 1e-15, too close to float's floor.
    double m[16];
    for (int i = 0; i < 16; ++i) m[i] = (&mat.m[0][0])[i];
    double inv[16];
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
    const double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    // Not invertible = the columns (nearly) lie in a smaller space - told by the determinant against
    // the largest it could be for columns that long (Hadamard: |det| <= the product of their lengths).
    // A small thing (all columns short) is fine; only a flattened one is not.
    double longest = 1;
    for (int c = 0; c < 4; ++c) longest *= std::sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2] + m[c * 4 + 3] * m[c * 4 + 3]);
    if (!(longest > 0) || !(std::fabs(det) > 1e-7 * longest)) return std::nullopt;  // NaN fails too
    Mat4 out;
    float* o = &out.m[0][0];
    for (int i = 0; i < 16; ++i) o[i] = static_cast<float>(inv[i] / det);
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

Mat4 Orthographic(float l, float r, float b, float t, float n, float f) {
    Mat4 o;
    o.m[0][0] = 2.f / (r - l);
    o.m[1][1] = 2.f / (t - b);
    o.m[2][2] = 1.f / (n - f);
    o.m[3][0] = (l + r) / (l - r);
    o.m[3][1] = (t + b) / (b - t);
    o.m[3][2] = n / (n - f);
    return o;
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

Mat4 ViewFromWorld(const Mat4& world) {
    const Vec3 eye = TransformPoint(world, {0, 0, 0});
    Vec3 forward = Normalize(TransformDirection(world, {0, 0, -1}));
    Vec3 up = Normalize(TransformDirection(world, {0, 1, 0}));
    if (Length(forward) == 0) forward = {0, 0, -1};  // a scale of 0
    if (Length(up) == 0 || std::fabs(Dot(forward, up)) > 0.9999f) up = std::fabs(forward.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    return LookAt(eye, eye + forward, up);
}

std::optional<std::array<Vec3, 8>> FrustumCorners(const Mat4& viewProjection) {
    const auto inv = Inverse(viewProjection);
    if (!inv) return std::nullopt;
    std::array<Vec3, 8> out;
    const float xs[4] = {-1, 1, 1, -1}, ys[4] = {-1, -1, 1, 1};
    for (int i = 0; i < 8; ++i) {
        float o[4];
        Transform4(*inv, xs[i % 4], ys[i % 4], i < 4 ? 0.f : 1.f, 1.f, o);
        if (std::fabs(o[3]) < 1e-12f) return std::nullopt;
        out[static_cast<size_t>(i)] = {o[0] / o[3], o[1] / o[3], o[2] / o[3]};
    }
    return out;
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
    return IntersectPlacedBox(ray, world, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f});
}

std::optional<float> IntersectPlacedBox(const Ray& ray, const Mat4& world, Vec3 mn, Vec3 mx) {
    const auto inv = Inverse(world);
    if (!inv) return std::nullopt;
    // In the box's own space the same t reaches the same point (the ray is moved, not re-measured).
    const Ray local{TransformPoint(*inv, ray.origin), TransformDirection(*inv, ray.dir)};
    return IntersectBox(local, mn, mx);
}

Bounds WorldBounds(const Mat4& world, Vec3 mn, Vec3 mx) {
    Bounds b;
    for (int i = 0; i < 8; ++i) {
        const Vec3 p = TransformPoint(world, {(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z});
        if (i == 0) {
            b.min = b.max = p;
            continue;
        }
        b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
        b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
    }
    return b;
}

bool BoxInView(const Mat4& viewProjection, const Bounds& box) {
    unsigned all = 0x3F;  // the sides every corner is outside of so far
    for (int i = 0; i < 8; ++i) {
        float c[4];
        Transform4(viewProjection, (i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z, 1.f, c);
        unsigned out = 0;
        if (c[0] < -c[3]) out |= 1;
        if (c[0] > c[3]) out |= 2;
        if (c[1] < -c[3]) out |= 4;
        if (c[1] > c[3]) out |= 8;
        if (c[2] < 0) out |= 16;  // depth 0..1 (Direct3D): nearer than the near plane
        if (c[2] > c[3]) out |= 32;
        all &= out;
        if (all == 0) return true;
    }
    return false;
}

Frustum FrustumOf(const Mat4& m) {
    // Clip space c = M p: row k of M is (m[0][k], m[1][k], m[2][k], m[3][k]). Inside: -w <= x <= w,
    // -w <= y <= w, 0 <= z <= w (BoxInView's sides, in its order).
    auto row = [&](int k) { return std::array<float, 4>{m.m[0][k], m.m[1][k], m.m[2][k], m.m[3][k]}; };
    const auto x = row(0), y = row(1), z = row(2), w = row(3);
    Frustum f;
    for (int i = 0; i < 4; ++i) {
        f.planes[0][i] = w[i] + x[i];  // x >= -w
        f.planes[1][i] = w[i] - x[i];  // x <= w
        f.planes[2][i] = w[i] + y[i];
        f.planes[3][i] = w[i] - y[i];
        f.planes[4][i] = z[i];         // z >= 0 (depth 0..1)
        f.planes[5][i] = w[i] - z[i];
    }
    return f;
}

bool BoxInFrustum(const Frustum& f, const Bounds& b) {
    for (const auto& p : f.planes) {
        // The corner farthest inside this plane: if even it is outside, every corner is.
        const float x = p[0] >= 0 ? b.max.x : b.min.x, y = p[1] >= 0 ? b.max.y : b.min.y, z = p[2] >= 0 ? b.max.z : b.min.z;
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
    }
    return true;
}

Mat4 DirectionalShadowMatrix(Vec3 direction, const Bounds& casters) {
    Vec3 dir = Normalize(direction);
    if (Length(dir) == 0) dir = {0, -1, 0};
    const Vec3 center = (casters.min + casters.max) * 0.5f;
    const float radius = std::max(Length(casters.max - casters.min) * 0.5f, 0.01f);
    // Looking straight down (or up) the usual up is the line of sight: another one is taken.
    const Vec3 up = std::fabs(dir.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const Mat4 view = LookAt(center - dir * (radius * 2.f), center, up);
    // The box's corners as the light sees them: the tightest fit (a little wider, so edges are not cut).
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    for (int i = 0; i < 8; ++i) {
        const Vec3 p = TransformPoint(view, {(i & 1) ? casters.max.x : casters.min.x, (i & 2) ? casters.max.y : casters.min.y,
                                             (i & 4) ? casters.max.z : casters.min.z});
        const float v[3] = {p.x, p.y, p.z};
        for (int a = 0; a < 3; ++a) {
            lo[a] = i == 0 ? v[a] : std::min(lo[a], v[a]);
            hi[a] = i == 0 ? v[a] : std::max(hi[a], v[a]);
        }
    }
    const float pad = radius * 0.02f + 0.01f;
    // View space looks along -z: the nearest corner has the largest z.
    return Orthographic(lo[0] - pad, hi[0] + pad, lo[1] - pad, hi[1] + pad, -hi[2] - pad, -lo[2] + pad) * view;
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

std::vector<size_t> BackToFront(std::span<const Vec3> centers, Vec3 eye) {
    std::vector<size_t> order(centers.size());
    std::vector<float> far(centers.size());
    for (size_t i = 0; i < centers.size(); ++i) {
        order[i] = i;
        const Vec3 d = centers[i] - eye;
        far[i] = Dot(d, d);
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return far[a] > far[b]; });
    return order;
}

}  // namespace rm::space3d
