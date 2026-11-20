#pragma once
// 3D 空間 - FBX を読む (Autodesk の形式。Blender、Maya、3ds Max、多くのアセットストアが書く)。
// 純粋: バイト列を渡す - tests/TestSpace3d.cpp でテストする。FBX SDK を使わずここで書く
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
//   読まないもの: スキン、ブレンドシェイプ、アニメーション、カメラとライト、NURBS、FBX 6 以前。

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
