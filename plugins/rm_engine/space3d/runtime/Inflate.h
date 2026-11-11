#pragma once
// 3D 空間 - undoing deflate compression (RFC 1951; zlib's RFC 1950 wrapper around it), which FBX
// files use for their big arrays. Written here instead of bringing in zlib: only this half is needed
// (about 200 lines). Pure - tested in tests/TestSpace3d.cpp.
//
//   auto bytes = space3d::InflateZlib(compressed, expectedSize);   // nullopt: broken (or too big)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rm::space3d {

// Raw deflate data. At most `maxSize` bytes come out (more = taken as broken).
std::optional<std::vector<uint8_t>> Inflate(std::span<const uint8_t> data, size_t maxSize);
// zlib: a 2-byte header (checked), deflate data, and a checksum at the end (checked).
std::optional<std::vector<uint8_t>> InflateZlib(std::span<const uint8_t> data, size_t maxSize);

}  // namespace rm::space3d
