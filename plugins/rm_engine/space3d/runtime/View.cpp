#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace rm::space3d {

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;
// 平行投影の目は、少なくともこれだけピボットの手前に下がる (その前後のこの長さの物が見える)。
constexpr float kOrthoBack = 500.f;

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
    // 保存された 16 個の数の余因子 (保存された並びの逆行列は、保存された逆行列)。double で行う:
    // 1e-5 の幅のものは行列式が 1e-15 になり、float の下限に近すぎる。
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
    // 逆行列がない = 列が (ほぼ) 小さい空間に載っている。行列式を、その長さの列がとりうる最大と
    // 比べて判断する (Hadamard: |det| <= 列の長さの積)。小さいもの (列が全部短い) は大丈夫。
    // つぶれたものだけが駄目。
    double longest = 1;
    for (int c = 0; c < 4; ++c) longest *= std::sqrt(m[c * 4] * m[c * 4] + m[c * 4 + 1] * m[c * 4 + 1] + m[c * 4 + 2] * m[c * 4 + 2] + m[c * 4 + 3] * m[c * 4 + 3]);
    if (!(longest > 0) || !(std::fabs(det) > 1e-7 * longest)) return std::nullopt;  // NaN も弾く
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
    // 左手系: ビュー空間の +z が目の前 (2026-11-20 から。前は右手系で -z)。
    p.m[2][2] = farZ / (farZ - nearZ);
    p.m[2][3] = 1.f;
    p.m[3][2] = -nearZ * farZ / (farZ - nearZ);
    p.m[3][3] = 0.f;
    return p;
}

Mat4 Orthographic(float l, float r, float b, float t, float n, float f) {
    Mat4 o;
    o.m[0][0] = 2.f / (r - l);
    o.m[1][1] = 2.f / (t - b);
    o.m[2][2] = 1.f / (f - n);
    o.m[3][0] = (l + r) / (l - r);
    o.m[3][1] = (t + b) / (b - t);
    o.m[3][2] = -n / (f - n);
    return o;
}

Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
    // 左手系: 右 = 上 × 前、ビュー空間の +z が前。
    const Vec3 f = Normalize(target - eye);
    Vec3 s = Normalize(Cross(up, f));
    if (Length(s) == 0) s = {1, 0, 0};  // 真上か真下を向いている
    const Vec3 u = Cross(f, s);
    Mat4 v;
    v.m[0][0] = s.x, v.m[1][0] = s.y, v.m[2][0] = s.z;
    v.m[0][1] = u.x, v.m[1][1] = u.y, v.m[2][1] = u.z;
    v.m[0][2] = f.x, v.m[1][2] = f.y, v.m[2][2] = f.z;
    v.m[3][0] = -Dot(s, eye), v.m[3][1] = -Dot(u, eye), v.m[3][2] = -Dot(f, eye);
    return v;
}

