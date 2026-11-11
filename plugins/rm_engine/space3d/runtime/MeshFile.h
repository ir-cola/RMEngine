#pragma once
// 3D 空間 - reading a model file from disk (the I/O around Mesh.h's pure reading).
// A .gltf's buffer files are looked for next to it (only below its folder).

#include "rm_engine/space3d/runtime/Mesh.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rm::space3d {

// nullopt: *error says why (Japanese, with the file's name). What the model points to (a .gltf's
// buffers, an OBJ's .mtl) is read only from the model's folder and below.
std::optional<MeshData> LoadMeshFile(const std::filesystem::path& file, std::string* error);

// The file a model names by a relative path ("tex/wood.png", "../Textures/wood.png"), as a full
// path - empty when it is not inside `root` (or is written as a full path). Pure (no disk).
std::filesystem::path RelatedFile(const std::filesystem::path& model, std::string_view relative, const std::filesystem::path& root);

}  // namespace rm::space3d
