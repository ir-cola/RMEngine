#pragma once
// 3D 空間 - 描く順番。1 回で描けるもの (インスタンシング) は 1 回にし、GPU はできるだけ状態を
// 変えないようにする。純粋 - tests/TestSpace3d.cpp でテストする。
//
// 1 つの描画はそのキーで決まる: どのシェーダーか、どの画像か (マテリアルのテクスチャとサンプラー -
// 数値はインスタンスごとに来るので違ってよい)、どのメッシュか、そのどの部分か。シェーダー、次に
// マテリアル、次にメッシュの順に並べる: 重い変更 (シェーダー、画像) が一番少なくなる。隣り合った
// 同じキーの描画は 1 つの DrawIndexedInstanced (Renderer3D.cpp) になり、各インスタンスが自分の場所と
// 数値を持つ。
//
//   std::vector<uint32_t> order;
//   SortDraws(keys, order);                 // 安定: 同じキーは来た順のまま
//   size_t draws = CountRuns(keys, order);  // かかる手間: 同じキーの並び 1 つにつき描画 1 回

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rm::space3d {

struct DrawKey {
    uint32_t shader = 0;    // どのシェーダーと状態か
    uint32_t material = 0;  // どの画像とサンプラーか (0: 無し)
    uint64_t mesh = 0;      // どのメッシュか (GpuMesh::serial)
    uint32_t part = 0;      // メッシュのどの部分か
    auto operator<=>(const DrawKey&) const = default;
};

// `order` は、キーで並べた `keys` の添字になる。同じキーは順番を保つ (安定)。
void SortDraws(std::span<const DrawKey> keys, std::vector<uint32_t>& order);

// `order` の順で、同じキーの並びがいくつあるか (1 つの並びが、1 回のインスタンス描画になれる)。
size_t CountRuns(std::span<const DrawKey> keys, std::span<const uint32_t> order);

}  // namespace rm::space3d
