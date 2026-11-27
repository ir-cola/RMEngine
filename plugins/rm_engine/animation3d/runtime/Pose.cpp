#include "rm_engine/animation3d/runtime/Pose.h"

#include "rm_engine/space3d/runtime/ClipPack.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_map>

namespace rm::animation3d {

using space3d::BoneTRS;
using space3d::Mat4;
using space3d::Quat;

namespace {

// クリップの時刻 -> 刻みの位置 (0 .. frames - 1)。最後のサンプルがちょうど duration になる。
float FramePosition(const space3d::Clip& clip, float time, bool loop) {
    if (clip.frames <= 1 || !(clip.duration > 0)) return 0;
    if (!std::isfinite(time)) time = 0;
    if (loop) {
        time = std::fmod(time, clip.duration);
        if (time < 0) time += clip.duration;
        if (time >= clip.duration) time = 0;  // 負の小さな数に duration を足すと duration に丸まることがある
    } else {
        time = std::clamp(time, 0.f, clip.duration);
    }
    const float last = static_cast<float>(clip.frames - 1);
    return std::clamp(time / clip.duration * last, 0.f, last);
}

// 1 つの列 (位置・回転・大きさのどれか) の、刻みの位置 i0 + w の値。長さ 0 なら rest。
template <class T, class Mix>
T SampleChannel(const std::vector<T>& v, size_t i0, float w, T rest, Mix mix) {
    if (v.empty()) return rest;
    if (v.size() == 1) return v[0];
    const size_t last = v.size() - 1;
    const T& a = v[std::min(i0, last)];
    const T& b = v[std::min(i0 + 1, last)];
    return mix(a, b, w);
}

}  // namespace

Pose RestPose(const space3d::Skeleton& skeleton) {
    Pose p;
    p.reserve(skeleton.bones.size());
    for (const space3d::Bone& b : skeleton.bones) p.push_back(b.rest);
    return p;
}

Vec3 Lerp(Vec3 a, Vec3 b, float w) { return {a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w}; }

Quat QuatNlerp(Quat a, Quat b, float w) {
    if (space3d::QuatDot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
    return space3d::QuatNormalize({a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w, a.w + (b.w - a.w) * w});
}

BoneTRS LerpTRS(const BoneTRS& a, const BoneTRS& b, float w) { return {Lerp(a.t, b.t, w), QuatNlerp(a.r, b.r, w), Lerp(a.s, b.s, w)}; }

ClipBinding BindClip(const space3d::Clip& clip, const space3d::Skeleton& skeleton) {
    ClipBinding binding;
    binding.track.assign(skeleton.bones.size(), -1);
    // 名前 -> その名前のトラックの番号 (クリップの順)
    std::unordered_map<std::string_view, std::vector<int>> byName;
    for (size_t i = 0; i < clip.tracks.size(); ++i) byName[clip.tracks[i].bone].push_back(static_cast<int>(i));
    std::unordered_map<std::string_view, size_t> seen;  // 名前ごとに、ここまでに出た骨の数
    std::vector<bool> used(clip.tracks.size(), false);
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const std::string_view name = skeleton.bones[i].name;
        const auto it = byName.find(name);
        if (it == byName.end()) continue;
        const size_t k = seen[name]++;
        const int track = k < it->second.size() ? it->second[k] : it->second.front();
        binding.track[i] = track;
        used[static_cast<size_t>(track)] = true;
    }
    binding.unmatched = static_cast<int>(std::count(used.begin(), used.end(), false));
    return binding;
}

namespace {

// 骨 `i` の、刻みの位置 i0 + w の値 (SampleClip と SampleBone の 1 本の式)。
BoneTRS SampleAt(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, size_t i, size_t i0, float w) {
    const BoneTRS& rest = skeleton.bones[i].rest;
    const int ti = i < binding.track.size() ? binding.track[i] : -1;
    if (ti < 0 || static_cast<size_t>(ti) >= clip.tracks.size()) return rest;
    BoneTRS out;
    if (clip.packed.size() == clip.tracks.size()) {  // 圧縮したクリップ: 前後 2 つの鍵だけ戻す (ClipPack.h)
        const space3d::PackedTrack& p = clip.packed[static_cast<size_t>(ti)];
        const float frame = static_cast<float>(i0) + w;
        out.t = space3d::SamplePackedVec(p.t, frame, rest.t);
        out.r = space3d::SamplePackedQuat(p.r, frame, rest.r);  // (混ぜた後に長さを 1 にしてある)
        out.s = space3d::SamplePackedVec(p.s, frame, rest.s);
        return out;
    }
    const space3d::BoneTrack& tr = clip.tracks[static_cast<size_t>(ti)];
    out.t = SampleChannel(tr.t, i0, w, rest.t, Lerp);
    out.r = space3d::QuatNormalize(SampleChannel(tr.r, i0, w, rest.r, QuatNlerp));
    out.s = SampleChannel(tr.s, i0, w, rest.s, Lerp);
    return out;
}

}  // namespace

void SampleClip(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, float time, bool loop,
                Pose& out) {
    const size_t n = skeleton.bones.size();
    out.resize(n);
    const float f = FramePosition(clip, time, loop);
    const size_t i0 = static_cast<size_t>(f);
    const float w = f - static_cast<float>(i0);
    for (size_t i = 0; i < n; ++i) out[i] = SampleAt(clip, binding, skeleton, i, i0, w);
}

float SampleMorph(const space3d::Clip& clip, int track, float time, bool loop) {
    if (track < 0 || static_cast<size_t>(track) >= clip.morphs.size()) return 0.f;
    const float f = FramePosition(clip, time, loop);
    const size_t i0 = static_cast<size_t>(f);
    return SampleChannel(clip.morphs[static_cast<size_t>(track)].weights, i0, f - static_cast<float>(i0), 0.f,
                         [](float a, float b, float w) { return a + (b - a) * w; });
}

BoneTRS SampleBone(const space3d::Clip& clip, const ClipBinding& binding, const space3d::Skeleton& skeleton, int bone, float time, bool loop) {
    if (bone < 0 || static_cast<size_t>(bone) >= skeleton.bones.size()) return {};
    const float f = FramePosition(clip, time, loop);
    const size_t i0 = static_cast<size_t>(f);
    return SampleAt(clip, binding, skeleton, static_cast<size_t>(bone), i0, f - static_cast<float>(i0));
}

void Blend(const Pose& a, const Pose& b, float w, Pose& out) {
    const size_t na = a.size(), nb = b.size();
    const size_t both = std::min(na, nb);
    if (na >= nb) {
        // out が a のときも、ここで書くのは自分の番号だけなので壊れない
        out.resize(na);
        for (size_t i = 0; i < both; ++i) out[i] = LerpTRS(a[i], b[i], w);
        for (size_t i = both; i < na; ++i) out[i] = a[i];
    } else {
        out.resize(nb);
        for (size_t i = 0; i < both; ++i) out[i] = LerpTRS(a[i], b[i], w);
        for (size_t i = both; i < nb; ++i) out[i] = b[i];
    }
}

void PoseAccumulator::Begin(size_t boneCount) {
    t_.assign(boneCount, Vec3{0, 0, 0});
    s_.assign(boneCount, Vec3{0, 0, 0});
    r_.assign(boneCount, Quat{0, 0, 0, 0});
    w_.assign(boneCount, 0.f);
    total_ = 0;
}

void PoseAccumulator::Add(const Pose& pose, float weight) {
    if (!(weight > 0) || !std::isfinite(weight)) return;
    const size_t n = std::min(pose.size(), t_.size());
    for (size_t i = 0; i < n; ++i) {
        const BoneTRS& p = pose[i];
        t_[i] = {t_[i].x + p.t.x * weight, t_[i].y + p.t.y * weight, t_[i].z + p.t.z * weight};
        s_[i] = {s_[i].x + p.s.x * weight, s_[i].y + p.s.y * weight, s_[i].z + p.s.z * weight};
        // ここまでの和と向きをそろえる (最初の 1 つは和が 0 なので、そのまま)
        const float sign = space3d::QuatDot(r_[i], p.r) < 0 ? -weight : weight;
        r_[i] = {r_[i].x + p.r.x * sign, r_[i].y + p.r.y * sign, r_[i].z + p.r.z * sign, r_[i].w + p.r.w * sign};
        w_[i] += weight;
    }
    total_ += weight;
}

void PoseAccumulator::Finish(const space3d::Skeleton& skeleton, Pose& out) const {
    out = RestPose(skeleton);
    const size_t n = std::min(out.size(), t_.size());
    for (size_t i = 0; i < n; ++i) {
        if (!(w_[i] > 0)) continue;  // この骨はどの姿勢にも無かった: 休みの姿勢
        const float inv = 1.f / w_[i];
        out[i].t = {t_[i].x * inv, t_[i].y * inv, t_[i].z * inv};
        out[i].s = {s_[i].x * inv, s_[i].y * inv, s_[i].z * inv};
        out[i].r = space3d::QuatNormalize(r_[i]);
    }
}

std::vector<float> BoneMask(const space3d::Skeleton& skeleton, std::span<const std::string> roots) {
    const size_t n = skeleton.bones.size();
    if (roots.empty()) return std::vector<float>(n, 1.f);
    std::vector<float> mask(n, 0.f);
    for (size_t i = 0; i < n; ++i) {
        const space3d::Bone& b = skeleton.bones[i];
        bool hit = std::find(roots.begin(), roots.end(), b.name) != roots.end();
        // (親が先に並んでいる: 親が入っていれば子も)
        if (!hit && b.parent >= 0 && static_cast<size_t>(b.parent) < i) hit = mask[static_cast<size_t>(b.parent)] > 0;
        mask[i] = hit ? 1.f : 0.f;
    }
    return mask;
}

void OverlayPose(Pose& base, const Pose& layer, float weight, std::span<const float> mask) {
    if (!(weight > 0) || !std::isfinite(weight)) return;
    const float w0 = std::min(weight, 1.f);
    const size_t n = std::min(base.size(), layer.size());
    for (size_t i = 0; i < n; ++i) {
        const float w = w0 * (mask.empty() ? 1.f : (i < mask.size() ? mask[i] : 0.f));
        if (w <= 0) continue;
        base[i] = w >= 1 ? layer[i] : LerpTRS(base[i], layer[i], w);
    }
}

void AddPose(Pose& base, const Pose& layer, const Pose& rest, float weight, std::span<const float> mask) {
    if (!(weight > 0) || !std::isfinite(weight)) return;
    const size_t n = std::min({base.size(), layer.size(), rest.size()});
    const space3d::Quat identity{0, 0, 0, 1};
    for (size_t i = 0; i < n; ++i) {
        const float w = weight * (mask.empty() ? 1.f : (i < mask.size() ? mask[i] : 0.f));
        if (w <= 0) continue;
        const space3d::BoneTRS& l = layer[i];
        const space3d::BoneTRS& r = rest[i];
        space3d::BoneTRS& b = base[i];
        b.t = {b.t.x + (l.t.x - r.t.x) * w, b.t.y + (l.t.y - r.t.y) * w, b.t.z + (l.t.z - r.t.z) * w};
        const space3d::Quat delta = space3d::QuatNormalize(space3d::QuatConjugate(r.r) * l.r);
        b.r = space3d::QuatNormalize(b.r * QuatNlerp(identity, delta, w));
        auto ratio = [](float a, float c) { return std::fabs(c) > 1e-6f ? a / c : 1.f; };
        const Vec3 s{ratio(l.s.x, r.s.x), ratio(l.s.y, r.s.y), ratio(l.s.z, r.s.z)};
        b.s = {b.s.x * (1 + (s.x - 1) * w), b.s.y * (1 + (s.y - 1) * w), b.s.z * (1 + (s.z - 1) * w)};
    }
}

void ModelMatrices(const space3d::Skeleton& skeleton, const Pose& pose, std::vector<Mat4>& out) {
    const size_t n = skeleton.bones.size();
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const space3d::Bone& bone = skeleton.bones[i];
        const Mat4 local = space3d::TRSMatrix(i < pose.size() ? pose[i] : bone.rest);
        const int parent = bone.parent;
        out[i] = parent >= 0 && static_cast<size_t>(parent) < i ? out[static_cast<size_t>(parent)] * local : local;
    }
}

void SkinMatrices(std::span<const Mat4> model, std::span<const Mat4> inverseBind, std::vector<space3d::BoneMatrix>& out) {
    out.resize(model.size());
    for (size_t i = 0; i < model.size(); ++i) {
        out[i] = space3d::ToBoneMatrix(i < inverseBind.size() ? model[i] * inverseBind[i] : model[i]);
    }
}

}  // namespace rm::animation3d
