// RM2D.h - Transform in 2D for scripts (the plugin ゲーム制作 × 2D gives it).
//
//   #include "RM2D.h"
//   rm::Transform2D t(self);
//   rm::Vec2 p = t.Position();     // in the parent
//   p.x += 2.0f * dt;
//   t.SetPosition(p);
//   t.SetRotation(t.Rotation() + 90.0f * dt);   // degrees, counter-clockwise +
//   t.LookAt(rm::Transform2D(target).WorldPosition());   // its right (+X) towards the target
//
// The sprite's animation (スプライトのアニメーション):
//   rm::SpriteAnimation anim(self);
//   anim.SetFrames({4, 5, 6, 7});   anim.Play();          // a run: those cells, from the start
//   anim.SetSpeed(12);  anim.Pause();  anim.Stop();       // 1 秒のコマ数 / held / back to the first
//   if (!anim.Playing()) ...                              // a one that does not loop reached its end
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
        : entity_(entity), api_(Part<RmTransform2DApi2>(RM_PART_TRANSFORM2D, RM_PART_TRANSFORM2D_VERSION2)) {}

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
    // In the world (its parents included); setting one changes its own values to make up for the parents'.
    Vec2 WorldPosition() const { return World(0, {0, 0}); }
    float WorldRotation() const { return World(1, {0, 0}).x; }
    Vec2 WorldScale() const { return World(2, {1, 1}); }
    void SetWorldPosition(Vec2 v) const { SetWorld(0, v); }
    void SetWorldRotation(float degrees) const { SetWorld(1, {degrees, 0}); }
    // Turns it so its right (+X) points at `target` in the world.
    void LookAt(Vec2 target) const {
        const float t[2] = {target.x, target.y};
        if (api_) api_->lookAt(Api()->ctx, entity_.id, t);
    }
    void LookAt(Entity target) const { LookAt(Transform2D(target).WorldPosition()); }
    // Where its right (+X) and up (+Y) point in the world (length 1).
    Vec2 Right() const { return FromAngle(WorldRotation()); }
    Vec2 Up() const { return FromAngle(WorldRotation() + 90.0f); }
    // Moved by `delta`: along its own axes (self) or the world's.
    void Translate(Vec2 delta, bool self = true) const {
        if (self) {
            SetPosition(Position() + Rotate(delta, Rotation()));
        } else {
            SetWorldPosition(WorldPosition() + delta);
        }
    }

private:
    Vec2 World(int which, Vec2 fallback) const {
        float v[2] = {fallback.x, fallback.y};
        if (api_ && api_->worldGet(Api()->ctx, entity_.id, which, v)) return {v[0], v[1]};
        return fallback;
    }
    void SetWorld(int which, Vec2 v) const {
        const float f[2] = {v.x, v.y};
        if (api_) api_->worldSet(Api()->ctx, entity_.id, which, f);
    }
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
    const RmTransform2DApi2* api_;
};

// The entity's スプライトのアニメーション, from a script (nothing happens on an entity without one: Valid()).
class SpriteAnimation {
public:
    explicit SpriteAnimation(Entity entity)
        : entity_(entity), api_(Part<RmSpriteAnimationApi>(RM_PART_SPRITE_ANIMATION, RM_PART_SPRITE_ANIMATION_VERSION)) {}
    bool Valid() const { return api_ && api_->has(Api()->ctx, entity_.id) != 0; }
    // On (paused / stopped: moving again); fromStart: from the first step.
    void Play(bool fromStart = false) const {
        if (api_) api_->play(Api()->ctx, entity_.id, fromStart ? 1 : 0);
    }
    void Pause(bool paused = true) const {
        if (api_) api_->pause(Api()->ctx, entity_.id, paused ? 1 : 0);
    }
    void Stop() const {
        if (api_) api_->stop(Api()->ctx, entity_.id);
    }
    // 1 秒のコマ数.
    float Speed() const { return api_ ? api_->speed(Api()->ctx, entity_.id) : 0.0f; }
    void SetSpeed(float framesPerSecond) const {
        if (api_) api_->setSpeed(Api()->ctx, entity_.id, framesPerSecond);
    }
    bool Loop() const { return api_ && api_->loop(Api()->ctx, entity_.id) != 0; }
    void SetLoop(bool loop) const {
        if (api_) api_->setLoop(Api()->ctx, entity_.id, loop ? 1 : 0);
    }
    // コマの並び: the sprite's cells to show in turn (empty: all of them, 0 up); it plays them from the start.
    void SetFrames(const std::vector<int>& cells) const {
        if (api_) api_->setFrames(Api()->ctx, entity_.id, cells.data(), static_cast<uint32_t>(cells.size()));
    }
    // The place in コマの並び now (0 = the first), and a jump to one.
    int Frame() const { return api_ ? api_->step(Api()->ctx, entity_.id) : 0; }
    void SetFrame(int step) const {
        if (api_) api_->setStep(Api()->ctx, entity_.id, step);
    }
    // Moving: not paused or stopped, and not on the last step of one that does not loop.
    bool Playing() const { return api_ && api_->playing(Api()->ctx, entity_.id) != 0; }

private:
    Entity entity_;
    const RmSpriteAnimationApi* api_;
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
