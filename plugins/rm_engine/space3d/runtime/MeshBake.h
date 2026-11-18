#pragma once
// 3D 空間 - a model "baked" for an exported game (design RME_PLATFORM 5-a: ゲーム用の形に変える; the
// readers of glTF / OBJ / FBX never go into a game). The export reads the model exactly as the editor
// does (LoadMeshFile: the import settings' size and up axis applied) and writes the result in this
// engine's own form; the game puts it on the GPU as it is (Renderer::Upload). Pure - tested in
// tests/TestSpace3d.cpp (written and read back the same).
//
//   "RMMESH\0\0"  format u32                                     then chunks, each  tag (4) + size u64 + bytes
//   BNDS  min, max (6 floats)
//   VERT  count u32, then position / normal / uv (8 floats) per vertex
//   INDX  count u32, then u32 per index
//   PART  count u32, then firstIndex, indexCount u32, material i32 per part
//   MATL  count u32, then per material: name, baseColor (4), alpha u8, alphaCutoff, metallic, roughness,
//         normalScale, emissive (4), and its 4 pictures (color, metal / rough, normal, emission) as
//         paths of the exported game's files (empty: none) - the pictures are baked apart (DDS)
//   SHRP  per material 4 bytes: each picture drawn with the nearest texel (くっきり: its import setting)
// A chunk of a tag this version does not know is skipped (keys, as the design's rule for binary data).

#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/TextureSampling.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

inline constexpr uint32_t kBakedMeshFormat = 1;

// A material's 4 pictures (the slots of Texture.h: colour, metal / rough, normal, emission) as paths,
// and how each is read (its import settings: フィルタ・端の扱い・ストリーミング - one byte each, PackSampling).
struct BakedPictures {
    std::array<std::string, 4> path;
    std::array<TextureSampling, 4> sampling{};
};

// `pictures`: one per material of `mesh` (missing ones: none).
std::string WriteBakedMesh(const MeshData& mesh, const std::vector<BakedPictures>& pictures);

// The mesh, its materials' pictures given as images whose `file` is the path (one image per path);
// `samplingOfImage` (optional) gets, per image, how it is read. nullopt and *error (Japanese)
// when it is not a baked mesh, of a newer format, or broken.
std::optional<MeshData> ReadBakedMesh(std::string_view bytes, std::string* error, std::vector<TextureSampling>* samplingOfImage = nullptr);

}  // namespace rm::space3d
