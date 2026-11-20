// RMMath.h - スクリプト用の数学 (RM.h が取り込むので、自分で取り込まなくてよい)。純粋: ここにあるものはエンジンに何も聞かない。
//
//   rm::Vec3 to = target - position;                       // Vec2 / Vec3 で + - * / が使える
//   float d = rm::Length(to);  rm::Vec3 dir = rm::Normalize(to);
//   position = rm::MoveTowards(position, target, speed * dt);
//   float a = rm::Math::Lerp(0.0f, 10.0f, 0.5f);           // 5
//   rm::Quat q = rm::Quat::Euler({0, 90, 0});              // エンジンの角度 (度。X -> Y -> Z)
//   rm::Vec3 v = q * rm::Vec3{0, 0, 1};                   // 回した結果
//   float r = rm::Random::Range(1.0f, 6.0f);
//
// エンジンの決まり: Y が上で左手系 (右・上・奥が +)。もの (とカメラ) は +Z の向きを見る。角度は度で、Transform の回転は
// X、Y、Z の順に回す (R = Rz * Ry * Rx)。
//
// rm::Vec2 / rm::Vec3 / rm::Color は、このファイルより前に宣言しておく (RM.h が宣言する)。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rm {

// --- Vec2 / Vec3 -------------------------------------------------------------------------------
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator-(Vec2 a) { return {-a.x, -a.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { return a = a + b; }
inline Vec2& operator-=(Vec2& a, Vec2 b) { return a = a - b; }
inline Vec2& operator*=(Vec2& a, float s) { return a = a * s; }
inline Vec2& operator/=(Vec2& a, float s) { return a = a / s; }

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { return a = a + b; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { return a = a - b; }
inline Vec3& operator*=(Vec3& a, float s) { return a = a * s; }
inline Vec3& operator/=(Vec3& a, float s) { return a = a / s; }
// 成分ごとに掛ける (拡大・縮小)。
inline Vec2 Scale(Vec2 a, Vec2 b) { return {a.x * b.x, a.y * b.y}; }
inline Vec3 Scale(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

namespace Math {
inline constexpr float PI = 3.14159265358979323846f;
inline constexpr float Deg2Rad = PI / 180.0f;
inline constexpr float Rad2Deg = 180.0f / PI;
inline constexpr float Epsilon = 1e-6f;

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }
// t = 0 で a、t = 1 で b (t は 0..1 に切る)。LerpUnclamped はその外にも進む。
inline float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }
inline float LerpUnclamped(float a, float b, float t) { return a + (b - a) * t; }
// v が a と b の間のどこか (0..1)。a == b なら 0。
inline float InverseLerp(float a, float b, float v) { return a == b ? 0.0f : Clamp01((v - a) / (b - a)); }
// 両端がなめらか (エルミート補間): t が 0..1 のとき a から b へ。
inline float SmoothStep(float a, float b, float t) {
    t = Clamp01(t);
    t = t * t * (3.0f - 2.0f * t);
    return a + (b - a) * t;
}
// `current` から `target` へ、最大でも `maxDelta` だけ進む (行き過ぎない)。
inline float MoveTowards(float current, float target, float maxDelta) {
    if (std::fabs(target - current) <= maxDelta) return target;
    return current + (target > current ? maxDelta : -maxDelta);
}
// v を 0..length に巻き戻す (0..2 での 5.5 -> 1.5)。PingPong は 0 -> length -> 0 ... と往復する。
inline float Repeat(float v, float length) {
    if (length <= 0) return 0;
    return Clamp(v - std::floor(v / length) * length, 0.0f, length);
}
inline float PingPong(float v, float length) {
    if (length <= 0) return 0;
    v = Repeat(v, length * 2.0f);
    return length - std::fabs(v - length);
}
// 角度 a から角度 b への一番短い回転 (度。-180..180)。
inline float DeltaAngle(float a, float b) {
    float d = Repeat(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    return d;
}
inline float LerpAngle(float a, float b, float t) { return a + DeltaAngle(a, b) * Clamp01(t); }
inline float MoveTowardsAngle(float current, float target, float maxDelta) {
    const float d = DeltaAngle(current, target);
    if (-maxDelta < d && d < maxDelta) return target;
    return MoveTowards(current, current + d, maxDelta);
}
inline bool Approximately(float a, float b) {
    return std::fabs(b - a) < std::max(1e-6f * std::max(std::fabs(a), std::fabs(b)), Epsilon * 8.0f);
}
inline float Sign(float v) { return v >= 0 ? 1.0f : -1.0f; }
inline float Abs(float v) { return std::fabs(v); }
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Sqrt(float v) { return std::sqrt(v); }
inline float Pow(float v, float p) { return std::pow(v, p); }
inline float Sin(float radians) { return std::sin(radians); }
inline float Cos(float radians) { return std::cos(radians); }
inline float Tan(float radians) { return std::tan(radians); }
inline float Atan2(float y, float x) { return std::atan2(y, x); }
inline float Floor(float v) { return std::floor(v); }
inline float Ceil(float v) { return std::ceil(v); }
inline float Round(float v) { return std::round(v); }
// `target` へ、行き過ぎないバネのようになめらかに追う (カメラの追従など)。`velocity` はフレームの間
// 呼び出し側が持っておく (RM_TRANSIENT のフィールド)。smoothTime: 着くまでのおよその秒数。
inline float SmoothDamp(float current, float target, float& velocity, float smoothTime, float dt, float maxSpeed = 1e30f) {
    smoothTime = std::max(0.0001f, smoothTime);
    const float omega = 2.0f / smoothTime;
    const float x = omega * dt;
    const float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    const float original = target;
    const float maxChange = maxSpeed * smoothTime;
    change = Clamp(change, -maxChange, maxChange);
    target = current - change;
    const float temp = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * e;
    float out = target + (change + temp) * e;
    if ((original - current > 0.0f) == (out > original)) {
        out = original;
        velocity = dt > 0 ? (out - original) / dt : 0.0f;
    }
    return out;
}
}  // namespace Math

inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// 2D: a.x * b.y - a.y * b.x (b が a から反時計回りにあるとき > 0)。
inline float Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float LengthSquared(Vec2 v) { return Dot(v, v); }
inline float LengthSquared(Vec3 v) { return Dot(v, v); }
inline float Length(Vec2 v) { return std::sqrt(Dot(v, v)); }
inline float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
inline float Distance(Vec2 a, Vec2 b) { return Length(a - b); }
inline float Distance(Vec3 a, Vec3 b) { return Length(a - b); }
// 同じ向きで長さ 1 にする ((0, 0) は (0, 0) のまま)。
inline Vec2 Normalize(Vec2 v) {
    const float l = Length(v);
    return l > 1e-12f ? v / l : Vec2{};
}
inline Vec3 Normalize(Vec3 v) {
    const float l = Length(v);
    return l > 1e-12f ? v / l : Vec3{};
}
inline Vec2 Lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * Math::Clamp01(t); }
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * Math::Clamp01(t); }
inline Vec2 LerpUnclamped(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline Vec3 LerpUnclamped(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec2 Min(Vec2 a, Vec2 b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline Vec2 Max(Vec2 a, Vec2 b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
inline Vec3 Min(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 Max(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
// `current` から `target` へ、最大でも `maxDistance` だけ進む (行き過ぎない)。
inline Vec2 MoveTowards(Vec2 current, Vec2 target, float maxDistance) {
    const Vec2 d = target - current;
    const float l = Length(d);
    return l <= maxDistance || l == 0 ? target : current + d / l * maxDistance;
}
inline Vec3 MoveTowards(Vec3 current, Vec3 target, float maxDistance) {
    const Vec3 d = target - current;
    const float l = Length(d);
    return l <= maxDistance || l == 0 ? target : current + d / l * maxDistance;
}
// maxLength より長くしない (向きは同じ)。
inline Vec2 ClampLength(Vec2 v, float maxLength) {
    const float l = Length(v);
    return l > maxLength && l > 0 ? v / l * maxLength : v;
}
inline Vec3 ClampLength(Vec3 v, float maxLength) {
    const float l = Length(v);
    return l > maxLength && l > 0 ? v / l * maxLength : v;
}
// この法線 (長さ 1) の面ではね返った向き。
inline Vec2 Reflect(Vec2 v, Vec2 normal) { return v - normal * (2.0f * Dot(v, normal)); }
inline Vec3 Reflect(Vec3 v, Vec3 normal) { return v - normal * (2.0f * Dot(v, normal)); }
// v のうち `onto` に沿う部分 / この法線の面に沿う部分。
inline Vec3 Project(Vec3 v, Vec3 onto) {
    const float d = Dot(onto, onto);
    return d > 1e-12f ? onto * (Dot(v, onto) / d) : Vec3{};
}
inline Vec3 ProjectOnPlane(Vec3 v, Vec3 normal) { return v - Project(v, normal); }
// 2 つの向きの間の角度 (度。0..180)。
inline float Angle(Vec2 a, Vec2 b) {
    const float d = std::sqrt(LengthSquared(a) * LengthSquared(b));
    return d < 1e-12f ? 0.0f : std::acos(Math::Clamp(Dot(a, b) / d, -1.0f, 1.0f)) * Math::Rad2Deg;
}
inline float Angle(Vec3 a, Vec3 b) {
    const float d = std::sqrt(LengthSquared(a) * LengthSquared(b));
    return d < 1e-12f ? 0.0f : std::acos(Math::Clamp(Dot(a, b) / d, -1.0f, 1.0f)) * Math::Rad2Deg;
}
// 2D: a から b へ、反時計回りが + (-180..180)。3D: `axis` の先から見たときの符号。
inline float SignedAngle(Vec2 a, Vec2 b) { return Angle(a, b) * (Cross(a, b) >= 0 ? 1.0f : -1.0f); }
inline float SignedAngle(Vec3 a, Vec3 b, Vec3 axis) { return Angle(a, b) * (Dot(axis, Cross(a, b)) >= 0 ? 1.0f : -1.0f); }
// 2D の角度が表す向き (度。0 = 右、90 = 上)と、その逆。
inline Vec2 FromAngle(float degrees) { return {std::cos(degrees * Math::Deg2Rad), std::sin(degrees * Math::Deg2Rad)}; }
inline float ToAngle(Vec2 v) { return std::atan2(v.y, v.x) * Math::Rad2Deg; }
// `degrees` だけ反時計回りに回す。
inline Vec2 Rotate(Vec2 v, float degrees) {
    const float c = std::cos(degrees * Math::Deg2Rad), s = std::sin(degrees * Math::Deg2Rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
inline Vec2 SmoothDamp(Vec2 current, Vec2 target, Vec2& velocity, float smoothTime, float dt) {
    return {Math::SmoothDamp(current.x, target.x, velocity.x, smoothTime, dt), Math::SmoothDamp(current.y, target.y, velocity.y, smoothTime, dt)};
}
inline Vec3 SmoothDamp(Vec3 current, Vec3 target, Vec3& velocity, float smoothTime, float dt) {
    return {Math::SmoothDamp(current.x, target.x, velocity.x, smoothTime, dt), Math::SmoothDamp(current.y, target.y, velocity.y, smoothTime, dt),
            Math::SmoothDamp(current.z, target.z, velocity.z, smoothTime, dt)};
}

// --- 色 -----------------------------------------------------------------------------------
inline Color Lerp(Color a, Color b, float t) {
    t = Math::Clamp01(t);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// --- 回転 (クォータニオン) ---------------------------------------------------------------------
// 3D での回転。エンジンの角度 (Euler)、軸と角度、見る向きのどれかから作る。
// q * v は v を回す。a * b は先に b、次に a で回す。
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    static Quat Identity() { return {}; }
    // `axis` のまわりに `degrees` (軸の長さは何でもよい。右手: 軸の先から見て反時計回り)。
    static Quat AngleAxis(float degrees, Vec3 axis) {
        const Vec3 n = Normalize(axis);
        if (LengthSquared(n) == 0) return {};
        const float h = degrees * Math::Deg2Rad * 0.5f, s = std::sin(h);
        return {n.x * s, n.y * s, n.z * s, std::cos(h)};
    }
    // エンジンの角度 (Transform の回転): X、Y、Z の順。
    static Quat Euler(Vec3 degrees) {
        return AngleAxis(degrees.z, {0, 0, 1}) * AngleAxis(degrees.y, {0, 1, 0}) * AngleAxis(degrees.x, {1, 0, 0});
    }
    // `forward` の向きを見る (ものの +Z がそこへ向く)。Y は `up` にできるだけ近づける。
    static Quat LookRotation(Vec3 forward, Vec3 up = {0, 1, 0}) {
        const Vec3 zAxis = Normalize(forward);
        if (LengthSquared(zAxis) == 0) return {};
        Vec3 xAxis = Normalize(Cross(up, zAxis));
        if (LengthSquared(xAxis) == 0) xAxis = Normalize(Cross(std::fabs(zAxis.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, zAxis));
        const Vec3 yAxis = Cross(zAxis, xAxis);
        return FromAxes(xAxis, yAxis, zAxis);
    }
    // 向き `from` を向き `to` へ移す回転 (一番短いもの)。
    static Quat FromToRotation(Vec3 from, Vec3 to) {
        const Vec3 a = Normalize(from), b = Normalize(to);
        const float d = Dot(a, b);
        if (d >= 1.0f - 1e-6f) return {};
        if (d <= -1.0f + 1e-6f) {
            Vec3 axis = Cross({1, 0, 0}, a);
            if (LengthSquared(axis) < 1e-6f) axis = Cross({0, 1, 0}, a);
            return AngleAxis(180.0f, axis);
        }
        const Vec3 c = Cross(a, b);
        return Quat{c.x, c.y, c.z, 1.0f + d}.Normalized();
    }
    // X、Y、Z の軸がこれらの向きへ行く回転 (どれも長さ 1 で、互いに直角)。
    static Quat FromAxes(Vec3 xAxis, Vec3 yAxis, Vec3 zAxis) {
        const float m00 = xAxis.x, m10 = xAxis.y, m20 = xAxis.z;
        const float m01 = yAxis.x, m11 = yAxis.y, m21 = yAxis.z;
        const float m02 = zAxis.x, m12 = zAxis.y, m22 = zAxis.z;
        Quat q;
        const float trace = m00 + m11 + m22;
        if (trace > 0) {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;
            q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
        } else if (m00 > m11 && m00 > m22) {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
            q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
        } else if (m11 > m22) {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
            q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
        } else {
            const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
        }
        return q.Normalized();
    }

    Quat Normalized() const {
        const float l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 1e-12f ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    // 逆の回転。
    Quat Inverse() const { return Quat{-x, -y, -z, w}.Normalized(); }
    // この回転のエンジンの角度 (度。X -> Y -> Z)。Transform の回転として使う。
    Vec3 ToEuler() const {
        const float r20 = 2 * (x * z - w * y), r21 = 2 * (y * z + w * x), r22 = 1 - 2 * (x * x + y * y);
        const float r10 = 2 * (x * y + w * z), r00 = 1 - 2 * (y * y + z * z);
        const float r01 = 2 * (x * y - w * z), r11 = 1 - 2 * (x * x + z * z);
        const float sy = Math::Clamp(-r20, -1.0f, 1.0f);
        const float ay = std::asin(sy) * Math::Rad2Deg;
        if (std::fabs(sy) < 0.99999f) return {std::atan2(r21, r22) * Math::Rad2Deg, ay, std::atan2(r10, r00) * Math::Rad2Deg};
        // 真上か真下: x -/+ z だけが決まる (z = 0)。
        return {(sy > 0 ? std::atan2(r01, r11) : std::atan2(-r01, r11)) * Math::Rad2Deg, sy > 0 ? 90.0f : -90.0f, 0.0f};
    }
    Vec3 Forward() const;  // +Z を回した向き (見ている向き)
    Vec3 Right() const;    // +X を回した向き
    Vec3 Up() const;       // +Y を回した向き

    friend Quat operator*(Quat a, Quat b) {
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }
    friend Vec3 operator*(Quat q, Vec3 v) {
        const Vec3 u{q.x, q.y, q.z};
        const Vec3 t = Cross(u, v) * 2.0f;
        return v + t * q.w + Cross(u, t);
    }
};
inline Vec3 Quat::Forward() const { return *this * Vec3{0, 0, 1}; }
inline Vec3 Quat::Right() const { return *this * Vec3{1, 0, 0}; }
inline Vec3 Quat::Up() const { return *this * Vec3{0, 1, 0}; }

inline float Dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
// 2 つの回転の間の角度 (度。0..180)。
inline float Angle(Quat a, Quat b) {
    const float d = std::min(std::fabs(Dot(a.Normalized(), b.Normalized())), 1.0f);
    return d > 1.0f - 1e-6f ? 0.0f : 2.0f * std::acos(d) * Math::Rad2Deg;
}
// a から b へ、一番短い道すじでなめらかに (t は 0..1)。
inline Quat Slerp(Quat a, Quat b, float t) {
    t = Math::Clamp01(t);
    float d = Dot(a, b);
    if (d < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    if (d > 0.9995f) return Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.Normalized();
    const float th = std::acos(d), s = std::sin(th);
    const float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}
// `from` から `to` へ、最大でも `maxDegrees` だけ回す。
inline Quat RotateTowards(Quat from, Quat to, float maxDegrees) {
    const float a = Angle(from, to);
    return a <= maxDegrees || a == 0 ? to : Slerp(from, to, maxDegrees / a);
}

// --- 乱数 ------------------------------------------------------------------------------------
// ゲーム全体で 1 本の流れ (xorshift)。Seed(n) で同じ数をもう一度出せる。
namespace Random {
namespace detail {
inline uint64_t& State() {
    static uint64_t s = 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&s));
    return s;
}
inline uint64_t Next() {
    uint64_t& s = State();
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ull;
}
}  // namespace detail
inline void Seed(uint64_t seed) { detail::State() = seed ? seed * 0x9E3779B97F4A7C15ull : 0x9E3779B97F4A7C15ull; }
// 0 <= Value() < 1。
inline float Value() { return static_cast<float>(detail::Next() >> 40) / static_cast<float>(1ull << 24); }
// min <= x < max (float)、min <= n < max (int。Range(1, 7) はサイコロ)。
inline float Range(float min, float max) { return min + (max - min) * Value(); }
inline int Range(int min, int max) {
    if (max <= min) return min;
    return min + static_cast<int>(detail::Next() % static_cast<uint64_t>(static_cast<int64_t>(max) - min));
}
inline bool Chance(float probability) { return Value() < probability; }
// 半径 1 の円 / 球の中の点と、(その縁の) 向き。
inline Vec2 InsideUnitCircle() {
    const float a = Value() * 2.0f * Math::PI, r = std::sqrt(Value());
    return {std::cos(a) * r, std::sin(a) * r};
}
inline Vec3 OnUnitSphere() {
    const float z = Range(-1.0f, 1.0f), a = Value() * 2.0f * Math::PI, r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return {std::cos(a) * r, std::sin(a) * r, z};
}
inline Vec3 InsideUnitSphere() { return OnUnitSphere() * std::cbrt(Value()); }
// どの向きにも (まんべんなく) 向く回転。
inline Quat Rotation() {
    const float u1 = Value(), u2 = Value() * 2.0f * Math::PI, u3 = Value() * 2.0f * Math::PI;
    const float a = std::sqrt(1 - u1), b = std::sqrt(u1);
    return {a * std::sin(u2), a * std::cos(u2), b * std::sin(u3), b * std::cos(u3)};
}
}  // namespace Random

}  // namespace rm
