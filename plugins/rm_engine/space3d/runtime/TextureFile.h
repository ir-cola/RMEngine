#pragma once
// 3D 空間 - compressed pictures kept on disk, so a picture is compressed once (not every time the
// project opens). Pure (tested in tests/TestSpace3d.cpp): the DDS file's bytes, and the cache file's
// name.
//
//   <project>/Cache/Textures/<asset id>_<16 hex digits>.dds
//
// The name is made from what the picture depends on: the asset's id, a hash of the file's (or the
// embedded picture's) bytes, the import settings as written, what it is used for, whether the GPU
// has BC7, and kCacheVersion (bumped when the compression changes). Anything else changed = another
// name: an old file is never read by mistake. The folder can be deleted at any time - everything is
// made again (it is not shared: it is not in git).
//
// DDS (DirectDraw Surface) is the file Direct3D tools use for GPU pictures: a 128-byte header, the
// "DX10" extension that names the DXGI format, then each mip level's bytes as the GPU takes them.

#include "rm_engine/space3d/runtime/BlockCompress.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::space3d {

inline constexpr uint32_t kCacheVersion = 1;

std::vector<uint8_t> WriteDds(const TexturePixels& pixels);
// nullopt: not a DDS this engine wrote (or cut short): *error says why (Japanese).
std::optional<TexturePixels> ReadDds(std::span<const uint8_t> bytes, std::string* error);

// The DXGI format number of a format (sRGB or not) - what the DDS header and Direct3D use.
uint32_t DxgiFormatOf(TextureFormat f, bool srgb);

// FNV-1a, 64 bits (quick; not for security - only to see that the bytes changed).
uint64_t HashBytes(std::span<const uint8_t> bytes);
// The cache file's name (see above). `assetId` other than [0-9a-zA-Z_#-] is left out.
std::string TextureCacheName(std::string_view assetId, uint64_t contentHash, std::string_view settings, TextureUse use, bool canBC7);

}  // namespace rm::space3d
