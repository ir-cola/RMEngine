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
#pragma once

#include "RM.h"
#include "RM2DAbi.h"

namespace rm {

class Transform2D {
public:
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

}  // namespace rm
