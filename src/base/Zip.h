#pragma once
// zip ファイルを読む (純粋。std だけ)。プラグインのパッケージ (段階 1: docs/PLUGIN_DISTRIBUTION.md) に使う。
//
//   zip の終わりにある「中央の一覧」(End of central directory → central directory) から、
//   入っているファイルの名前・大きさ・CRC・場所を読む (ReadDirectory)。
//   1 つのファイルは、その「ローカルの見出し」の後ろのバイトを、そのまま (stored) か
//   deflate (RFC 1951) を自分で解いて取り出す (ReadEntry → Inflate)。CRC-32 で確かめる。
//
// 読めないもの: zip64 (4 GB を超える物)・暗号・deflate 以外の圧縮・複数のディスク。どれも
// Problem::Unsupported で断る。名前のパスが安全かどうかはここでは見ない (PluginIndex.h の
// ZipPathProblem)。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::zip {

enum class Problem {
    None,
    NotZip,       // zip の終わりの印が無い
    Corrupt,      // 一覧や中身が壊れている・途中で切れている
    Unsupported,  // zip64・暗号・知らない圧縮・複数のディスク
    TooBig,       // 決めた大きさを超える
    Crc,          // 取り出した中身の CRC-32 が一覧と違う
};

struct Entry {
    std::string name;  // zip に書いてあるとおり (区切りは / のはず。\ のこともある)
    uint16_t method = 0;  // 0: そのまま、8: deflate
    uint32_t crc = 0;
    uint64_t packed = 0, size = 0;  // zip の中の大きさ・取り出した大きさ
    uint64_t localOffset = 0;
    bool directory = false;  // 名前が / で終わる
};

std::optional<std::vector<Entry>> ReadDirectory(std::string_view zip, Problem* problem);
// 1 つのファイルの中身。`maxSize` を超える物は Problem::TooBig。
std::optional<std::string> ReadEntry(std::string_view zip, const Entry& entry, uint64_t maxSize, Problem* problem);
// 生の deflate (zlib の見出しなし)。`maxSize` を超えたら Problem::TooBig。
std::optional<std::string> Inflate(std::string_view deflated, uint64_t maxSize, Problem* problem);
uint32_t Crc32(std::string_view bytes);

}  // namespace rm::zip
