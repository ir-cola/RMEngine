#pragma once
// 3D 空間 - ライブラリだけではないエディターの部分: アセット パネルのサムネイルのために小さく描いた
// 3D モデル (核の editor::kThumbnailPoint に差し出す - 核は 3D を何も知らない)。
//
// モデルはシーンビューと同じやり方 (形・マテリアル・画像) で、サムネイルのワーカースレッドで読む
// (PrepareModelThumbnail: ファイルとその画像を読み込む)。それから UI スレッドで、同じ Renderer を使って
// 専用の画面に描く - 正面から、少し右と上から見て、範囲で枠に収め、まわりは透かす - そして GPU から
// 読み戻す (DrawModelThumbnail)。

#include "editor/plugin/Software.h"
#include "platform/Image.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace rm::space3d {

void RegisterEditor(editor::PluginRegistry& registry);

// どのスレッドでもよい: `file` を読み、その画像を読み込む (ファイルの中の画像は `root` の内側のものだけ)。
std::shared_ptr<void> PrepareModelThumbnail(const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                                            std::string* error);
// UI スレッド: Prepare が作ったものを、size x size ピクセルに描く (乗算済みの BGRA)。
bool DrawModelThumbnail(platform::Gfx& gfx, const void* prepared, uint32_t size, platform::Image& out, std::string* error);
// 両方を一度に (UI スレッドで)。
bool MakeModelThumbnail(platform::Gfx& gfx, const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                        platform::Image& out, std::string* error);

}  // namespace rm::space3d
