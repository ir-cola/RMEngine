#pragma once
// 3D 空間 - スキニングの CPU の式 (docs/ANIMATION.md 3-4)。GPU のもの (RendererShaders.cpp の
// SkinnedMeshVS / SkinnedShadowVS) と**同じ式**: テスト (tests/TestSkinning.cpp) が比べる。骨で動いた
// 頂点が要る CPU の計算 (クリックで選ぶ・範囲) もこれを使う。
// 純粋 (描画もファイルもない)。
//
//   スキンの行列 S[b] = 骨の行列 (モデルの空間) x 逆バインド行列 (休みの姿勢では単位行列)
//   頂点の行列 M = sum(ウェイト[k] / 65535 x S[骨[k]])   (3x4 を足し合わせる。ウェイトの合計は直さない)
//   位置 = M x (p, 1)、法線 = normalize(M の 3x3 x n)
//
// 見えるかの判定 (Renderer3D の カリング): 骨ごとの箱 (SkinData::boundsMin / Max。バインドした
// ときの骨の空間) を、バインドしたときのモデルの空間の向きのある箱 (BoneBox) にしておき、
// 描くたびにスキンの行列で動かして、その和をとる (SkinnedBounds)。

#include "base/Math.h"
#include "rm_engine/space3d/runtime/Skeleton.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rm::space3d {

// 3x4 の行列を点に掛ける (dot(行, (p, 1)))。
inline Vec3 BonePoint(const BoneMatrix& m, Vec3 p) {
    auto row = [&](int r) { return m.row[r][0] * p.x + m.row[r][1] * p.y + m.row[r][2] * p.z + m.row[r][3]; };
    return {row(0), row(1), row(2)};
}
// ... 向きに (3x3 の部分だけ)。
inline Vec3 BoneDirection(const BoneMatrix& m, Vec3 d) {
    auto row = [&](int r) { return m.row[r][0] * d.x + m.row[r][1] * d.y + m.row[r][2] * d.z; };
    return {row(0), row(1), row(2)};
}

// 頂点 `v` の行列: 4 本の骨のスキンの行列をウェイトで混ぜたもの。範囲の外の骨は 0 (GPU のバッファーの
// 外を読んだときと同じ。Renderer::Upload は範囲の外の骨を持つメッシュを受け付けない)。
inline BoneMatrix SkinMatrix(const SkinVertex& v, std::span<const BoneMatrix> skin) {
    BoneMatrix out;
    for (auto& r : out.row) r[0] = r[1] = r[2] = r[3] = 0;
    for (int k = 0; k < 4; ++k) {
        if (v.bone[k] >= skin.size()) continue;
        const float w = static_cast<float>(v.weight[k]) / 65535.f;  // (UNORM16 を GPU が読むのと同じ)
        const BoneMatrix& b = skin[v.bone[k]];
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) out.row[r][c] += w * b.row[r][c];
        }
    }
    return out;
}

// 骨で動いた位置 (モデルの空間)。`skin` が空: 休みの姿勢 (そのまま)。
inline Vec3 SkinPosition(const SkinVertex& v, std::span<const BoneMatrix> skin, Vec3 p) {
    if (skin.empty()) return p;
    return BonePoint(SkinMatrix(v, skin), p);
}
// 骨で動いた法線 (長さ 1。3x3 の部分で動かす: 骨の大きさが軸ごとに違うと少しずれる)。
// 長さ 0 になったときは元の法線。
inline Vec3 SkinNormal(const SkinVertex& v, std::span<const BoneMatrix> skin, Vec3 n) {
    if (skin.empty()) return n;
    const Vec3 d = BoneDirection(SkinMatrix(v, skin), n);
    const float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
    if (!(len2 > 1e-20f)) return n;
    const float s = 1.f / std::sqrt(len2);
    return {d.x * s, d.y * s, d.z * s};
}

// 骨 1 本が動かす頂点を囲む箱を、バインドしたときのモデルの空間に置いたもの (向きのある箱):
// 真ん中と、3 本の辺の半分 (骨の軸に沿う)。empty: その骨が動かす頂点が無い。
struct BoneBox {
    Vec3 center{0, 0, 0};
    std::array<Vec3, 3> half{};
    bool empty = true;
};

// SkinData の骨ごとの範囲 (バインドしたときの骨の空間) から BoneBox (骨の数だけ)。逆バインド行列が
// 逆にできない骨と、範囲が無い骨 (min > max) は empty。逆バインド行列が無い骨は単位行列とする。
inline std::vector<BoneBox> BoneBoxes(const SkinData& skin) {
    const size_t n = skin.skeleton.bones.size();
    std::vector<BoneBox> out(n);
    for (size_t b = 0; b < n && b < skin.boundsMin.size() && b < skin.boundsMax.size(); ++b) {
        const Vec3 lo = skin.boundsMin[b], hi = skin.boundsMax[b];
        if (!(lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z)) continue;
        Mat4 bind;  // バインドしたときの骨の空間 -> モデルの空間
        if (b < skin.inverseBind.size()) {
            const auto inv = Inverse(skin.inverseBind[b]);
            if (!inv) continue;
            bind = *inv;
        }
        const BoneMatrix m = ToBoneMatrix(bind);
        BoneBox& box = out[b];
        box.center = BonePoint(m, (lo + hi) * 0.5f);
        const Vec3 e = (hi - lo) * 0.5f;
        box.half = {BoneDirection(m, {e.x, 0, 0}), BoneDirection(m, {0, e.y, 0}), BoneDirection(m, {0, 0, e.z})};
        box.empty = false;
    }
    return out;
}

// 今の姿勢 (`skin`: スキンの行列) で骨が動かす頂点を全部囲む、モデルの空間の箱: 範囲のある骨それぞれの
// 箱をそのスキンの行列で動かしたもの (の軸に沿った箱) の和。範囲のある骨が 1 本も無い: nullopt
// (呼ぶ側はメッシュの範囲を使う)。
inline std::optional<Bounds> SkinnedBounds(std::span<const BoneMatrix> skin, std::span<const BoneBox> boxes) {
    std::optional<Bounds> out;
    const size_t n = std::min(skin.size(), boxes.size());
    for (size_t b = 0; b < n; ++b) {
        const BoneBox& box = boxes[b];
        if (box.empty) continue;
        const Vec3 c = BonePoint(skin[b], box.center);
        Vec3 ext{0, 0, 0};
        for (const Vec3& h : box.half) {
            const Vec3 d = BoneDirection(skin[b], h);
            ext = {ext.x + std::fabs(d.x), ext.y + std::fabs(d.y), ext.z + std::fabs(d.z)};
        }
        const Vec3 lo = c - ext, hi = c + ext;
        if (!out) {
            out = Bounds{lo, hi};
            continue;
        }
        out->min = {std::min(out->min.x, lo.x), std::min(out->min.y, lo.y), std::min(out->min.z, lo.z)};
        out->max = {std::max(out->max.x, hi.x), std::max(out->max.y, hi.y), std::max(out->max.z, hi.z)};
    }
    return out;
}

}  // namespace rm::space3d
