// RM3D.h - スクリプトから使う 3D の Transform (プラグイン ゲーム制作 × 3D が与える)。
//
//   #include "RM3D.h"
//   rm::Transform3D t(self);
//   rm::Vec3 p = t.Position();            // 親の中での位置 (インスペクターの ローカル)
//   p.y += 1.0f * dt;
//   t.SetPosition(p);
//   t.SetRotation({0, 90, 0});            // 度。X -> Y -> Z の順に回す
//   t.LookAt(rm::Transform3D(target).WorldPosition());   // その -Z (カメラが見る向き) を target に向ける
//   t.Translate({0, 0, -speed * dt});     // 自分の軸に沿って (前へ)
//   t.Rotate({0, 90 * dt, 0});            // 自分の軸のまわりに
//   rm::Vec3 ahead = t.Forward();         // ワールドの向き: Forward (-Z), Right (+X), Up (+Y)
//
// Transform の無いエンティティ: 取る方は 0 (スケールは 1) を返し、書く方は何もしない。
// Valid() で、Transform があるかどうか分かる。
//
// デバッグ描画 - スクリプトが何をしているか (どこを狙い、何に当たるか) を見るための線。
// ゲームが動いている間、エディターがゲームビューとシーンビューに出す (隠れた部分は薄く)。
// 書き出したゲームでは何もしない (そこでは呼んでもほとんど手間がかからない)。
//   rm::Debug::Line(a, b, {1, 0, 0, 1});              // このフレームだけ
//   rm::Debug::Box(center, {1, 1, 1}, color, 2.0f);   // 2 秒間
//   rm::Debug::Sphere(center, 0.5f, color);
//   rm::Debug::Ray(origin, forward, 10.0f, color);
//
// シーンの空 (空 → 地球の空): その 時刻。
//   rm::Sky::SetHour(18.0f);                          // 夕方
// (天候 - 雨・雪・風 - はプラグイン 天候のもの: #include "RMWeather.h"。)
#pragma once

#include "RM.h"
#include "RM3DAbi.h"

namespace rm {

class Transform3D {
public:
    // エンジンの部品 (Entity::Add / Get / Remove<rm::Transform3D>())。
    static constexpr const char* kComponentType = "rm::space3d::Transform";
    explicit Transform3D(Entity entity)
        : entity_(entity), api_(Part<RmTransform3DApi2>(RM_PART_TRANSFORM3D, RM_PART_TRANSFORM3D_VERSION2)) {}

    // エンティティに Transform がある (かつ、このエンジンに 3D プラグインがある)。
    bool Valid() const {
        float v[3];
        return api_ && api_->get(Api()->ctx, entity_.id, 0, v) != 0;
    }
    Vec3 Position() const { return Get(0, {0, 0, 0}); }
    Vec3 Rotation() const { return Get(1, {0, 0, 0}); }  // 度
    Vec3 Scale() const { return Get(2, {1, 1, 1}); }
    void SetPosition(Vec3 v) const { Set(0, v); }
    void SetRotation(Vec3 degrees) const { Set(1, degrees); }
    void SetScale(Vec3 v) const { Set(2, v); }
    // ワールドでの位置 (親も含める)。
    Vec3 WorldPosition() const {
        float v[3] = {0, 0, 0};
        if (api_) api_->worldPosition(Api()->ctx, entity_.id, v);
        return {v[0], v[1], v[2]};
    }
    // ワールドでこう置く / 回す (親の分を埋め合わせるよう、自分の値を変える)。
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
    // 回転をクォータニオンで (親の中 / ワールド)。
    Quat RotationQuat() const { return Quat::Euler(Rotation()); }
    Quat WorldRotationQuat() const { return Quat::Euler(WorldRotation()); }
    void SetRotation(Quat q) const { SetRotation(q.ToEuler()); }

    // その -Z (カメラが見る向き) がワールドの `target` を向くように回し、Y を `up` の方へ向ける。
    void LookAt(Vec3 target, Vec3 up = {0, 1, 0}) const {
        const float t[3] = {target.x, target.y, target.z}, u[3] = {up.x, up.y, up.z};
        if (api_) api_->lookAt(Api()->ctx, entity_.id, t, u);
    }
    void LookAt(Entity target, Vec3 up = {0, 1, 0}) const { LookAt(Transform3D(target).WorldPosition(), up); }

