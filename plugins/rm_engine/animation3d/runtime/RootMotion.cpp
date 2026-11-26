#include "rm_engine/animation3d/runtime/RootMotion.h"

#include "rm_engine/space3d/runtime/Mesh.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rm::animation3d {

using space3d::BoneTRS;
using space3d::Quat;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float Wrap(float r) {
    if (!std::isfinite(r)) return 0;
    r = std::remainder(r, 2 * kPi);  // [-π, π]
    return r;
}

Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

bool Uses(const ClipRootMotion& s) { return s.enabled && (s.horizontal || s.vertical || s.turn); }

// クリップの中の時刻に収める (継ぎ目をまたがない区間の端)。
float InClip(const space3d::Clip& clip, float t) {
    const float d = std::isfinite(clip.duration) ? std::max(clip.duration, 0.f) : 0.f;
    return std::isfinite(t) ? std::clamp(t, 0.f, d) : 0.f;
}

}  // namespace

int RootBone(const space3d::Skeleton& skeleton, std::string_view name) {
    if (name.empty()) {
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            const space3d::Bone& b = skeleton.bones[i];
            if (b.parent < 0 && b.name != space3d::kStaticBoneName) return static_cast<int>(i);
        }
        return -1;
    }
    const int i = skeleton.Find(name);
    if (i < 0 || skeleton.bones[static_cast<size_t>(i)].parent >= 0) return -1;
    return i;
}

float YawOf(Quat q) {
    if (!(std::abs(q.y) > 0) && !(std::abs(q.w) > 0)) return 0;  // (横の軸のまわりに半周: ひねりが決まらない)
    return Wrap(2.f * std::atan2(q.y, q.w));
}

Quat YawQuat(float radians) { return space3d::QuatAxisAngle({0, 1, 0}, std::isfinite(radians) ? radians : 0.f); }

Vec3 RotateY(Vec3 v, float radians) { return space3d::QuatRotate(YawQuat(radians), v); }

RootDelta RootSegment(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int root,
                      const ClipRootMotion& settings, float a, float b) {
    RootDelta out;
    if (root < 0 || static_cast<size_t>(root) >= skeleton.bones.size() || !Uses(settings)) return out;
    a = InClip(clip, a);
    b = InClip(clip, b);
    if (a == b) return out;
    const BoneTRS pa = SampleBone(clip, binding, skeleton, root, a, false);
    const BoneTRS pb = SampleBone(clip, binding, skeleton, root, b, false);
    Vec3 d = Sub(pb.t, pa.t);
    if (settings.turn) {
        // 向きは始めのコマから回った分。区間を刻みより細かく分け、途中の回り方を足す (半周を超えても数える)。
        const Quat r0 = space3d::QuatConjugate(SampleBone(clip, binding, skeleton, root, 0.f, false).r);
        auto yaw = [&](float t) { return YawOf(SampleBone(clip, binding, skeleton, root, t, false).r * r0); };
        const float rate = std::isfinite(clip.rate) && clip.rate > 0 ? clip.rate : space3d::kDefaultClipRate;
        const int steps = std::clamp(static_cast<int>(std::ceil(std::abs(b - a) * rate)) + 1, 1, static_cast<int>(clip.frames) + 1);
        float prev = yaw(a);
        const float ya = prev;
        for (int i = 1; i <= steps; ++i) {
            const float y = i == steps ? YawOf(pb.r * r0) : yaw(a + (b - a) * static_cast<float>(i) / static_cast<float>(steps));
            out.turn += Wrap(y - prev);
            prev = y;
        }
        d = RotateY(d, -ya);  // (区間の始めの向きから見たもの)
    }
    if (!settings.horizontal) d.x = d.z = 0;
    if (!settings.vertical) d.y = 0;
    out.move = d;
    return out;
}

RootDelta AppendDelta(const RootDelta& first, const RootDelta& then) {
    const Vec3 m = RotateY(then.move, first.turn);
    return {{first.move.x + m.x, first.move.y + m.y, first.move.z + m.z}, first.turn + then.turn};
}

RootDelta RootDeltaBetween(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int root,
                           const ClipRootMotion& settings, float prevTime, const TimeStep& step, float speed, bool loop) {
    if (root < 0 || !Uses(settings) || !(clip.duration > 0)) return {};
    if (!loop || step.loops <= 0) return RootSegment(clip, binding, skeleton, root, settings, prevTime, step.time);
    const float end = clip.duration;
    const bool back = speed < 0;
    // prev → 継ぎ目、(またいだ数 - 1) 回の 1 周、継ぎ目の向こう → now。
    RootDelta total = RootSegment(clip, binding, skeleton, root, settings, prevTime, back ? 0.f : end);
    const int loops = std::min(step.loops, kMaxEventLoops);
    if (loops > 1) {
        const RootDelta full = RootSegment(clip, binding, skeleton, root, settings, back ? end : 0.f, back ? 0.f : end);
        for (int k = 1; k < loops; ++k) total = AppendDelta(total, full);
    }
    return AppendDelta(total, RootSegment(clip, binding, skeleton, root, settings, back ? end : 0.f, step.time));
}

RootDelta MixRootDelta(std::span<const RootDelta> deltas, std::span<const float> weights) {
    RootDelta out;
    double sum = 0;
    for (size_t i = 0; i < deltas.size() && i < weights.size(); ++i) {
        const float w = weights[i];
        if (!(w > 0) || !std::isfinite(w)) continue;
        out.move.x += deltas[i].move.x * w;
        out.move.y += deltas[i].move.y * w;
        out.move.z += deltas[i].move.z * w;
        out.turn += deltas[i].turn * w;
        sum += w;
    }
    if (!(sum > 0)) return {};
    const float inv = static_cast<float>(1.0 / sum);
    return {{out.move.x * inv, out.move.y * inv, out.move.z * inv}, out.turn * inv};
}

RootDelta LerpDelta(const RootDelta& a, const RootDelta& b, float w) { return {Lerp(a.move, b.move, w), a.turn + (b.turn - a.turn) * w}; }

void StripRoot(Pose& pose, int root, const ClipRootMotion& settings, const BoneTRS& start) {
    if (root < 0 || static_cast<size_t>(root) >= pose.size() || !Uses(settings)) return;
    BoneTRS& p = pose[static_cast<size_t>(root)];
    if (settings.horizontal) {
        p.t.x = start.t.x;
        p.t.z = start.t.z;
    }
    if (settings.vertical) p.t.y = start.t.y;
    if (settings.turn) {
        // 始めのコマから回ったもの (モデルの空間) のひねりを外す: 残りは傾きだけ。
        const float yaw = YawOf(p.r * space3d::QuatConjugate(start.r));
        p.r = space3d::QuatNormalize(YawQuat(-yaw) * p.r);
    }
}

RootDelta RootTotals(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int root,
                     const ClipRootMotion& settings) {
    return RootSegment(clip, binding, skeleton, root, settings, 0.f, clip.duration);
}

}  // namespace rm::animation3d
