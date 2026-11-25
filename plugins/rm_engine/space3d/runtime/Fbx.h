#pragma once
// 3D 空間 - FBX を読む (Autodesk の形式。Blender、Maya、3ds Max、多くのアセットストアが書く)。
// 純粋: バイト列を渡す - tests/TestSpace3d.cpp (スキンとテイクは tests/TestFbxSkin.cpp) でテストする。FBX SDK を使わずここで書く
// (そのライセンスではエンジンに自由に同梱できず、必要なのは形だけだから)。
//
//   バイナリ FBX 7.x (7100 .. 7700。Blender と FBX SDK が書くもの) と ASCII FBX 7.x。どちらも
//   ノードのツリー (名前、プロパティ、子) で、バイナリの大きな配列はたいてい zlib で圧縮されている
//   (Inflate.h)。
//   読むもの: 全てのメッシュ (Geometry) を、それを置くモデル (Model: Lcl Translation / Rotation /
//   Scaling、前 / 後の回転、ピボット、回転の順、ジオメトリのオフセット) とその親で置き、このエンジンの
//   軸 (GlobalSettings: UpAxis / FrontAxis / CoordAxis) とメートル (UnitScaleFactor は 1 単位あたりの
//   センチメートル) に直す。法線と最初の UV セット (ポリゴン頂点ごと / コントロール点ごと、直接 /
//   添字で)。マテリアル (DiffuseColor x DiffuseFactor、Opacity、EmissiveColor x EmissiveFactor -
//   Blender が書くとおり、リニアの光として扱う) と、その拡散色に付いた画像 (モデルの隣のファイル、
//   またはファイルの中に書かれた画像: Video の Content)。
//   ポリゴンは最初の角から三角形に切る。
//   スキン (MeshData::skin。docs/ANIMATION.md 3-2): Deformer (Skin) -> SubDeformer (Cluster) の
//   Indexes / Weights (コントロール点ごと) と Transform / TransformLink (バインドの行列)。骨 = クラスターの
//   骨のモデルと、それをつなぐ祖先 (LimbNode / Null / Root)。親は骨として登録された一番近い祖先。スキンの
//   あるメッシュはバインドしたときの形 (Transform) で置く。スキンのあるファイルの、スキンの無いメッシュと
//   ウェイトの無い点は、そのメッシュのモデルを骨にしてウェイト 1 で付ける。5 本目より後の骨は捨てて警告。
//   骨が kMaxBones を超えたら読めない。
//   テイク (MeshData::clips。スキンのあるファイルだけ): AnimationStack -> 最初の AnimationLayer ->
//   AnimationCurveNode (Lcl Translation / Rotation / Scaling) -> AnimationCurve (直線 / 一定。曲線の補間は
//   直線として)。ファイルの刻み (GlobalSettings の TimeMode / CustomFrameRate。上限 kMaxClipRate) ごとに
//   骨の位置・回転・大きさのサンプルに焼く。長さはテイクの LocalStart / LocalStop (無ければキーの範囲)。
//   骨もクリップも、頂点と同じくファイルの軸と単位をこのエンジンのものに直す。
//   読まないもの: ブレンドシェイプ、2 つ目からのアニメーションのレイヤー、スキンの無いファイルのテイク、
//   カメラとライト、NURBS、FBX 6 以前。

#include "rm_engine/space3d/runtime/Mesh.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

// `budget`: 読むときに使ってよいもの (Mesh.h の ReadBudget。null = その上限)。`fileAxes` / `fileUnits`:
// ファイルの GlobalSettings の軸 / 単位に直すか (false: ファイルの数のまま - 取り込みの設定で座標系や
// 単位を選んだとき)。
std::optional<MeshData> ParseFbx(std::span<const uint8_t> bytes, std::string* error, ReadBudget* budget = nullptr, bool fileAxes = true,
                                 bool fileUnits = true);

// ノードのツリー (両方の FBX がこれに読み込まれる)。単独でテストする。
struct FbxProperty {
    char type = 0;               // バイナリの文字: Y C I L (整数) F D (数) S R (文字 / バイト) f d i l b (配列)
    int64_t integer = 0;         // 整数 (id は 64 ビット)
    double number = 0;           // どんな数でも (整数も)
    std::string text;            // S (名前 "Model::Cube": バイナリの "Cube\0\1Model" は入れ替える) と R (バイト)
    std::vector<double> numbers; // 配列
};

struct FbxNode {
    std::string name;
    std::vector<FbxProperty> props;
    std::vector<FbxNode> children;

    const FbxNode* Child(std::string_view n) const;
};

// 一番上のノード。ファイルが壊れていれば nullopt (と *error、日本語)。`version`: 7400 など。
std::optional<std::vector<FbxNode>> ParseFbxNodes(std::span<const uint8_t> bytes, int* version, std::string* error, ReadBudget* budget = nullptr);

}  // namespace rm::space3d
