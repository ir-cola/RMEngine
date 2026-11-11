#pragma once
// 3D 空間 - the shape of a model read from a file: vertices (position, normal, UV) and triangles.
// Pure: the bytes are given (MeshFile.h reads them from disk) - tested in tests/TestSpace3d.cpp.
//
//   glTF 2.0: .gltf (JSON; buffers in other files or written inside as data: URIs) and .glb (one
//             file). Every mesh of the default scene, placed by its nodes (matrix or T / R / S),
//             becomes one mesh in the model's own space. Triangles, strips and fans.
//   OBJ:      v / vt / vn / f (polygons are cut into triangles; negative numbers count from the end).
//
// Not read (yet): materials and textures, skins, morph targets, animation, sparse accessors, Draco.
// Normals that are missing are made from the triangles (smooth). The front of a triangle is
// counter-clockwise, as in both formats; UV (0, 0) is the top left of a picture, as in glTF and
// Direct3D (OBJ's V, which goes up, is turned over).
//
//   std::string error;
//   auto mesh = space3d::ParseMesh(".glb", bytes, readRelated, &error);   // nullopt: error says why (Japanese)

#include "base/Math.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;  // 3 per triangle, front counter-clockwise
    Vec3 min{0, 0, 0}, max{0, 0, 0};  // the bounds (the box along the model's own axes that holds it)
};

// Another file a .gltf points to (its buffer "model.bin"), by the uri as written (percent signs
// already decoded); nullopt when it cannot be read.
using ReadRelated = std::function<std::optional<std::vector<uint8_t>>(std::string_view uri)>;

// By the file's extension (".gltf" / ".glb" / ".obj", any case).
std::optional<MeshData> ParseMesh(std::string_view extension, std::span<const uint8_t> bytes, const ReadRelated& read,
                                  std::string* error);
// .gltf or .glb (told apart by the first bytes).
std::optional<MeshData> ParseGltf(std::span<const uint8_t> bytes, const ReadRelated& read, std::string* error);
std::optional<MeshData> ParseObj(std::string_view text, std::string* error);

// Smooth normals from the triangles (each triangle counts by its area); a vertex on no triangle
// points up.
void ComputeNormals(MeshData& mesh);
void ComputeBounds(MeshData& mesh);

// Pieces of reading, tested on their own.
std::optional<std::vector<uint8_t>> DecodeBase64(std::string_view text);
std::string DecodePercent(std::string_view uri);  // "my%20model.bin" -> "my model.bin"

}  // namespace rm::space3d
