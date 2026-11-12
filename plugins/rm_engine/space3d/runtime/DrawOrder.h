#pragma once
// 3D 空間 - the order draws are made in, so that what can be one draw is one (instancing) and the GPU
// changes its state as seldom as it can. Pure - tested in tests/TestSpace3d.cpp.
//
// A draw is told by its key: which shaders, which pictures (the material's textures and sampler - its
// numbers come per instance and may differ), which mesh, which part of it. Sorted shader first, then
// material, then mesh: the costly changes (shaders, pictures) happen least. Draws with the same key
// next to each other become one DrawIndexedInstanced (Renderer3D.cpp), each instance with its own
// place and numbers.
//
//   std::vector<uint32_t> order;
//   SortDraws(keys, order);                 // stable: equal keys keep the order they came in
//   size_t draws = CountRuns(keys, order);  // what it costs: one draw per run of equal keys

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rm::space3d {

struct DrawKey {
    uint32_t shader = 0;    // which shaders and states
    uint32_t material = 0;  // which pictures and sampler (0: none)
    uint64_t mesh = 0;      // which mesh (GpuMesh::serial)
    uint32_t part = 0;      // which part of the mesh
    auto operator<=>(const DrawKey&) const = default;
};

// `order` becomes the indices of `keys` sorted by key; equal keys keep their order (stable).
void SortDraws(std::span<const DrawKey> keys, std::vector<uint32_t>& order);

// How many runs of equal keys there are, taken in `order` (each run can be one instanced draw).
size_t CountRuns(std::span<const DrawKey> keys, std::span<const uint32_t> order);

}  // namespace rm::space3d
