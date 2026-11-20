// RM2D.h - スクリプト用の 2D の Transform (プラグイン ゲーム制作 × 2D が提供する)。
//
//   #include "RM2D.h"
//   rm::Transform2D t(self);
//   rm::Vec2 p = t.Position();     // 親の中での位置
//   p.x += 2.0f * dt;
//   t.SetPosition(p);
//   t.SetRotation(t.Rotation() + 90.0f * dt);   // 度、反時計回りが +
//   t.LookAt(rm::Transform2D(target).WorldPosition());   // 右 (+X) を目標に向ける
//
// スプライトのアニメーション:
//   rm::SpriteAnimation anim(self);
//   anim.SetFrames({4, 5, 6, 7});   anim.Play();          // 走り: そのセルを、始めから
//   anim.SetSpeed(12);  anim.Pause();  anim.Stop();       // 1 秒のコマ数 / 止めておく / 最初に戻る
//   if (!anim.Playing()) ...                              // ループしないものが終わりに着いた
//
// Transform 2D のないエンティティ: ゲッターは 0 を返し (拡大縮小は 1)、セッターは何もしない。
//
// デバッグ描画 - スクリプトが何をしているか見るための平面上の線。Play 中、エディターはゲームビューと
// シーンビューの上にこれを出す。書き出したゲームは何もしない。
//   rm::Debug::Line(a, b, {1, 0, 0, 1});             // このフレームだけ
//   rm::Debug::Rect(center, {2, 1}, color, 2.0f);    // 2 秒間
//   rm::Debug::Circle(center, 0.5f, color);
#pragma once

#include "RM.h"
#include "RM2DAbi.h"

namespace rm {

class Transform2D {
public:
    // エンジンの部品 (Entity::Add / Get / Remove<rm::Transform2D>())。
    static constexpr const char* kComponentType = "rm::space2d::Transform2D";
    explicit Transform2D(Entity entity)
        : entity_(entity), api_(Part<RmTransform2DApi2>(RM_PART_TRANSFORM2D, RM_PART_TRANSFORM2D_VERSION2)) {}

    // エンティティに Transform 2D がある (そしてこのエンジンに 2D プラグインがある)。
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
    // ワールドでの値 (親を含む): 設定すると、親の分を埋め合わせるように自分の値を変える。
    Vec2 WorldPosition() const { return World(0, {0, 0}); }
    float WorldRotation() const { return World(1, {0, 0}).x; }
    Vec2 WorldScale() const { return World(2, {1, 1}); }
    void SetWorldPosition(Vec2 v) const { SetWorld(0, v); }
    void SetWorldRotation(float degrees) const { SetWorld(1, {degrees, 0}); }
    // 右 (+X) がワールドの `target` を向くように回す。
    void LookAt(Vec2 target) const {
        const float t[2] = {target.x, target.y};
        if (api_) api_->lookAt(Api()->ctx, entity_.id, t);
    }
    void LookAt(Entity target) const { LookAt(Transform2D(target).WorldPosition()); }
    // 右 (+X) と上 (+Y) がワールドでどちらを向くか (長さ 1)。
    Vec2 Right() const { return FromAngle(WorldRotation()); }
    Vec2 Up() const { return FromAngle(WorldRotation() + 90.0f); }
    // `delta` だけ動かす: 自分の軸に沿って (self) か、ワールドの軸に沿って。
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

// エンティティの スプライトのアニメーション をスクリプトから (持たないエンティティでは何も起きない: Valid())。
class SpriteAnimation {
public:
    explicit SpriteAnimation(Entity entity)
        : entity_(entity), api_(Part<RmSpriteAnimationApi>(RM_PART_SPRITE_ANIMATION, RM_PART_SPRITE_ANIMATION_VERSION)) {}
    bool Valid() const { return api_ && api_->has(Api()->ctx, entity_.id) != 0; }
    // オン (一時停止 / 停止していたら、また動く)。fromStart: 最初のステップから。
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
    // コマの並び: 順に見せるスプライトのセル (空: すべて、0 から)。始めから再生する。
    void SetFrames(const std::vector<int>& cells) const {
        if (api_) api_->setFrames(Api()->ctx, entity_.id, cells.data(), static_cast<uint32_t>(cells.size()));
    }
    // 今の コマの並び での位置 (0 = 最初)、と、そこへのジャンプ。
    int Frame() const { return api_ ? api_->step(Api()->ctx, entity_.id) : 0; }
    void SetFrame(int step) const {
        if (api_) api_->setStep(Api()->ctx, entity_.id, step);
    }
    // 動いている: 一時停止も停止もしておらず、ループしないものの最後のステップでもない。
    bool Playing() const { return api_ && api_->playing(Api()->ctx, entity_.id) != 0; }

private:
    Entity entity_;
    const RmSpriteAnimationApi* api_;
};

namespace Debug {
namespace detail {
inline const RmDebug2DApi* Api2D() {
    static const RmDebug2DApi* api = Part<RmDebug2DApi>(RM_PART_DEBUG2D, RM_PART_DEBUG2D_VERSION);  // エンジンのもの、ずっと
    return api;
}
}  // namespace detail

// a から b への線 (平面の単位)、`seconds` 秒間 (0: このフレームだけ)。
inline void Line(Vec2 a, Vec2 b, Color color = {}, float seconds = 0) {
    const float pa[2] = {a.x, a.y}, pb[2] = {b.x, b.y}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug2DApi* d = detail::Api2D()) d->line(Api()->ctx, pa, pb, c, seconds);
}
// 平面の軸に沿った長方形の辺: その真ん中と大きさ。
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
