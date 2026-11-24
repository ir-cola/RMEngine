#include "rm_engine/physics/runtime/Shapes.h"

#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::physics {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr int kCircle = 32;  // 円の線の分割数

Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Len(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 Abs(Vec3 v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }

}  // namespace

// 親の位置 `up` (null: 一番上) の下にある `e` の位置。false: 自分の位置は持たない。
bool PlaceUnder(const game_production::SceneEntity& e, const EntityPlace* up, EntityPlace& out) {
    out = up ? *up : EntityPlace{};
    out.parent3 = out.world3;
    out.parent2 = out.world2;
    out.parentOrder = up ? up->order : 0;
    out.order = out.parentOrder;
    if (const game_production::Component* c = e.FindComponent(reflect::TypeOf<space3d::Transform>().name); c && c->Known()) {
        out.flat = false;
        out.world3 = space3d::WorldMatrix(out.parent3, *static_cast<const space3d::Transform*>(c->value.Get()));
        return true;
    }
    if (const game_production::Component* c = e.FindComponent(reflect::TypeOf<space2d::Transform2D>().name); c && c->Known()) {
        const auto& t = *static_cast<const space2d::Transform2D*>(c->value.Get());
        out.flat = true;
        out.world2 = space2d::WorldMatrix(out.parent2, t);
        out.order = out.parentOrder + t.order;
        return true;
    }
    // 自分の位置は持たない。子は親のいる所にいる。
    out.world3 = out.parent3;
    out.world2 = out.parent2;
    return false;
}

Pose PoseOf(const space3d::Mat4& world) {
    Pose p;
    p.position = {world.m[3][0], world.m[3][1], world.m[3][2]};
    Vec3 axis[3];
    float len[3];
    for (int c = 0; c < 3; ++c) {
        axis[c] = {world.m[c][0], world.m[c][1], world.m[c][2]};
        len[c] = Len(axis[c]);
    }
    p.scale = {len[0], len[1], len[2]};
    // 回転: 軸を長さ 1 で互いに直角にしたもの (拡大率 0 の軸は、本来その軸になるもの)。
    Vec3 x = len[0] > 1e-12f ? Mul(axis[0], 1 / len[0]) : Vec3{1, 0, 0};
    Vec3 y = len[1] > 1e-12f ? Mul(axis[1], 1 / len[1]) : Vec3{0, 1, 0};
    Vec3 z = space3d::Normalize(space3d::Cross(x, y));
    if (Len(z) < 0.5f) z = {0, 0, 1};
    y = space3d::Normalize(space3d::Cross(z, x));
    // 反転した行列 (負の拡大率): 回転は右手系のまま残し、反転は拡大率に入れる。
    if (space3d::Dot(z, len[2] > 1e-12f ? Mul(axis[2], 1 / len[2]) : z) < 0) p.scale.z = -p.scale.z;
    const Vec3 cols[3] = {x, y, z};
    for (int c = 0; c < 3; ++c) {
        p.turn.m[c][0] = cols[c].x;
        p.turn.m[c][1] = cols[c].y;
        p.turn.m[c][2] = cols[c].z;
        p.turn.m[c][3] = 0;
    }
    return p;
}

Pose PoseOf(const space2d::Mat3& world) {
    Pose p;
    p.position = {world.tx, world.ty, 0};
    const float sx = std::sqrt(world.a * world.a + world.b * world.b);
    float sy = std::sqrt(world.c * world.c + world.d * world.d);
    if (space2d::Determinant(world) < 0) sy = -sy;  // 反転: 反転は Y の拡大率に入れる
    p.scale = {sx, sy, 1};
    const float angle = sx > 1e-12f ? std::atan2(world.b, world.a) : 0.f;
    p.turn = space3d::RotationMatrix({0, 0, angle * 180.f / kPi});
    return p;
}

