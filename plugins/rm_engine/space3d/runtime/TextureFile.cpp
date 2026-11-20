#include "rm_engine/space3d/runtime/TextureFile.h"

#include <cstring>
#include <format>

namespace rm::space3d {

namespace {

constexpr uint32_t kMagic = 0x20534444;  // "DDS "
constexpr uint32_t kFourCcDx10 = 0x30315844;  // "DX10"
constexpr uint32_t kHeaderSize = 124, kPixelFormatSize = 32;
constexpr uint32_t kFlags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000;  // caps、高さ、幅、ピクセルフォーマット、ミップ数
constexpr uint32_t kCaps = 0x1000 | 0x400000 | 0x8;              // テクスチャ、ミップマップ、複合
constexpr uint32_t kDimensionTexture2D = 3;
constexpr size_t kAllHeaders = 4 + kHeaderSize + 20;
constexpr uint32_t kMaxLevels = 16;

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int k = 0; k < 4; ++k) out.push_back(static_cast<uint8_t>(v >> (8 * k)));
}

uint32_t Get32(std::span<const uint8_t> b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;  // リトルエンディアン (x64 / ARM64 の Windows と同じ)
}

struct Known {
    TextureFormat format;
    bool srgb;
    uint32_t dxgi;
};
// DXGI_FORMAT の番号 (dxgiformat.h)。このファイルが Windows のヘッダーを要らないよう、ここに書く。
constexpr Known kFormats[] = {
    {TextureFormat::Rgba8, false, 28}, {TextureFormat::Rgba8, true, 29}, {TextureFormat::BC1, false, 71}, {TextureFormat::BC1, true, 72},
    {TextureFormat::BC3, false, 77},   {TextureFormat::BC3, true, 78},   {TextureFormat::BC4, false, 80}, {TextureFormat::BC5, false, 83},
    {TextureFormat::BC7, false, 98},   {TextureFormat::BC7, true, 99},
};

}  // namespace

uint32_t DxgiFormatOf(TextureFormat f, bool srgb) {
    // BC4 / BC5 は数値だけを持つ: sRGB の形は無い。
    if (f == TextureFormat::BC4 || f == TextureFormat::BC5) srgb = false;
    for (const Known& k : kFormats) {
        if (k.format == f && k.srgb == srgb) return k.dxgi;
    }
    return 0;
}

std::vector<uint8_t> WriteDds(const TexturePixels& p) {
    std::vector<uint8_t> out;
    if (p.levels.empty()) return out;
    const platform::Image& top = p.levels[0];
    Put32(out, kMagic);
    Put32(out, kHeaderSize);
    Put32(out, kFlags | 0x80000);  // + リニアサイズ
    Put32(out, top.height);
    Put32(out, top.width);
    Put32(out, static_cast<uint32_t>(LevelBytes(p.format, top.width, top.height)));
    Put32(out, 0);  // 奥行き
    Put32(out, static_cast<uint32_t>(p.levels.size()));
    for (int i = 0; i < 11; ++i) Put32(out, 0);
    Put32(out, kPixelFormatSize);  // ピクセルフォーマット: 「DX10 ヘッダーを見よ」
    Put32(out, 0x4);               // DDPF_FOURCC
    Put32(out, kFourCcDx10);
    for (int i = 0; i < 5; ++i) Put32(out, 0);
    Put32(out, kCaps);
    for (int i = 0; i < 4; ++i) Put32(out, 0);  // caps2..4、予約
    Put32(out, DxgiFormatOf(p.format, p.srgb));
    Put32(out, kDimensionTexture2D);
    Put32(out, 0);  // misc
    Put32(out, 1);  // 配列サイズ
    Put32(out, 0);  // misc 2
    for (const platform::Image& l : p.levels) out.insert(out.end(), l.pixels.begin(), l.pixels.end());
    return out;
}

std::optional<TexturePixels> ReadDds(std::span<const uint8_t> b, std::string* error) {
    auto fail = [&](std::string why) -> std::optional<TexturePixels> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    if (b.size() < kAllHeaders || Get32(b, 0) != kMagic || Get32(b, 4) != kHeaderSize) return fail("DDS ではありません");
    if (Get32(b, 4 + 80) != kFourCcDx10) return fail("DX10 の見出しのない DDS は読みません");
    const uint32_t height = Get32(b, 12), width = Get32(b, 16), levels = Get32(b, 28);
    const uint32_t dxgi = Get32(b, 128);
    if (width == 0 || height == 0 || width > 16384 || height > 16384 || levels == 0 || levels > kMaxLevels) {
        return fail("DDS の大きさが正しくありません");
    }
    if (Get32(b, 132) != kDimensionTexture2D || Get32(b, 140) != 1) return fail("2D の絵 1 枚の DDS ではありません");
    TexturePixels p;
    bool found = false;
    for (const Known& k : kFormats) {
        if (k.dxgi == dxgi) p.format = k.format, p.srgb = k.srgb, found = true;
    }
    if (!found) return fail(std::format("DDS の形式 {} は読めません", dxgi));
    size_t at = kAllHeaders;
    uint32_t w = width, h = height;
    for (uint32_t i = 0; i < levels; ++i) {
        const size_t size = LevelBytes(p.format, w, h);
        if (size > b.size() - at) return fail("DDS が途中で切れています");
        platform::Image l;
        l.width = w;
        l.height = h;
        l.pixels.assign(b.begin() + static_cast<std::ptrdiff_t>(at), b.begin() + static_cast<std::ptrdiff_t>(at + size));
        p.levels.push_back(std::move(l));
        at += size;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    return p;
}

uint64_t HashBytes(std::span<const uint8_t> bytes) {
    uint64_t h = 14695981039346656037ull;
    for (const uint8_t c : bytes) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string TextureCacheName(std::string_view assetId, uint64_t contentHash, std::string_view settings, TextureUse use, bool srgb,
                             bool canBC7) {
    std::string id;
    for (const char c : assetId) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '#' || c == '-') id += c;
    }
    if (id.empty()) id = "noid";
    // 絵が頼るもの全部を 1 つのハッシュにする。
    const std::string key = std::format("{}|{:016x}|{}|{}|{}|{}|{}", id, contentHash, settings, static_cast<int>(use), srgb ? 1 : 0,
                                        canBC7 ? 1 : 0, kCacheVersion);
    const uint64_t h = HashBytes({reinterpret_cast<const uint8_t*>(key.data()), key.size()});
    return std::format("{}_{:016x}.dds", id, h);
}

}  // namespace rm::space3d
