#pragma once
// 3D 空間 - ブレンドシェイプ (モーフ。docs/ANIMATION.md 7-2-2): 表情・口の形。形ごとに「動く頂点の番号と、その
// 位置・法線の差」を持ち、重みを掛けて足す。順は **形を足す → 骨で動かす** (スキニングの前)。
//   読む:   glTF (primitives の targets・mesh.weights・animations の weights)・FBX (BlendShape / BlendShapeChannel / Shape)
//   焼く:   MeshBake (.rmmesh の MRPH・MANI)
//   描く:   Renderer3D (Compute Shader で、重みの付いた形を足した頂点を作り、それを描く: MeshDraw::morphWeights)
// 純粋 (描画もファイルもない)。CPU の式 MorphVertex は GPU のシェーダーと同じ式 (テストが比べる)。
// tests/TestGltfSkin.cpp・TestSkeleton.cpp でテストする。

#include "base/Math.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rm::space3d {

// 1 つのモデルの形の上限 (読み込み)。超えたものは警告して捨てる。
inline constexpr size_t kMaxMorphTargets = 256;
// 1 つの形の動く頂点の上限。
inline constexpr size_t kMaxMorphVertices = 1'000'000;

struct MorphTarget {
    std::string name;                // glTF: mesh.extras.targetNames (無ければ「形 N」)・FBX: BlendShapeChannel の名前
    std::vector<uint32_t> vertices;  // 動く頂点の番号 (MeshData::vertices の。小さい順)
    std::vector<Vec3> position;      // その頂点の位置の差
    std::vector<Vec3> normal;        // その頂点の法線の差 (無ければ空: 法線は変えない)
};

// クリップの中の形の重みの曲線 (BoneTrack と同じ: 列の長さは 0 / 1 / Clip::frames)。形は名前で指す。
struct MorphTrack {
    std::string target;
    std::vector<float> weights;
};

// 頂点 `index` の位置と法線に、重み `weights` (targets と同じ並び。短ければ足りない分は 0) の形を足す。
// 法線は足した後に長さを 1 に直す (差が 0 なら元のまま)。GPU の Compute Shader と同じ式。
void MorphVertex(uint32_t index, Vec3& position, Vec3& normal, std::span<const float> weights, std::span<const MorphTarget> targets);

// 形の差の一番大きな長さ (見えるかの判定の箱を、重み x これだけ広げる)。
float MorphReach(const MorphTarget& target);

// 形の名前の番号 (無い: -1)。
int FindMorph(std::span<const MorphTarget> targets, std::string_view name);

}  // namespace rm::space3d
