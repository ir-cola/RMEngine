#pragma once
// .rmpak: 書き出したゲームのアセットを 1 つのファイルにまとめたもの (設計 RME_PLATFORM 5-a「書き出した
// ゲームのアセットとコードを読めないようにする」: ① まとめる ② ゲーム用の形 ③ 暗号化)。持つのは書き出した
// ゲームだけ - プロジェクト自身のファイルは JSON のまま / 元のまま。純粋: バイトは関数を通って出入りし、
// tests/TestPak.cpp でテストする。
//
//   "RMPAK\0\0\0" (8)  format u32  count u32  build u64  indexAt u64  indexSize u64     (ヘッダー、そのまま)
//   エントリーのバイトが続けて並ぶ (それぞれ撹拌済み)
//   索引 (撹拌済み): エントリーごとに  path (u32 の長さ + UTF-8)  at u64  size u64  sign u64
//
// `build` は書き出しの番号: exe にも同じものが渡され (その生成 main)、別の書き出しの pak は
// 断る ("混ぜない" - 設計 5-a、憲法 3)。`sign` は元のバイトの FNV-1a: 壊れた・変わったファイルは
// 読むときに見つかる。
//
// エントリーと索引は、渡された Cipher で暗号化する (format 2: Windows の CNG の AES-256-CTR、
// platform/Crypto PakCipher - 設計の決定。核は素の C++ のまま)。エントリー i はストリーム i、
// 索引はストリーム ~0。★ 鍵は exe の中にある: 何を守り、何を守らないかは TECHNOLOGIES.md に書いてある。

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::pak {

inline constexpr uint32_t kFormat = 2;  // 2: AES (1 は最初のバージョンの XOR - もう読まない)
inline constexpr char kExtension[] = ".rmpak";

struct Entry {
    std::string path;   // 部分の区切りは "/"。ゲームのアセットのフォルダーから ("Assets/Scenes/Main.rmscene")
    std::string bytes;
};

// ストリーム `stream` の `size` バイト (その先頭から) を鍵ストリームと XOR する: 同じ呼び出しが
// 暗号化も復号もする。できなかったときは false。
using Cipher = std::function<bool(char* data, size_t size, uint64_t stream)>;

// これらのエントリーの pak 全体 (パスは重複しないこと。順番は保つ)。暗号が失敗したときは空。
std::string Write(std::span<const Entry> entries, uint64_t build, const Cipher& cipher);

struct IndexEntry {
    std::string path;
    uint64_t at = 0, size = 0, sign = 0;
};

// `read(offset, size, out)` で pak を読む (`size` バイト全部読めたら true) - ファイル、
// またはテストではメモリ。
class Reader {
public:
    using ReadFn = std::function<bool(uint64_t offset, size_t size, char* out)>;
    // pak でない、新しい format、壊れている、別の build のものは false (と *error、日本語)
    // (`build` != 0 で違うとき)。
    bool Open(ReadFn read, uint64_t fileSize, uint64_t build, Cipher cipher, std::string* error);
    const std::vector<IndexEntry>& Entries() const { return entries_; }
    const IndexEntry* Find(std::string_view path) const;
    // エントリーの元のバイト。読めない、または sign が違うときは nullopt (と *error)。
    std::optional<std::string> Read(const IndexEntry& e, std::string* error) const;
    std::optional<std::string> Read(std::string_view path, std::string* error) const;
    uint64_t Build() const { return build_; }

private:
    ReadFn read_;
    Cipher cipher_;
    uint64_t build_ = 0;
    std::vector<IndexEntry> entries_;
};

// バイトの FNV-1a。
uint64_t Sign(std::string_view bytes);

}  // namespace rm::pak
