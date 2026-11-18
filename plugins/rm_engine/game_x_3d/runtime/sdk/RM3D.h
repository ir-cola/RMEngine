// RM3D.h - Transform in 3D for scripts (the plugin ゲーム制作 × 3D gives it).
//
//   #include "RM3D.h"
//   rm::Transform3D t(self);
//   rm::Vec3 p = t.Position();            // in the parent (the inspector's ローカル)
//   p.y += 1.0f * dt;
//   t.SetPosition(p);
//   t.SetRotation({0, 90, 0});            // degrees, turned X -> Y -> Z
//   t.LookAt(rm::Transform3D(target).WorldPosition());   // its -Z (where a camera looks) at the target
//   t.Translate({0, 0, -speed * dt});     // along its own axes (forward)
//   t.Rotate({0, 90 * dt, 0});            // about its own axes
//   rm::Vec3 ahead = t.Forward();         // world directions: Forward (-Z), Right (+X), Up (+Y)
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
        : entity_(entity), api_(Part<RmTransform3DApi2>(RM_PART_TRANSFORM3D, RM_PART_TRANSFORM3D_VERSION2)) {}

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
    // Put there / turned so in the world (its own values changed to make up for its parents').
    void SetWorldPosition(Vec3 p) const {
        const float f[3] = {p.x, p.y, p.z};
        if (api_) api_->setWorldPosition(Api()->ctx, entity_.id, f);
    }
    Vec3 WorldRotation() const {
        float v[3] = {0, 0, 0};
        if (api_) api_->worldRotation(Api()->ctx, entity_.id, v);
        return {v[0], v[1], v[2]};
    }
    void SetWorldRotation(Vec3 degrees) const {
        const float f[3] = {degrees.x, degrees.y, degrees.z};
        if (api_) api_->setWorldRotation(Api()->ctx, entity_.id, f);
    }
    // Its turn as a quaternion (in the parent / in the world).
    Quat RotationQuat() const { return Quat::Euler(Rotation()); }
    Quat WorldRotationQuat() const { return Quat::Euler(WorldRotation()); }
    void SetRotation(Quat q) const { SetRotation(q.ToEuler()); }

    // Turns it so its -Z (where a camera looks) points at `target` in the world, its Y towards `up`.
    void LookAt(Vec3 target, Vec3 up = {0, 1, 0}) const {
        const float t[3] = {target.x, target.y, target.z}, u[3] = {up.x, up.y, up.z};
        if (api_) api_->lookAt(Api()->ctx, entity_.id, t, u);
    }
    void LookAt(Entity target, Vec3 up = {0, 1, 0}) const { LookAt(Transform3D(target).WorldPosition(), up); }

    // Where its own axes point in the world (length 1): Forward = -Z (where it looks), Right = +X, Up = +Y.
    Vec3 Forward() const { return Normalize(Direction(2) * -1.0f); }
    Vec3 Right() const { return Normalize(Direction(0)); }
    Vec3 Up() const { return Normalize(Direction(1)); }
    // A point of its own space (as its children see it) in the world.
    Vec3 TransformPoint(Vec3 local) const {
        float m[16];
        if (!api_ || !api_->worldMatrix(Api()->ctx, entity_.id, m)) return local;
        return {m[0] * local.x + m[4] * local.y + m[8] * local.z + m[12], m[1] * local.x + m[5] * local.y + m[9] * local.z + m[13],
                m[2] * local.x + m[6] * local.y + m[10] * local.z + m[14]};
    }

    // Moved by `delta`: along its own axes (self = true: {0, 0, -1} is forward), or the world's.
    void Translate(Vec3 delta, bool self = true) const {
        if (self) {
            SetPosition(Position() + RotationQuat() * delta);
        } else {
            SetWorldPosition(WorldPosition() + delta);
        }
    }
    // Turned by `degrees` (X -> Y -> Z) about its own axes (self) or the world's.
    void Rotate(Vec3 degrees, bool self = true) const {
        if (self) {
            SetRotation(RotationQuat() * Quat::Euler(degrees));
        } else {
            SetWorldRotation((Quat::Euler(degrees) * WorldRotationQuat()).ToEuler());
        }
    }
    // Turned by `degrees` about `axis` through `point` (both in the world): it moves round the point too.
    void RotateAround(Vec3 point, Vec3 axis, float degrees) const {
        const Quat q = Quat::AngleAxis(degrees, axis);
        SetWorldPosition(point + q * (WorldPosition() - point));
        SetWorldRotation((q * WorldRotationQuat()).ToEuler());
    }

private:
    // A column of its world matrix (an axis, scaled).
    Vec3 Direction(int column) const {
        float m[16];
        if (!api_ || !api_->worldMatrix(Api()->ctx, entity_.id, m)) {
            return column == 0 ? Vec3{1, 0, 0} : column == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
        }
        return {m[column * 4 + 0], m[column * 4 + 1], m[column * 4 + 2]};
    }
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
    const RmTransform3DApi2* api_;
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
