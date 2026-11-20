#pragma once
// 3D 空間 - 圧縮した絵をディスクに残し、絵の圧縮を 1 回で済ませる (プロジェクトを開くたびに
// やらない)。純粋 (tests/TestSpace3d.cpp でテストする): DDS ファイルのバイト列と、キャッシュ
// ファイルの名前。
//
//   <project>/Cache/Textures/<asset id>_<16 hex digits>.dds
//
// 名前は絵が頼るものから作る: アセットの id、ファイル (か埋め込まれた絵) のバイト列のハッシュ、
// 書かれたままの取り込みの設定、何に使うか、sRGB として読むか、GPU が BC7 を持つか、
// kCacheVersion (圧縮が変わったら上げる)。どれか 1 つでも変われば別の名前になる。古いファイルを
// 間違って読むことはない。フォルダはいつ消してもよく、全部作り直される (共有はしない: git に
// 入れない)。
//
// DDS (DirectDraw Surface) は Direct3D のツールが GPU の絵に使うファイル。128 バイトのヘッダー、
// DXGI フォーマットを名指す "DX10" 拡張、そのあと各ミップレベルのバイト列 (GPU が受け取る形) が続く。

#include "rm_engine/space3d/runtime/BlockCompress.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

inline constexpr uint32_t kCacheVersion = 2;  // 2: 名前に sRGB を入れた

std::vector<uint8_t> WriteDds(const TexturePixels& pixels);
// nullopt: このエンジンが書いた DDS ではない (か途中で切れている): *error に理由 (日本語)。
std::optional<TexturePixels> ReadDds(std::span<const uint8_t> bytes, std::string* error);

// フォーマット (sRGB かどうか) の DXGI フォーマット番号。DDS のヘッダーと Direct3D が使うもの。
uint32_t DxgiFormatOf(TextureFormat f, bool srgb);

// FNV-1a、64 ビット (速い。セキュリティ用ではない。バイト列が変わったかを見るだけ)。
uint64_t HashBytes(std::span<const uint8_t> bytes);
// キャッシュファイルの名前 (上を見よ)。`assetId` の [0-9a-zA-Z_#-] 以外は省く。
std::string TextureCacheName(std::string_view assetId, uint64_t contentHash, std::string_view settings, TextureUse use, bool srgb,
                             bool canBC7);

}  // namespace rm::space3d
