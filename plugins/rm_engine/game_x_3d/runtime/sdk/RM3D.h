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
#pragma once

#include "RM.h"
#include "RM3DAbi.h"

namespace rm {

class Transform3D {
public:
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

}  // namespace rm
