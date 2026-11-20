#pragma once
// 書き出したゲームが受け取る画像 (スプライトのもの - ゲーム制作 × 2D が焼く -、ゲームの UI の画像):
// 絵のファイルそのまま (ゲームの中では WIC が読む PNG / JPEG など。それ以上は圧縮しない) の前に、
// エディターが知っていること - ファイル自身の大きさ (スプライトの世界での大きさはここから決まる。
// 絵を小さく読んだときも)、取り込みの設定の くっきり と 大きさの上限。純粋 - tests/TestBakedImage.cpp でテストする。
//
//   "RMIMAGE\0"  JSON の長さ u32  JSON {"width", "height", "sharp", "maxSize", ...}  それからファイルのバイト
// このバージョンが知らないキーは飛ばす (自己記述型。設計の決まり通り)。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rm::asset {

// Direct2D が 1 つのビットマップに持てる大きさ: それより大きい絵は小さく読む (スプライトは大きさを保つ)。
inline constexpr uint32_t kMaxImageSize = 16384;

struct BakedImageInfo {
    uint32_t width = 0, height = 0;  // ファイルのピクセル数 (0: 不明 - 焼いていない絵)
    bool sharp = false;              // 一番近いピクセルで描く (取り込みの設定の くっきり)
    uint32_t maxSize = 0;            // これより大きくは読まない (取り込みの設定の 大きさの上限。0: 上限なし)
};

struct BakedImage {
    BakedImageInfo info;
    std::string_view picture;  // 絵のファイルのバイト (渡されたバイトの中)
};

std::string WriteBakedImage(const BakedImageInfo& info, std::string_view picture);

// 焼いていない絵のファイル (ヘッダーなし) はそのまま返す (info は全部 0)。ヘッダーが壊れていれば
// nullopt と *error (日本語)。
std::optional<BakedImage> ReadBakedImage(std::string_view bytes, std::string* error);

}  // namespace rm::asset
