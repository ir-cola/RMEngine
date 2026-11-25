#pragma once
// 3D 空間 - 骨のあるモデルのデータ (docs/ANIMATION.md 3-1): 骨の木 (Skeleton)・スキン (頂点ごとの
// 骨とウェイト、骨ごとの逆バインド行列)・クリップ (骨の動きのサンプル)。
//   読む:   glTF (Mesh.cpp)・FBX (Fbx.cpp) -> MeshData::skin / MeshData::clips
//   焼く:   MeshBake (.rmmesh の SKEL / SKIN / IBND / BBOX)・クリップは .rmclip
//   描く:   Renderer3D (GPU スキニング: MeshDraw::bones)
//   姿勢:   クリップ -> 骨の行列 は animation3d プラグイン (ここは型と小さな計算だけ)
// 純粋 (描画もファイルもない)。tests/TestSkeleton.cpp でテストする。
//
// 骨の空間: 骨の行列 (モデルの空間) = 親の骨の行列 x TRSMatrix(その骨の位置・回転・大きさ)。
// スキンの行列 = 骨の行列 x 逆バインド行列 (休みの姿勢では単位行列になる)。
// 頂点の位置 = sum(ウェイト x スキンの行列 x 頂点)。

#include "base/Math.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rm::space3d {

// 1 つのスキンの骨の上限 (読み込み・GPU の 1 回の描画とも)。超えたら読めないとして理由を出す
// (前のエンジンは 512 を超えた骨が黙って 1 本に潰れた)。
inline constexpr uint32_t kMaxBones = 1024;
// クリップの刻み (1 秒のサンプル数) の上限と、ファイルに無いときの値。
inline constexpr float kMaxClipRate = 60.f;
inline constexpr float kDefaultClipRate = 30.f;

// 回転 (単位四元数)。x y z が軸 x sin(角度 / 2)、w が cos(角度 / 2)。glTF と同じ並び。
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    bool operator==(const Quat&) const = default;
};
Quat operator*(Quat a, Quat b);  // a を b の後に (行列の a * b と同じ)
Quat QuatNormalize(Quat q);           // 長さ 0: 回らないもの
Quat QuatConjugate(Quat q);           // 逆の回転 (単位四元数のとき)
float QuatDot(Quat a, Quat b);
Vec3 QuatRotate(Quat q, Vec3 v);
// `axis` まわりに `radians` (右手系: 先端から見て反時計回り。AxisAngle と同じ向き)。
Quat QuatAxisAngle(Vec3 axis, float radians);
Mat4 QuatMatrix(Quat q);  // 回転だけの行列 (長さは 1 に直してから)
// 回転の行列 (各列の長さが 1、行列式 +1) の四元数。w >= 0 のものを返す。
Quat QuatFromMatrix(const Mat4& rotation);

// 骨の位置・回転・大きさ (親の空間で)。
struct BoneTRS {
    Vec3 t{0, 0, 0};
    Quat r{};
    Vec3 s{1, 1, 1};
};
Mat4 TRSMatrix(const BoneTRS& p);  // T * R * S
// 行列を分ける (TRSMatrix の逆)。鏡写し (行列式 < 0) のときは X の大きさを負にする。
// 軸ごとに違う大きさが回っている (シアー) ときは近いもの。
BoneTRS DecomposeTRS(const Mat4& m);

// GPU に送る骨の行列: Mat4 の上 3 行 (3x4。シェーダーは float4 を 3 つ読んで dot(行, float4(p, 1)))。
struct BoneMatrix {
    float row[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
};
BoneMatrix ToBoneMatrix(const Mat4& m);

struct Bone {
    std::string name;
    int parent = -1;  // この骨より前の番号 (-1: 根)
    BoneTRS rest;     // 休みの姿勢 (ファイルのノードの値。クリップに無い骨はこのまま)
};

// 骨の木。**親が先に並ぶ** (1 回のループで親から順に掛けられる)。骨はエンティティにしない。
struct Skeleton {
    std::vector<Bone> bones;
    // 名前で引く (同じ名前が 2 つあるときは最初のもの)。無ければ -1。
    int Find(std::string_view name) const;
};
// 親が先に並んでいて、親の番号がどれも正しいか (読んだ後とテストで)。
bool ParentsFirst(const Skeleton& s);

// 頂点ごとの骨 4 本とウェイト (unorm16: 65535 = 1。合計は 65535)。使わない枠はウェイト 0。
struct SkinVertex {
    std::array<uint16_t, 4> bone{};
    std::array<uint16_t, 4> weight{};
    bool operator==(const SkinVertex&) const = default;
};

// 頂点ごとの (骨, ウェイト) を 4 本にする: 重い 4 本を残し、合計 1 にし直して unorm16 に丸める
// (丸めの差は一番重いものに足す)。`dropped` に、5 本目より後で捨てたものがあれば true。
// ウェイトが全部 0 (や負) のときは骨 `fallback` にウェイト 1。
SkinVertex PackInfluences(std::vector<std::pair<uint32_t, float>> influences, uint32_t fallback, bool* dropped = nullptr);

struct SkinData {
    Skeleton skeleton;
    std::vector<SkinVertex> vertices;  // MeshData::vertices と同じ数・同じ順
    std::vector<Mat4> inverseBind;     // 骨ごと: モデルの空間 -> バインドしたときの骨の空間
    // 骨ごと: その骨が動かす頂点 (ウェイト > 0) を、バインドしたときの骨の空間で囲む箱
    // (見えるかの判定: 今の骨の行列で動かした箱の和)。動かす頂点が無い骨は min > max。
    std::vector<Vec3> boundsMin, boundsMax;
};
// 頂点と inverseBind から boundsMin / boundsMax を作る。
void ComputeBoneBounds(SkinData& skin, const std::vector<Vec3>& positions);

// 骨 1 本の動き。それぞれの列の長さ: 0 = 動かない (休みの姿勢のまま)、1 = ずっと同じ値、
// Clip::frames = 刻みごとのサンプル。骨は**名前**で指す (同じ骨の名前の別のモデルでも使える。
// 名前から番号へはクリップを使う側が 1 回だけ引く)。
struct BoneTrack {
    std::string bone;
    std::vector<Vec3> t;
    std::vector<Quat> r;
    std::vector<Vec3> s;
};

struct Clip {
    std::string name;
    float duration = 0;              // 秒
    float rate = kDefaultClipRate;   // 1 秒のサンプル数
    uint32_t frames = 1;             // サンプルの数 = round(duration x rate) + 1
    std::vector<BoneTrack> tracks;
};
// duration と rate からサンプルの数。
uint32_t ClipFrames(float duration, float rate);

}  // namespace rm::space3d