float AngleOf(const Pose& pose) { return std::atan2(pose.turn.m[0][1], pose.turn.m[0][0]) * 180.f / kPi; }

ShapeDims DimsOf(const Collider& c, Vec3 scale) {
    const Vec3 s = Abs(scale);
    ShapeDims d;
    d.offset = {c.offset.x * scale.x, c.offset.y * scale.y, c.offset.z * scale.z};
    // (拡大率 0 だと Jolt の逆拡大率が無限大になる: 0.001 以上にし、符号は残す)
    auto least = [](float k) { return std::fabs(k) < 0.001f ? (k < 0 ? -0.001f : 0.001f) : k; };
    d.meshScale = {least(scale.x), least(scale.y), least(scale.z)};
    switch (c.shape) {
        case Shape::Box:
            d.kind = ShapeDims::Kind::Box;
            d.half = {std::max(c.size.x * s.x, 0.001f) * 0.5f, std::max(c.size.y * s.y, 0.001f) * 0.5f, std::max(c.size.z * s.z, 0.001f) * 0.5f};
            break;
        case Shape::Sphere:
            d.kind = ShapeDims::Kind::Sphere;
            d.radius = std::max(c.radius * std::max({s.x, s.y, s.z}), 0.001f);
            d.halfHeight = 0;
            break;
        case Shape::Capsule: {
            d.kind = ShapeDims::Kind::Capsule;
            d.radius = std::max(c.radius * std::max(s.x, s.z), 0.001f);
            d.halfHeight = std::max(c.height * s.y * 0.5f - d.radius, 0.f);  // まっすぐな部分 (なし: 球)
            break;
        }
        case Shape::Mesh: d.kind = ShapeDims::Kind::Mesh; break;
        case Shape::Cylinder:
            d.kind = ShapeDims::Kind::Cylinder;
            d.radius = std::max(c.radius * std::max(s.x, s.z), 0.001f);
            d.halfHeight = std::max(c.height * s.y * 0.5f, 0.001f);
            break;
    }
    return d;
}

ShapeDims DimsOf(const Collider2D& c, Vec3 scale) {
    const Vec3 s = Abs(scale);
    ShapeDims d;
    d.offset = {c.offset.x * scale.x, c.offset.y * scale.y, 0};
    switch (c.shape) {
        case Shape2D::Box:
            d.kind = ShapeDims::Kind::Box;
            d.half = {std::max(c.size.x * s.x, 0.001f) * 0.5f, std::max(c.size.y * s.y, 0.001f) * 0.5f, kPlaneHalfDepth};
            break;
        case Shape2D::Circle:
            d.kind = ShapeDims::Kind::Sphere;
            d.radius = std::max(c.radius * std::max(s.x, s.y), 0.001f);
            d.halfHeight = 0;
            break;
        case Shape2D::Capsule:
            d.kind = ShapeDims::Kind::Capsule;
            d.radius = std::max(c.radius * s.x, 0.001f);
            d.halfHeight = std::max(c.height * s.y * 0.5f - d.radius, 0.f);
            break;
    }
    return d;
}

ShapeDims DimsOf(const Character& c) {
    ShapeDims d;
    d.kind = ShapeDims::Kind::Capsule;
    d.radius = std::max(c.radius, 0.01f);
    const float height = std::max(c.height, d.radius * 2 + 0.01f);
    d.halfHeight = height * 0.5f - d.radius;
    d.offset = {0, height * 0.5f, 0};  // エンティティの位置に立つ
    return d;
}

