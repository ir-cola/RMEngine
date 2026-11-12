// RM2D.h - Transform in 2D for scripts (the plugin ゲーム制作 × 2D gives it).
//
//   #include "RM2D.h"
//   rm::Transform2D t(self);
//   rm::Vec2 p = t.Position();     // in the parent
//   p.x += 2.0f * dt;
//   t.SetPosition(p);
//   t.SetRotation(t.Rotation() + 90.0f * dt);   // degrees, counter-clockwise +
//
// An entity without a Transform 2D: the getters give zeros (scale: ones), the setters do nothing.
//
// Debug drawing - lines in the plane to see what a script does. The editor shows them over the game
// view and the scene view while the game plays; an exported game does nothing with them.
//   rm::Debug::Line(a, b, {1, 0, 0, 1});             // this frame only
//   rm::Debug::Rect(center, {2, 1}, color, 2.0f);    // for 2 seconds
//   rm::Debug::Circle(center, 0.5f, color);
#pragma once

#include "RM.h"
#include "RM2DAbi.h"

namespace rm {

class Transform2D {
public:
    // The engine's component (Entity::Add / Get / Remove<rm::Transform2D>()).
    static constexpr const char* kComponentType = "rm::space2d::Transform2D";
    explicit Transform2D(Entity entity)
        : entity_(entity), api_(Part<RmTransform2DApi>(RM_PART_TRANSFORM2D, RM_PART_TRANSFORM2D_VERSION)) {}

    // The entity has a Transform 2D (and this engine has the 2D plugin).
    bool Valid() const {
        float v[2];
        return api_ && api_->get(Api()->ctx, entity_.id, 0, v) != 0;
    }
    Vec2 Position() const { return Get(0, {0, 0}); }
    float Rotation() const { return Get(1, {0, 0}).x; }
    Vec2 Scale() const { return Get(2, {1, 1}); }
    void SetPosition(Vec2 v) const { Set(0, v); }
    void SetRotation(float degrees) const { Set(1, {degrees, 0}); }
    void SetScale(Vec2 v) const { Set(2, v); }

private:
    Vec2 Get(int which, Vec2 fallback) const {
        float v[2] = {fallback.x, fallback.y};
        if (api_ && api_->get(Api()->ctx, entity_.id, which, v)) return {v[0], v[1]};
        return fallback;
    }
    void Set(int which, Vec2 v) const {
        const float f[2] = {v.x, v.y};
        if (api_) api_->set(Api()->ctx, entity_.id, which, f);
    }

    Entity entity_;
    const RmTransform2DApi* api_;
};

namespace Debug {
namespace detail {
inline const RmDebug2DApi* Api2D() {
    static const RmDebug2DApi* api = Part<RmDebug2DApi>(RM_PART_DEBUG2D, RM_PART_DEBUG2D_VERSION);  // the engine's, for good
    return api;
}
}  // namespace detail

// A line from a to b (units of the plane), for `seconds` (0: this frame only).
inline void Line(Vec2 a, Vec2 b, Color color = {}, float seconds = 0) {
    const float pa[2] = {a.x, a.y}, pb[2] = {b.x, b.y}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug2DApi* d = detail::Api2D()) d->line(Api()->ctx, pa, pb, c, seconds);
}
// The edges of a rectangle along the plane's axes: its middle and its size.
inline void Rect(Vec2 center, Vec2 size, Color color = {}, float seconds = 0) {
    const float p[2] = {center.x, center.y}, s[2] = {size.x, size.y}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug2DApi* d = detail::Api2D()) d->rect(Api()->ctx, p, s, c, seconds);
}
inline void Circle(Vec2 center, float radius, Color color = {}, float seconds = 0) {
    const float p[2] = {center.x, center.y}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug2DApi* d = detail::Api2D()) d->circle(Api()->ctx, p, radius, c, seconds);
}
}  // namespace Debug

}  // namespace rm