    // 自分の軸がワールドで向く先 (長さ 1): Forward = -Z (見る向き), Right = +X, Up = +Y。
    Vec3 Forward() const { return Normalize(Direction(2) * -1.0f); }
    Vec3 Right() const { return Normalize(Direction(0)); }
    Vec3 Up() const { return Normalize(Direction(1)); }
    // 自分の空間 (子から見たもの) の点をワールドへ。
    Vec3 TransformPoint(Vec3 local) const {
        float m[16];
        if (!api_ || !api_->worldMatrix(Api()->ctx, entity_.id, m)) return local;
        return {m[0] * local.x + m[4] * local.y + m[8] * local.z + m[12], m[1] * local.x + m[5] * local.y + m[9] * local.z + m[13],
                m[2] * local.x + m[6] * local.y + m[10] * local.z + m[14]};
    }

    // `delta` だけ動かす: 自分の軸に沿って (self = true: {0, 0, -1} が前)、またはワールドの軸に沿って。
    void Translate(Vec3 delta, bool self = true) const {
        if (self) {
            SetPosition(Position() + RotationQuat() * delta);
        } else {
            SetWorldPosition(WorldPosition() + delta);
        }
    }
    // `degrees` だけ回す (X -> Y -> Z)。自分の軸 (self) かワールドの軸のまわりに。
    void Rotate(Vec3 degrees, bool self = true) const {
        if (self) {
            SetRotation(RotationQuat() * Quat::Euler(degrees));
        } else {
            SetWorldRotation((Quat::Euler(degrees) * WorldRotationQuat()).ToEuler());
        }
    }
    // `point` を通る `axis` (どちらもワールド) のまわりに `degrees` だけ回す: 点のまわりを回るので位置も動く。
    void RotateAround(Vec3 point, Vec3 axis, float degrees) const {
        const Quat q = Quat::AngleAxis(degrees, axis);
        SetWorldPosition(point + q * (WorldPosition() - point));
        SetWorldRotation((q * WorldRotationQuat()).ToEuler());
    }

private:
    // ワールド行列の 1 列 (軸。スケール込み)。
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
    static const RmDebug3DApi* api = Part<RmDebug3DApi>(RM_PART_DEBUG3D, RM_PART_DEBUG3D_VERSION);  // エンジンのもの。ずっと使う
    return api;
}
}  // namespace detail

// a から b への線 (ワールド単位)。`seconds` 秒間 (0: このフレームだけ)。
inline void Line(Vec3 a, Vec3 b, Color color = {}, float seconds = 0) {
    const float pa[3] = {a.x, a.y, a.z}, pb[3] = {b.x, b.y, b.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->line(Api()->ctx, pa, pb, c, seconds);
}
// ワールドの軸に沿った箱の辺: その中心と大きさ。
inline void Box(Vec3 center, Vec3 size, Color color = {}, float seconds = 0) {
    const float p[3] = {center.x, center.y, center.z}, s[3] = {size.x, size.y, size.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->box(Api()->ctx, p, s, c, seconds);
}
// 3 つの円で描く球 (各軸に 1 つずつ)。
inline void Sphere(Vec3 center, float radius, Color color = {}, float seconds = 0) {
    const float p[3] = {center.x, center.y, center.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->sphere(Api()->ctx, p, radius, c, seconds);
}
// `origin` から `direction` の向きに `length` だけ (direction の長さは何でもよい。0 なら何も描かない)。
inline void Ray(Vec3 origin, Vec3 direction, float length, Color color = {}, float seconds = 0) {
    const float o[3] = {origin.x, origin.y, origin.z}, v[3] = {direction.x, direction.y, direction.z}, c[4] = {color.r, color.g, color.b, color.a};
    if (const RmDebug3DApi* d = detail::Api3D()) d->ray(Api()->ctx, o, v, length, c, seconds);
}
}  // namespace Debug

// シーンの 空 (地球の空): その 時刻 (0 .. 24)。すぐ変わる。
namespace Sky {
namespace detail {
inline const RmSky3DApi2* ApiSky() {
    static const RmSky3DApi2* api = Part<RmSky3DApi2>(RM_PART_SKY3D, RM_PART_SKY3D_VERSION2);
    return api;
}
}  // namespace detail

// シーンに空がある (かつ、このエンジンに 3D プラグインがある)。
inline bool Valid() {
    float h = 0;
    return detail::ApiSky() && detail::ApiSky()->hour(Api()->ctx, &h) != 0;
}
inline void SetHour(float hour) {
    if (const RmSky3DApi2* s = detail::ApiSky()) s->setHour(Api()->ctx, hour);
}
inline float Hour() {
    float h = 12;
    if (const RmSky3DApi2* s = detail::ApiSky()) s->hour(Api()->ctx, &h);
    return h;
}
}  // namespace Sky

}  // namespace rm
