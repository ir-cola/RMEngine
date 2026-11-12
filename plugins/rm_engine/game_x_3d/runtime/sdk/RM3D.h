// RM3D.h - Transform in 3D for scripts (the plugin ゲーム制作 × 3D gives it).
//
//   #include "RM3D.h"
//   rm::Transform3D t(self);
//   rm::Vec3 p = t.Position();            // in the parent (the inspector's ローカル)
//   p.y += 1.0f * dt;
//   t.SetPosition(p);
//   t.SetRotation({0, 90, 0});            // degrees, turned X -> Y -> Z
//
// An entity without a Transform: the getters give zeros (scale: ones) and the setters do nothing.
// Valid() tells whether it has one.
//
// Debug drawing - lines to see what a script does (where it aims, what it hits). The editor shows
// them in the game view and the scene view while the game plays (hidden parts faint); an exported
// game does nothing with them (the calls cost next to nothing there).
//   rm::Debug::Line(a, b, {1, 0, 0, 1});              // this frame only
//   rm::Debug::Box(center, {1, 1, 1}, color, 2.0f);   // for 2 seconds
//   rm::Debug::Sphere(center, 0.5f, color);
//   rm::Debug::Ray(origin, forward, 10.0f, color);
#pragma once

#include "RM.h"
#include "RM3DAbi.h"

namespace rm {

class Transform3D {
public:
    // The engine's component (Entity::Add / Get / Remove<rm::Transform3D>()).
    static constexpr const char* kComponentType = "rm::space3d::Transform";
    explicit Transform3D(Entity entity)
        : entity_(entity), api_(Part<RmTransform3DApi>(RM_PART_TRANSFORM3D, RM_PART_TRANSFORM3D_VERSION)) {}

    // The entity has a Transform (and this engine has the 3D plugin).
    bool Valid() const {
        float v[3];
        return api_ && api_->get(Api()->ctx, entity_.id, 0, v) != 0;
    }
    Vec3 Position() const { return Get(0, {0, 0, 0}); }
    Vec3 Rotation() const { return Get(1, {0, 0, 0}); }  // degrees
    Vec3 Scale() const { return Get(2, {1, 1, 1}); }
    void SetPosition(Vec3 v) const { Set(0, v); }
    void SetRotation(Vec3 degrees) const { Set(1, degrees); }
    void SetScale(Vec3 v) const { Set(2, v); }
    // Where it is in the world (its parents included).
    Vec3 WorldPosition() const {
        float v[3] = {0, 0, 0};
        if (api_) api_->worldPosition(Api()->ctx, entity_.id, v);
        return {v[0], v[1], v[2]};
    }

private:
    Vec3 Get(int which, Vec3 fallback) const {
        float v[3] = {fallback.x, fallback.y, fallback.z};
        if (api_ && api_->get(Api()->ctx, entity_.id, which, v)) return {v[0], v[1], v[2]};
        return fallback;
    }
    void Set(int which, Vec3 v) const {
        const float f[3] = {v.x, v.y, v.z};
        if (api_) api_->set(Api()->ctx, entity_.id, which, f);
    }

    Entity entity_;
    const RmTransform3DApi* api_;
};

namespace Debug {
namespace detail {
inline const RmDebug3DApi* Api3D() {
    static const RmDebug3DApi* api = Part<RmDebug3DApi>(RM_PART_DEBUG3D, RM_PART_DEBUG3D_VERSION);  // the engine's, for good
    return api;
}
}  // namespace detail

// A line from a to b (world units), for `seconds` (0: this frame only).
inline void Line(Vec3 a, Vec3 b, Color color = {}, float seconds = 0) {
    const float pa[3] = {a.x, a.y, a.z}, pb[3] = {b.x, b.y, b.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->line(Api()->ctx, pa, pb, c, seconds);
}
// The edges of a box along the world's axes: its middle and its size.
inline void Box(Vec3 center, Vec3 size, Color color = {}, float seconds = 0) {
    const float p[3] = {center.x, center.y, center.z}, s[3] = {size.x, size.y, size.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->box(Api()->ctx, p, s, c, seconds);
}
// A ball as three circles (one round each axis).
inline void Sphere(Vec3 center, float radius, Color color = {}, float seconds = 0) {
    const float p[3] = {center.x, center.y, center.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->sphere(Api()->ctx, p, radius, c, seconds);
}
// From `origin`, `length` along `direction` (any length; none: nothing drawn).
inline void Ray(Vec3 origin, Vec3 direction, float length, Color color = {}, float seconds = 0) {
    const float o[3] = {origin.x, origin.y, origin.z}, v[3] = {direction.x, direction.y, direction.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->ray(Api()->ctx, o, v, length, c, seconds);
}
}  // namespace Debug

}  // namespace rm
