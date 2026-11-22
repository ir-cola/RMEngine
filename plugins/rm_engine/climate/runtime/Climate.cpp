#include "rm_engine/climate/runtime/Climate.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <unordered_map>

namespace rm::climate {

namespace {

float Finite(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

float Length3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

// 3D の Transform の列 c (軸) の長さ。
float AxisLength(const space3d::Mat4& m, int c) { return Length3(m.m[c][0], m.m[c][1], m.m[c][2]); }

}  // namespace

float ZoneDepth(const ZoneBody& body, Vec3 p) {
    const Vec3 h = body.half;
    switch (body.kind) {
        case ZoneBody::Kind::Box: return std::min({h.x - std::fabs(p.x), h.y - std::fabs(p.y), h.z - std::fabs(p.z)});
        case ZoneBody::Kind::Cylinder: return std::min(h.x - std::sqrt(p.x * p.x + p.z * p.z), h.y - std::fabs(p.y));
        case ZoneBody::Kind::Rect: return std::min(h.x - std::fabs(p.x), h.y - std::fabs(p.y));
        case ZoneBody::Kind::Circle: return h.x - std::sqrt(p.x * p.x + p.y * p.y);
    }
    return -1.f;
}

float ZoneWeight(float depth, float border) {
    if (!std::isfinite(depth) || depth < 0) return 0.f;
    if (!(border > 0)) return 1.f;
    const float t = std::clamp(depth / border, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

ClimateHere Blend(ClimateHere outside, std::span<const ZoneEffect> zones) {
    std::vector<size_t> order(zones.size());
    std::iota(order.begin(), order.end(), size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return zones[a].priority < zones[b].priority; });
    ClimateHere c = outside;
    for (const size_t i : order) {
        const ZoneEffect& z = zones[i];
        const float w = std::isfinite(z.weight) ? std::clamp(z.weight, 0.f, 1.f) : 0.f;
        if (!(w > 0)) continue;
        if (z.setTemperature) c.temperature += (Finite(z.temperature, c.temperature) - c.temperature) * w;
        if (z.setHumidity) c.humidity += (Finite(z.humidity, c.humidity) - c.humidity) * w;
    }
    c.humidity = std::clamp(c.humidity, 0.f, 100.f);
    return c;
}

float CooledByHeight(float temperature, float height, const ClimateSettings& s) {
    const float lapse = std::isfinite(s.lapse) ? std::max(s.lapse, 0.f) : 0.f;
    const float above = Finite(height, 0.f) - Finite(s.baseHeight, 0.f);
    return temperature - above / 100.f * lapse;
}

ZoneBody BodyOf(const ClimateZone& zone, const space3d::Mat4& world) {
    ZoneBody b;
    const float sx = AxisLength(world, 0), sy = AxisLength(world, 1), sz = AxisLength(world, 2);
    const Vec3 size{std::max(Finite(zone.size.x, 0.f), 0.f), std::max(Finite(zone.size.y, 0.f), 0.f), std::max(Finite(zone.size.z, 0.f), 0.f)};
    if (zone.shape == ZoneShape::Cylinder) {
        b.kind = ZoneBody::Kind::Cylinder;
        b.half = {size.x * 0.5f * (sx + sz) * 0.5f, size.y * 0.5f * sy, 0};  // (横の大きさが違うときは平均の丸)
    } else {
        b.kind = ZoneBody::Kind::Box;
        b.half = {size.x * 0.5f * sx, size.y * 0.5f * sy, size.z * 0.5f * sz};
    }
    return b;
}

ZoneBody BodyOf(const ClimateZone2D& zone, const space2d::Mat3& world) {
    ZoneBody b;
    const float sx = std::sqrt(world.a * world.a + world.b * world.b), sy = std::sqrt(world.c * world.c + world.d * world.d);
    const Vec2 size{std::max(Finite(zone.size.x, 0.f), 0.f), std::max(Finite(zone.size.y, 0.f), 0.f)};
    if (zone.shape == ZoneShape2D::Circle) {
        b.kind = ZoneBody::Kind::Circle;
        b.half = {size.x * 0.5f * (sx + sy) * 0.5f, 0, 0};
    } else {
        b.kind = ZoneBody::Kind::Rect;
        b.half = {size.x * 0.5f * sx, size.y * 0.5f * sy, 0};
    }
    return b;
}

Vec3 LocalOf(const space3d::Mat4& world, Vec3 point) {
    const Vec3 d{point.x - world.m[3][0], point.y - world.m[3][1], point.z - world.m[3][2]};
    float out[3] = {0, 0, 0};
    for (int c = 0; c < 3; ++c) {
        const float len = AxisLength(world, c);
        if (len > 1e-12f) out[c] = (d.x * world.m[c][0] + d.y * world.m[c][1] + d.z * world.m[c][2]) / len;
    }
    return {out[0], out[1], out[2]};
}

Vec3 LocalOf(const space2d::Mat3& world, Vec2 point) {
    const float dx = point.x - world.tx, dy = point.y - world.ty;
    const float lx = std::sqrt(world.a * world.a + world.b * world.b), ly = std::sqrt(world.c * world.c + world.d * world.d);
    return {lx > 1e-12f ? (dx * world.a + dy * world.b) / lx : 0.f, ly > 1e-12f ? (dx * world.c + dy * world.d) / ly : 0.f, 0};
}

ClimateField::ClimateField(const game_production::Scene& scene) {
    if (const ClimateSettings* s = scene.Setting<ClimateSettings>()) settings_ = *s;
    const std::string t3 = reflect::TypeOf<space3d::Transform>().name, t2 = reflect::TypeOf<space2d::Transform2D>().name;
    const std::string z3 = reflect::TypeOf<ClimateZone>().name, z2 = reflect::TypeOf<ClimateZone2D>().name;
    struct Place {
        space3d::Mat4 world3;
        space2d::Mat3 world2;
        bool flat = false, placed = false;
    };
    std::unordered_map<int, Place> places;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const game_production::SceneEntity& e = *t.entity;
        const auto up = places.find(e.data.parent);
        Place p = up != places.end() ? up->second : Place{};
        p.placed = false;
        if (const game_production::Component* c = e.FindComponent(t3); c && c->Known()) {
            p.world3 = space3d::WorldMatrix(p.world3, *static_cast<const space3d::Transform*>(c->value.Get()));
            p.flat = false;
            p.placed = true;
        } else if (const game_production::Component* c2 = e.FindComponent(t2); c2 && c2->Known()) {
            p.world2 = space2d::WorldMatrix(p.world2, *static_cast<const space2d::Transform2D*>(c2->value.Get()));
            p.flat = true;
            p.placed = true;
        }
        places[e.data.id] = p;
        if (!t.activeInHierarchy || !p.placed) continue;  // (切ってあるエリア・場所の無いエリアは使わない)
        for (const game_production::Component& comp : e.components) {
            if (!comp.Known()) continue;
            if (!p.flat && comp.type == z3) zones3_.push_back({e.data.id, p.world3, *static_cast<const ClimateZone*>(comp.value.Get())});
            if (p.flat && comp.type == z2) zones2_.push_back({e.data.id, p.world2, *static_cast<const ClimateZone2D*>(comp.value.Get())});
        }
    }
}

ClimateHere ClimateField::At(Vec3 position) const {
    std::vector<ZoneEffect> effects;
    effects.reserve(zones3_.size());
    for (const Zone3D& z : zones3_) {
        const float w = ZoneWeight(ZoneDepth(BodyOf(z.zone, z.world), LocalOf(z.world, position)), z.zone.border);
        if (w > 0) effects.push_back({w, z.zone.priority, z.zone.setTemperature, z.zone.setHumidity, z.zone.temperature, z.zone.humidity});
    }
    ClimateHere c = Blend({settings_.temperature, settings_.humidity}, effects);
    c.temperature = CooledByHeight(c.temperature, position.y, settings_);
    return c;
}

ClimateHere ClimateField::At2D(Vec2 position) const {
    std::vector<ZoneEffect> effects;
    effects.reserve(zones2_.size());
    for (const Zone2D& z : zones2_) {
        const float w = ZoneWeight(ZoneDepth(BodyOf(z.zone, z.world), LocalOf(z.world, position)), z.zone.border);
        if (w > 0) effects.push_back({w, z.zone.priority, z.zone.setTemperature, z.zone.setHumidity, z.zone.temperature, z.zone.humidity});
    }
    return Blend({settings_.temperature, settings_.humidity}, effects);
}

}  // namespace rm::climate