Mat4 ViewFromWorld(const Mat4& world) {
    const Vec3 eye = TransformPoint(world, {0, 0, 0});
    Vec3 forward = Normalize(TransformDirection(world, {0, 0, 1}));
    Vec3 up = Normalize(TransformDirection(world, {0, 1, 0}));
    if (Length(forward) == 0) forward = {0, 0, 1};  // スケール 0
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
    // スラブ法: レイは、一番遅い入りと一番早い出の間でボックスの中にいる。
    float tMin = 0.f, tMax = std::numeric_limits<float>::infinity();
    const float o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float d[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
    const float lo[3] = {mn.x, mn.y, mn.z};
    const float hi[3] = {mx.x, mx.y, mx.z};
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(d[a]) < 1e-12f) {
            if (o[a] < lo[a] || o[a] > hi[a]) return std::nullopt;  // 平行で外側
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
    // ボックス自身の空間でも、同じ t が同じ点に届く (レイは動かしただけで、測り直さない)。
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
    unsigned all = 0x3F;  // これまでのところ、全部の角が外側にある面
    for (int i = 0; i < 8; ++i) {
        float c[4];
        Transform4(viewProjection, (i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y, (i & 4) ? box.max.z : box.min.z, 1.f, c);
        unsigned out = 0;
        if (c[0] < -c[3]) out |= 1;
        if (c[0] > c[3]) out |= 2;
        if (c[1] < -c[3]) out |= 4;
        if (c[1] > c[3]) out |= 8;
        if (c[2] < 0) out |= 16;  // 深度 0..1 (Direct3D): ニアプレーンより手前
        if (c[2] > c[3]) out |= 32;
        all &= out;
        if (all == 0) return true;
    }
    return false;
}

Frustum FrustumOf(const Mat4& m) {
    // クリップ空間 c = M p: M の行 k は (m[0][k], m[1][k], m[2][k], m[3][k])。内側: -w <= x <= w、
    // -w <= y <= w、0 <= z <= w (BoxInView の各面。その順)。
    auto row = [&](int k) { return std::array<float, 4>{m.m[0][k], m.m[1][k], m.m[2][k], m.m[3][k]}; };
    const auto x = row(0), y = row(1), z = row(2), w = row(3);
    Frustum f;
    for (int i = 0; i < 4; ++i) {
        f.planes[0][i] = w[i] + x[i];  // x >= -w
        f.planes[1][i] = w[i] - x[i];  // x <= w
        f.planes[2][i] = w[i] + y[i];
        f.planes[3][i] = w[i] - y[i];
        f.planes[4][i] = z[i];         // z >= 0 (深度 0..1)
        f.planes[5][i] = w[i] - z[i];
    }
    return f;
}

bool BoxInFrustum(const Frustum& f, const Bounds& b) {
    for (const auto& p : f.planes) {
        // この面の一番内側にある角: それすら外なら、全部の角が外。
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
    // 真下 (か真上) を向くと、普通の上はちょうど視線になる: 別のものを使う。
    const Vec3 up = std::fabs(dir.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const Mat4 view = LookAt(center - dir * (radius * 2.f), center, up);
    // 光から見たボックスの角: 一番ぴったり合わせる (縁が切れないよう少しだけ広げる)。
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
    // ビュー空間は +z を向く (左手系): 一番近い角の z が最小。
    return Orthographic(lo[0] - pad, hi[0] + pad, lo[1] - pad, hi[1] + pad, lo[2] - pad, hi[2] + pad) * view;
}

Vec3 OrbitView::Forward() const {
    const float y = yaw * kDeg, p = pitch * kDeg;
    // (yaw 0 は +Z を向く。yaw を増やすと左 (-X) へ向く: 前の右手系と同じ回り方なので、マウスの扱いと
    // 覚えた yaw はそのまま - 2026-11-20 の左手系への変更で、ピボットの Z だけが反転する)
    return {-std::sin(y) * std::cos(p), -std::sin(p), std::cos(y) * std::cos(p)};
}
Vec3 OrbitView::Right() const { return Normalize(Cross({0, 1, 0}, Forward())); }
Vec3 OrbitView::Up() const { return Cross(Forward(), Right()); }
float OrbitView::EyeDistance() const { return orthographic ? std::max(kOrthoBack, distance * 4.f) : distance; }
Vec3 OrbitView::EyeFor(Vec3 p) const { return orthographic ? p - Forward() * EyeDistance() : Eye(); }
float OrbitView::OrthoHalfHeight() const { return distance * std::tan(fovY * kDeg * 0.5f); }
float OrbitView::WorldPerPixel(Vec3 p, float viewHeight) const {
    if (orthographic) return 2.f * OrthoHalfHeight() / std::max(viewHeight, 1.f);
    const float depth = Dot(p - Eye(), Forward());
    if (depth <= 1e-4f) return 0.f;
    return 2.f * depth * std::tan(fovY * kDeg * 0.5f) / std::max(viewHeight, 1.f);
}

Mat4 OrbitView::ProjectionMatrix(float aspect) const {
    if (orthographic) {
        // 目はピボットの EyeDistance 手前: ピボットの前後に同じだけ (と少し) 見える。深さは線形なので
        // 遠い面が遠くても精度は落ちない。
        const float hh = OrthoHalfHeight(), hw = hh * std::max(aspect, 1e-3f);
        return Orthographic(-hw, hw, -hh, hh, 0.01f, EyeDistance() * 2.f);
    }
    // ニアプレーンは距離に合わせて動くので、小さいシーンでも大きいシーンでも深度の精度を保つ。
    const float nearZ = std::clamp(distance * 0.01f, 0.01f, 1.f);
    return Perspective(fovY, aspect, nearZ, std::max(1000.f, distance * 50.f));
}

void Look(OrbitView& v, float dYaw, float dPitch) {
    const Vec3 eye = v.Eye();
    v.yaw += dYaw;
    v.pitch = std::clamp(v.pitch + dPitch, -89.f, 89.f);
    v.pivot = eye + v.Forward() * v.EyeDistance();
}

void Orbit(OrbitView& v, float dYaw, float dPitch) {
    v.yaw += dYaw;
    v.pitch = std::clamp(v.pitch + dPitch, -89.f, 89.f);
}

void Pan(OrbitView& v, float dx, float dy, float viewHeight) {
    // ピボットの距離での 1 ピクセル: そこでは絵がマウスについてくる。
    const float perPixel = 2.f * v.distance * std::tan(v.fovY * kDeg * 0.5f) / std::max(viewHeight, 1.f);
    v.pivot = v.pivot - v.Right() * (dx * perPixel) + v.Up() * (dy * perPixel);
}

void Dolly(OrbitView& v, float notches) { v.distance = std::clamp(v.distance * std::pow(0.85f, notches), 0.05f, 10000.f); }

void ZoomAt(OrbitView& v, float notches, float ndcX, float ndcY, float aspect) {
    if (!v.orthographic) {
        Dolly(v, notches);
        return;
    }
    // マウスの下の点は、ピボットから画面の面に沿ってこれだけ離れている (大きさに比例する)。大きさが
    // k 倍になると、その点が同じ所に残るには、ピボットがその点へ (1 - k) だけ寄る。
    const float hh = v.OrthoHalfHeight();
    const Vec3 offset = v.Right() * (ndcX * hh * std::max(aspect, 1e-3f)) + v.Up() * (ndcY * hh);
    const float before = v.distance;
    Dolly(v, notches);
    v.pivot = v.pivot + offset * (1.f - v.distance / before);
}

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
