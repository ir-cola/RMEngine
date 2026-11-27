#include "rm_engine/space3d/runtime/ClipPack.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rm::space3d {

namespace {

using Kind = PackedChannel::Kind;

constexpr uint32_t kMaxPackedFrames = 65536;  // 鍵の刻みは 16 ビット
constexpr uint32_t kMaxSpan = 1024;           // 1 つの鍵の間の長さの上限 (圧縮にかかる時間を抑える)
constexpr float kMinReach = 0.05f;            // 骨の先までの長さの下限 (m。先の骨・小さな骨でも回転を雑にしすぎない)
constexpr float kSqrt2 = 1.41421356f;

float Len(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mix(Vec3 a, Vec3 b, float w) { return {a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w}; }
Quat MixQ(Quat a, Quat b, float w) {  // (animation3d の QuatNlerp と同じ式: 短い方へ)
    if (QuatDot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
    return QuatNormalize({a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w, a.w + (b.w - a.w) * w});
}
float Angle(Quat a, Quat b) {  // 2 つの回転の違い (ラジアン)
    // 4 つの数の向きの間の角の 2 倍。acos(dot) は 1 の近くで float の丸めが大きい (0.0003 ラジアン) ので atan2 で。
    a = QuatNormalize(a);
    b = QuatNormalize(b);
    if (QuatDot(a, b) < 0) b = {-b.x, -b.y, -b.z, -b.w};
    const Quat d{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}, s{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    return 4.f * std::atan2(std::sqrt(QuatDot(d, d)), std::sqrt(QuatDot(s, s)));
}

// 骨ごとの、先までの長さと、その骨を通る一番長い鎖の骨の数。
struct Reach {
    std::vector<float> length;
    std::vector<int> chain;
};
Reach ReachOf(const Skeleton& skeleton) {
    const size_t n = skeleton.bones.size();
    Reach out{std::vector<float>(n, kMinReach), std::vector<int>(n, 1)};
    if (!ParentsFirst(skeleton)) return out;
    std::vector<Mat4> world(n);
    std::vector<int> depth(n, 1), below(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const int p = skeleton.bones[i].parent;
        world[i] = p >= 0 ? world[static_cast<size_t>(p)] * TRSMatrix(skeleton.bones[i].rest) : TRSMatrix(skeleton.bones[i].rest);
        if (p >= 0) depth[i] = depth[static_cast<size_t>(p)] + 1;
    }
    std::vector<Vec3> at(n);
    for (size_t i = 0; i < n; ++i) at[i] = TransformPoint(world[i], {0, 0, 0});
    // 子孫の骨の場所まで (親へ向かって上がる)。
    for (size_t i = n; i-- > 0;) {
        for (int a = skeleton.bones[i].parent, steps = 1; a >= 0; a = skeleton.bones[static_cast<size_t>(a)].parent, ++steps) {
            const auto ai = static_cast<size_t>(a);
            out.length[ai] = std::max(out.length[ai], Len(Sub(at[i], at[ai])));
            below[ai] = std::max(below[ai], steps);
        }
    }
    for (size_t i = 0; i < n; ++i) out.chain[i] = depth[i] + below[i];
    return out;
}

// 鍵を選ぶ: 前の鍵から、間のサンプルが全部 `ok` (直線で戻した値が誤差の中) である限り伸ばす。
template <class T, class Ok>
std::vector<uint16_t> ChooseKeys(const std::vector<T>& decoded, Ok ok) {
    const auto n = static_cast<uint32_t>(decoded.size());
    std::vector<uint16_t> keys{0};
    uint32_t a = 0;
    for (uint32_t b = a + 2; b < n; ++b) {
        bool fits = b - a <= kMaxSpan;
        for (uint32_t j = a + 1; fits && j < b; ++j) {
            fits = ok(j, static_cast<float>(j - a) / static_cast<float>(b - a), decoded[a], decoded[b]);
        }
        if (!fits) {
            a = b - 1;
            keys.push_back(static_cast<uint16_t>(a));
        }
    }
    if (n > 1 && keys.back() != n - 1) keys.push_back(static_cast<uint16_t>(n - 1));
    return keys;
}

PackedChannel PackVec(const std::vector<Vec3>& v, float tolerance) {
    PackedChannel c;
    if (v.empty()) return c;
    if (v.size() == 1) {
        c.kind = Kind::Constant;
        c.constant = {v[0].x, v[0].y, v[0].z, 0};
        return c;
    }
    Vec3 lo = v[0], hi = v[0];
    for (const Vec3& p : v) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    if (Len(Sub(hi, lo)) <= tolerance * 0.25f) {  // 動きが誤差より小さい: ずっと同じ値
        c.kind = Kind::Constant;
        const Vec3 m = Mix(lo, hi, 0.5f);
        c.constant = {m.x, m.y, m.z, 0};
        return c;
    }
    const Vec3 step = {(hi.x - lo.x) / 65535.f, (hi.y - lo.y) / 65535.f, (hi.z - lo.z) / 65535.f};
    // 16 ビットの丸めの誤差 (半分の刻み) が上限の 1/4 より大きければ float で。
    const bool wide = Len(step) * 0.5f > tolerance * 0.25f;
    auto quant = [](float x, float l, float s) { return s > 0 ? static_cast<uint16_t>(std::clamp(std::lround((x - l) / s), 0L, 65535L)) : uint16_t{0}; };
    std::vector<Vec3> decoded(v.size());
    std::vector<std::array<uint16_t, 3>> qs(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (wide) {
            decoded[i] = v[i];
        } else {
            qs[i] = {quant(v[i].x, lo.x, step.x), quant(v[i].y, lo.y, step.y), quant(v[i].z, lo.z, step.z)};
            decoded[i] = {lo.x + qs[i][0] * step.x, lo.y + qs[i][1] * step.y, lo.z + qs[i][2] * step.z};
        }
    }
    c.keys = ChooseKeys(decoded, [&](uint32_t j, float w, Vec3 a, Vec3 b) { return Len(Sub(Mix(a, b, w), v[j])) <= tolerance; });
    if (wide) {
        c.kind = Kind::KeysFloat;
        for (const uint16_t k : c.keys) c.f.insert(c.f.end(), {v[k].x, v[k].y, v[k].z});
    } else {
        c.kind = Kind::Keys16;
        c.lo = lo;
        c.step = step;
        for (const uint16_t k : c.keys) c.q.insert(c.q.end(), qs[k].begin(), qs[k].end());
    }
    return c;
}

PackedChannel PackRot(const std::vector<Quat>& v, float angle) {
    PackedChannel c;
    if (v.empty()) return c;
    bool still = true;
    for (const Quat& q : v) still = still && Angle(q, v[0]) <= angle * 0.25f;
    if (still) {
        const Quat q = QuatNormalize(v[0]);
        c.kind = Kind::Constant;
        c.constant = {q.x, q.y, q.z, q.w};
        return c;
    }
    std::vector<Quat> decoded(v.size());
    std::vector<std::array<uint16_t, 3>> qs(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        PackQuat48(v[i], qs[i].data());
        decoded[i] = UnpackQuat48(qs[i].data());
    }
    c.keys = ChooseKeys(decoded, [&](uint32_t j, float w, Quat a, Quat b) { return Angle(MixQ(a, b, w), v[j]) <= angle; });
    c.kind = Kind::Keys16;
    for (const uint16_t k : c.keys) c.q.insert(c.q.end(), qs[k].begin(), qs[k].end());
    return c;
}

// `frame` の前後の鍵の番号と、その間の割合。
void Between(const std::vector<uint16_t>& keys, float frame, size_t& k0, size_t& k1, float& w) {
    const auto it = std::upper_bound(keys.begin(), keys.end(), frame, [](float f, uint16_t k) { return f < static_cast<float>(k); });
    if (it == keys.begin()) {
        k0 = k1 = 0;
        w = 0;
        return;
    }
    k0 = static_cast<size_t>(it - keys.begin()) - 1;
    if (k0 + 1 >= keys.size()) {
        k1 = k0;
        w = 0;
        return;
    }
    k1 = k0 + 1;
    w = std::clamp((frame - keys[k0]) / static_cast<float>(keys[k1] - keys[k0]), 0.f, 1.f);
}

size_t ChannelBytes(const PackedChannel& c) {
    switch (c.kind) {
        case Kind::Empty: return 0;
        case Kind::Constant: return sizeof(c.constant);
        case Kind::Keys16: return c.keys.size() * 2 + c.q.size() * 2 + sizeof(Vec3) * 2;
        case Kind::KeysFloat: return c.keys.size() * 2 + c.f.size() * 4;
    }
    return 0;
}
size_t RawChannelBytes(const PackedChannel& c, uint32_t frames, size_t each) {
    switch (c.kind) {
        case Kind::Empty: return 0;
        case Kind::Constant: return each;
        default: return size_t{frames} * each;
    }
}

}  // namespace

float PackTolerance(ClipCompression c) {
    switch (c) {
        case ClipCompression::None: return 0.f;
        case ClipCompression::Normal: return 0.001f;
        case ClipCompression::Strong: return 0.004f;
    }
    return 0.f;
}

void PackQuat48(Quat q, uint16_t out[3]) {
    q = QuatNormalize(q);
    const float c[4] = {q.x, q.y, q.z, q.w};
    int big = 0;
    for (int k = 1; k < 4; ++k) {
        if (std::fabs(c[k]) > std::fabs(c[big])) big = k;
    }
    const float sign = c[big] < 0 ? -1.f : 1.f;
    uint64_t bits = static_cast<uint64_t>(big);
    for (int k = 0, slot = 0; k < 4; ++k) {
        if (k == big) continue;
        // [-1/√2, 1/√2] -> 0 .. 32767
        const float x = std::clamp(c[k] * sign * kSqrt2 * 0.5f + 0.5f, 0.f, 1.f);
        bits |= static_cast<uint64_t>(std::lround(x * 32767.f)) << (2 + 15 * slot);
        ++slot;
    }
    out[0] = static_cast<uint16_t>(bits);
    out[1] = static_cast<uint16_t>(bits >> 16);
    out[2] = static_cast<uint16_t>(bits >> 32);
}

Quat UnpackQuat48(const uint16_t in[3]) {
    const uint64_t bits = uint64_t{in[0]} | (uint64_t{in[1]} << 16) | (uint64_t{in[2]} << 32);
    const int big = static_cast<int>(bits & 3);
    float c[4] = {};
    float sum = 0;
    for (int k = 0, slot = 0; k < 4; ++k) {
        if (k == big) continue;
        const float x = static_cast<float>((bits >> (2 + 15 * slot)) & 0x7fff) / 32767.f;
        c[k] = (x - 0.5f) * 2.f / kSqrt2;
        sum += c[k] * c[k];
        ++slot;
    }
    c[big] = std::sqrt(std::max(0.f, 1.f - sum));  // (これで長さ 1: 直さない。サンプルは混ぜた後に 1 回だけ直す)
    return {c[0], c[1], c[2], c[3]};
}

void PackClip(Clip& clip, const Skeleton& skeleton, float tolerance) {
    if (!(tolerance > 0) || !clip.packed.empty() || clip.frames > kMaxPackedFrames) return;
    const Reach reach = ReachOf(skeleton);
    std::vector<PackedTrack> packed(clip.tracks.size());
    for (size_t i = 0; i < clip.tracks.size(); ++i) {
        BoneTrack& tr = clip.tracks[i];
        // 長さ 0 / 1 / frames 以外の列 (壊れた): このクリップは圧縮しない。
        for (const size_t len : {tr.t.size(), tr.r.size(), tr.s.size()}) {
            if (len > 1 && len != clip.frames) return;
        }
        const int b = skeleton.Find(tr.bone);
        const float length = b >= 0 ? reach.length[static_cast<size_t>(b)] : 1.f;
        const float share = tolerance / static_cast<float>(b >= 0 ? reach.chain[static_cast<size_t>(b)] : 1);
        packed[i].t = PackVec(tr.t, share);
        packed[i].r = PackRot(tr.r, share / length);
        packed[i].s = PackVec(tr.s, share / length);
    }
    for (BoneTrack& tr : clip.tracks) {
        tr.t = {};
        tr.r = {};
        tr.s = {};
    }
    clip.packed = std::move(packed);
}

void PackClips(std::vector<Clip>& clips, const Skeleton& skeleton) {
    for (Clip& clip : clips) PackClip(clip, skeleton, PackTolerance(clip.bake.compression));
}

Vec3 SamplePackedVec(const PackedChannel& c, float frame, Vec3 rest) {
    switch (c.kind) {
        case Kind::Empty: return rest;
        case Kind::Constant: return {c.constant[0], c.constant[1], c.constant[2]};
        case Kind::Keys16:
        case Kind::KeysFloat: {
            if (c.keys.empty()) return rest;
            size_t k0 = 0, k1 = 0;
            float w = 0;
            Between(c.keys, frame, k0, k1, w);
            auto at = [&](size_t k) -> Vec3 {
                if (c.kind == Kind::KeysFloat) return {c.f[k * 3], c.f[k * 3 + 1], c.f[k * 3 + 2]};
                return {c.lo.x + c.q[k * 3] * c.step.x, c.lo.y + c.q[k * 3 + 1] * c.step.y, c.lo.z + c.q[k * 3 + 2] * c.step.z};
            };
            return Mix(at(k0), at(k1), w);
        }
    }
    return rest;
}

Quat SamplePackedQuat(const PackedChannel& c, float frame, Quat rest) {
    switch (c.kind) {
        case Kind::Empty: return rest;
        case Kind::Constant: return {c.constant[0], c.constant[1], c.constant[2], c.constant[3]};
        case Kind::Keys16:
        case Kind::KeysFloat: {
            if (c.keys.empty()) return rest;
            size_t k0 = 0, k1 = 0;
            float w = 0;
            Between(c.keys, frame, k0, k1, w);
            auto at = [&](size_t k) -> Quat {
                if (c.kind == Kind::KeysFloat) return {c.f[k * 4], c.f[k * 4 + 1], c.f[k * 4 + 2], c.f[k * 4 + 3]};
                return UnpackQuat48(&c.q[k * 3]);
            };
            return MixQ(at(k0), at(k1), w);
        }
    }
    return rest;
}

size_t ClipBytes(const Clip& clip) {
    size_t bytes = 0;
    for (const BoneTrack& t : clip.tracks) bytes += t.t.size() * sizeof(Vec3) + t.r.size() * sizeof(Quat) + t.s.size() * sizeof(Vec3);
    for (const PackedTrack& p : clip.packed) bytes += ChannelBytes(p.t) + ChannelBytes(p.r) + ChannelBytes(p.s);
    return bytes;
}

size_t RawClipBytes(const Clip& clip) {
    if (clip.packed.empty()) return ClipBytes(clip);
    size_t bytes = 0;
    for (const PackedTrack& p : clip.packed) {
        bytes += RawChannelBytes(p.t, clip.frames, sizeof(Vec3)) + RawChannelBytes(p.r, clip.frames, sizeof(Quat)) +
                 RawChannelBytes(p.s, clip.frames, sizeof(Vec3));
    }
    return bytes;
}

bool PackedChannelValid(const PackedChannel& c, uint32_t frames, bool rotation) {
    switch (c.kind) {
        case Kind::Empty:
        case Kind::Constant: return c.keys.empty() && c.q.empty() && c.f.empty();
        case Kind::Keys16:
        case Kind::KeysFloat: {
            if (c.keys.size() < 2 || frames < 2 || frames > kMaxPackedFrames || c.keys.front() != 0 || c.keys.back() != frames - 1) return false;
            for (size_t k = 1; k < c.keys.size(); ++k) {
                if (c.keys[k] <= c.keys[k - 1]) return false;
            }
            if (c.kind == Kind::Keys16) return c.q.size() == c.keys.size() * 3 && c.f.empty();
            return c.q.empty() && c.f.size() == c.keys.size() * (rotation ? 4 : 3);
        }
    }
    return false;
}

}  // namespace rm::space3d
