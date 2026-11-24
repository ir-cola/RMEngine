#pragma once
// アセット パネルの右側 (中身の並び) の計算。純粋: 画面もファイルも使わない (tests/TestAsset.cpp)。
//
//   | 戻る | フォルダ | フォルダ | 画像 | ... |    タイル = 上に絵 (一辺 icon)、下に名前 (2 行まで)
//   | ...                                   |    列の数は幅で決まる。左上から右へ、行ごとに並ぶ
//
// 中身 = 今のフォルダーの直下 (並びは AssetLibrary::Order と同じ: フォルダーが先、名前の順)。
// 検索中 = Assets 全体から、名前に検索の文字を含むもの (階層のツリーと同じ一致: tree::NameMatches)。

#include "core/asset/AssetIndex.h"
#include "ui/Types.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor {

struct AssetGridMetrics {
    float icon = 64;    // 絵の一辺 (DIP)
    float tileW = 0;    // タイルの幅と高さ (絵 + 名前 2 行 + 余白)
    float tileH = 0;
    float gap = 6;      // タイルどうしの間
    int columns = 1;    // 1 行に並ぶ数 (1 以上)
};

inline constexpr float kAssetGridPad = 8;    // 並びの左と上の余白
inline constexpr float kAssetNameLine = 15;  // 名前 1 行の高さ

// 幅 `width` に、絵の一辺 `icon` のタイルを並べるときの大きさ。
AssetGridMetrics MeasureAssetGrid(float width, float icon);
// `i` 番目のタイル (0 から)。`x0` / `y0`: 並びの左上 (余白の外側)。
ui::Rect AssetTileRect(const AssetGridMetrics& m, float x0, float y0, size_t i);
// `count` 個のタイルを並べたときの高さ (上下の余白込み)。
float AssetGridHeight(const AssetGridMetrics& m, size_t count);
// 点 (x, y) にあるタイルの番号。タイルの間や外: -1。
int AssetTileAt(const AssetGridMetrics& m, float x0, float y0, size_t count, float x, float y);

// `entries` の添字 (`order` の順のまま) のうち、フォルダー `folder` ("" = Assets の一番上) の直下のもの。
std::vector<size_t> AssetsInFolder(std::span<const asset::Entry> entries, std::span<const size_t> order, std::string_view folder);
// `entries` の添字 (`order` の順のまま) のうち、ファイル名に `filter` を含むもの (空: 何も)。
std::vector<size_t> AssetsMatching(std::span<const asset::Entry> entries, std::span<const size_t> order, std::string_view filter);
// パンくずリストの段: "a/b/c" -> {"a", "a/b", "a/b/c"} (それぞれのフォルダーのパス)。"" -> {}。
std::vector<std::string> AssetFolderSteps(std::string_view folder);

}  // namespace rm::editor
