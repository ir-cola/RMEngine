#pragma once
// 3D 空間 - the shape of a model read from a file: vertices (position, normal, UV) and triangles.
// Pure: the bytes are given (MeshFile.h reads them from disk) - tested in tests/TestSpace3d.cpp.
//
//   glTF 2.0: .gltf (JSON; buffers in other files or written inside as data: URIs) and .glb (one
//             file). Every mesh of the default scene, placed by its nodes (matrix or T / R / S),
//             becomes one mesh in the model's own space. Triangles, strips and fans.
//   OBJ:      v / vt / vn / f (polygons are cut into triangles; negative numbers count from the end).
//
// Materials (the colour of each part): glTF's pbrMetallicRoughness baseColorFactor and
// baseColorTexture (a picture file next to the model, one inside the .glb, or a data: URI); OBJ's
// .mtl (mtllib / usemtl: Kd, d and map_Kd). The triangles are sorted by material into parts
// (MeshPart: one draw each). Pictures are not decoded here (MeshImage keeps the bytes or the file's
// name): that needs Windows (WIC) - see Texture.h.
//
// Not read (yet): the rest of a material (metal, roughness, normal maps, emission), glTF samplers
// (every picture repeats), skins, morph targets, animation, sparse accessors, Draco.
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

// A picture a material is painted with: a file next to the model (`file`: relative to the model's
// folder, '/' between folders, percent signs decoded) or the picture's bytes written inside the
// model (`bytes`: a .glb's buffer, a data: URI). Not decoded yet.
struct MeshImage {
    std::string file;
    std::vector<uint8_t> bytes;
    std::string name;  // for messages: the file's name, or the image's name / "画像 2"
};

// How a material's alpha is taken (glTF alphaMode): not at all (the usual: a picture's alpha
// channel is ignored), as a cut-out (less than alphaCutoff is a hole), or see-through.
enum class AlphaMode : uint8_t { Opaque, Mask, Blend };

// The colour of a part: baseColor (as shown on the screen, 0..1; glTF's linear factor is turned
// into it) times the picture at the vertex's UV (image: index into MeshData::images, -1 = none).
struct MeshMaterial {
    std::string name;
    Color baseColor{1, 1, 1, 1};
    int image = -1;
    AlphaMode alpha = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
};

// Triangles [firstIndex, firstIndex + indexCount) of `indices`, all of one material (-1: none -
// drawn white, so the draw's colour shows).
struct MeshPart {
    uint32_t firstIndex = 0, indexCount = 0;
    int material = -1;
    bool operator==(const MeshPart&) const = default;
};

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;  // 3 per triangle, front counter-clockwise
    Vec3 min{0, 0, 0}, max{0, 0, 0};  // the bounds (the box along the model's own axes that holds it)
    std::vector<MeshPart> parts;      // cover `indices` in order; one per material used (at least one)
    std::vector<MeshMaterial> materials;
    std::vector<MeshImage> images;
    std::vector<std::string> warnings;  // read anyway, but something was missing (an OBJ's .mtl) - Japanese
};

// Another file a model points to (a .gltf's buffer "model.bin", an OBJ's "model.mtl"), by the path
// relative to the model's folder (percent signs already decoded, '/' between folders); nullopt
// when it cannot be read.
using ReadRelated = std::function<std::optional<std::vector<uint8_t>>(std::string_view uri)>;

// By the file's extension (".gltf" / ".glb" / ".obj", any case).
std::optional<MeshData> ParseMesh(std::string_view extension, std::span<const uint8_t> bytes, const ReadRelated& read,
                                  std::string* error);
// .gltf or .glb (told apart by the first bytes).
std::optional<MeshData> ParseGltf(std::span<const uint8_t> bytes, const ReadRelated& read, std::string* error);
// `read` finds the .mtl files (null: no materials).
std::optional<MeshData> ParseObj(std::string_view text, const ReadRelated& read, std::string* error);
inline std::optional<MeshData> ParseObj(std::string_view text, std::string* error) { return ParseObj(text, nullptr, error); }

// One .mtl file's materials (folder: where the .mtl is, relative to the model's folder - "" or
// "mats/" - so its pictures can be found from the model). Lines it does not know are skipped.
std::vector<MeshMaterial> ParseMtl(std::string_view text, std::string_view folder, std::vector<MeshImage>& images);

// A colour component from linear light (glTF's factors) to the screen's (sRGB) 0..1.
float LinearToScreen(float linear);

// The triangles sorted by material (stable: the order inside a material stays), and the parts that
// say where each material's run is. `materialOfTriangle` has one entry per triangle.
void SortByMaterial(MeshData& mesh, std::span<const int> materialOfTriangle);

// Smooth normals from the triangles (each triangle counts by its area); a vertex on no triangle
// points up.
void ComputeNormals(MeshData& mesh);
void ComputeBounds(MeshData& mesh);

// Pieces of reading, tested on their own.
std::optional<std::vector<uint8_t>> DecodeBase64(std::string_view text);
std::string DecodePercent(std::string_view uri);  // "my%20model.bin" -> "my model.bin"

}  // namespace rm::space3d
