#pragma once
// 3D 空間 - ディスクからモデルファイルを読む (Mesh.h の純粋な読み込みを包む I/O)。
// .gltf のバッファーファイルは、その隣で探す (そのフォルダの下だけ)。

#include "rm_engine/space3d/runtime/Mesh.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rm::space3d {

// nullopt: *error が理由を言う (日本語、ファイル名つき)。モデルが指すもの (.gltf のバッファー、
// OBJ の .mtl) は、モデルのフォルダとその下からだけ読む。アセットの取り込みの設定 (その .meta:
// 大きさ、どちらが上か - core/asset/ImportSettings.h) を適用する。`budget`: 読むのに使ってよい量
// (null: Mesh.h の上限)。例外は投げない: メモリ不足も「読めない」になる。
std::optional<MeshData> LoadMeshFile(const std::filesystem::path& file, std::string* error, ReadBudget* budget = nullptr);

// モデルが相対パスで名指すファイル ("tex/wood.png"、"../Textures/wood.png") を、フルパスで。
// `root` の中でない (またはフルパスで書かれている) ときは空。純粋 (ディスクは使わない)。
std::filesystem::path RelatedFile(const std::filesystem::path& model, std::string_view relative, const std::filesystem::path& root);

}  // namespace rm::space3d
