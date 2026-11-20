#pragma once
// 3D 空間 - deflate 圧縮を戻す (RFC 1951。それを包む zlib の RFC 1950)。FBX のファイルが大きな配列に
// 使う。zlib を持ち込まずここで書いた: 要るのはこの半分だけ (200 行ほど)。
// 純粋 - tests/TestSpace3d.cpp でテストする。
//
//   auto bytes = space3d::InflateZlib(compressed, expectedSize);   // nullopt: 壊れている (または大きすぎる)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rm::space3d {

// 生の deflate データ。出てくるのは多くて `maxSize` バイトまで (それ以上は壊れているとみなす)。
std::optional<std::vector<uint8_t>> Inflate(std::span<const uint8_t> data, size_t maxSize);
// zlib: 2 バイトのヘッダー (調べる)、deflate データ、最後のチェックサム (調べる)。
std::optional<std::vector<uint8_t>> InflateZlib(std::span<const uint8_t> data, size_t maxSize);

}  // namespace rm::space3d