void Outline(const ShapeDims& dims, const Pose& pose, bool flat, std::vector<Segment>& out) {
    // 形の空間 -> ワールド: エンティティと同じ向きに回し、エンティティとオフセットの所へ動かす。
    auto at = [&](Vec3 local) {
        const Vec3 p = Add(local, dims.offset);
        return Add(pose.position, space3d::TransformDirection(pose.turn, p));
    };
    auto line = [&](Vec3 a, Vec3 b) { out.push_back({at(a), at(b)}); };
    // 軸 u と v の平面で `center` を中心にした半径 `radius` の円 (一部だけ: `from` .. `to` 回転分)。
    auto arc = [&](Vec3 center, Vec3 u, Vec3 v, float radius, float from, float to) {
        const int n = std::max(2, static_cast<int>(kCircle * (to - from)));
        for (int i = 0; i < n; ++i) {
            const float t0 = (from + (to - from) * static_cast<float>(i) / n) * 2 * kPi;
            const float t1 = (from + (to - from) * static_cast<float>(i + 1) / n) * 2 * kPi;
            line(Add(center, Add(Mul(u, std::cos(t0) * radius), Mul(v, std::sin(t0) * radius))),
                 Add(center, Add(Mul(u, std::cos(t1) * radius), Mul(v, std::sin(t1) * radius))));
        }
    };
    const Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1}, O{0, 0, 0};
    switch (dims.kind) {
        case ShapeDims::Kind::Box: {
            const Vec3 h = dims.half;
            if (flat) {  // 2D の箱: 平面上の長方形
                const Vec3 k[4] = {{-h.x, -h.y, 0}, {h.x, -h.y, 0}, {h.x, h.y, 0}, {-h.x, h.y, 0}};
                for (int i = 0; i < 4; ++i) line(k[i], k[(i + 1) % 4]);
                break;
            }
            Vec3 k[8];
            for (int i = 0; i < 8; ++i) k[i] = {(i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z};
            const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& e : edges) line(k[e[0]], k[e[1]]);
            break;
        }
        case ShapeDims::Kind::Sphere:
            arc(O, X, Y, dims.radius, 0, 1);
            if (!flat) {
                arc(O, Y, Z, dims.radius, 0, 1);
                arc(O, Z, X, dims.radius, 0, 1);
            }
            break;
        case ShapeDims::Kind::Capsule: {
            const float r = dims.radius, hh = dims.halfHeight;
            const Vec3 top{0, hh, 0}, bottom{0, -hh, 0};
            // 正面 (XY) から見た輪郭: 半円 2 つと両側の 2 辺。
            arc(top, X, Y, r, 0, 0.5f);
            arc(bottom, X, Y, r, 0.5f, 1);
            line({r, hh, 0}, {r, -hh, 0});
            line({-r, hh, 0}, {-r, -hh, 0});
            if (!flat) {  // ... 横 (ZY) から見たもの、と端が側面に接する所の輪
                arc(top, Z, Y, r, 0, 0.5f);
                arc(bottom, Z, Y, r, 0.5f, 1);
                line({0, hh, r}, {0, -hh, r});
                line({0, hh, -r}, {0, -hh, -r});
                arc(top, X, Z, r, 0, 1);
                arc(bottom, X, Z, r, 0, 1);
            }
            break;
        }
        case ShapeDims::Kind::Cylinder: {
            const float r = dims.radius, hh = dims.halfHeight;
            if (flat) {  // (2D では使わない: 正面から見た長方形)
                line({-r, -hh, 0}, {r, -hh, 0});
                line({r, -hh, 0}, {r, hh, 0});
                line({r, hh, 0}, {-r, hh, 0});
                line({-r, hh, 0}, {-r, -hh, 0});
                break;
            }
            // 上と下の輪と、4 本の縦の線。
            arc({0, hh, 0}, X, Z, r, 0, 1);
            arc({0, -hh, 0}, X, Z, r, 0, 1);
            for (const Vec3 side : {Vec3{r, 0, 0}, Vec3{-r, 0, 0}, Vec3{0, 0, r}, Vec3{0, 0, -r}}) line({side.x, hh, side.z}, {side.x, -hh, side.z});
            break;
        }
        case ShapeDims::Kind::Mesh: break;  // モデル自身が形になる (シーンビューがもう描いている)
    }
}

}  // namespace rm::physics
