#pragma once
// 3D 空間 - reading a model file from disk (the I/O around Mesh.h's pure reading).
// A .gltf's buffer files are looked for next to it (only below its folder).

#include "rm_engine/space3d/runtime/Mesh.h"

#include <filesystem>
#include <optional>
#include <string>

namespace rm::space3d {

// nullopt: *error says why (Japanese, with the file's name).
std::optional<MeshData> LoadMeshFile(const std::filesystem::path& file, std::string* error);

}  // namespace rm::space3d
