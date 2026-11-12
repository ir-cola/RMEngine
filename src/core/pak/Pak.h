#pragma once
// .rmpak: the assets of an exported game packed into one file (design RME_PLATFORM 5-a "書き出した
// ゲームのアセットとコードを読めないようにする": ① まとめる ② ゲーム用の形 ③ 暗号化). Only exported
// games have one - a project's own files stay JSON / as they were. Pure: the bytes come and go through
// functions, tested in tests/TestPak.cpp.
//
//   "RMPAK\0\0\0" (8)  format u32  count u32  build u64  indexAt u64  indexSize u64     (the header, as is)
//   the entries' bytes, one after another (each scrambled)
//   the index (scrambled): per entry  path (u32 length + UTF-8)  at u64  size u64  sign u64
//
// `build` is a number of the export: the exe is given the same one (its generated main), and a pak of
// another export is refused ("混ぜない" - design 5-a, rule 3). `sign` is the FNV-1a of the plain bytes:
// a broken or changed file is found when it is read.
//
// ★ The scrambling is the first version's simple one (a keystream from a fixed key, the build number
// and the entry's place: XOR). It only keeps the files from being opened as they are. The design's
// AES (Windows' BCrypt) replaces it later (the format number goes up then).

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::pak {

inline constexpr uint32_t kFormat = 1;
inline constexpr char kExtension[] = ".rmpak";

struct Entry {
    std::string path;   // "/" between the parts, from the game's folder of assets ("Assets/Scenes/Main.rmscene")
    std::string bytes;
};

// The whole pak for these entries (paths must be unique; the order is kept).
std::string Write(std::span<const Entry> entries, uint64_t build);

struct IndexEntry {
    std::string path;
    uint64_t at = 0, size = 0, sign = 0;
};

// Reads a pak through `read(offset, size, out)` (true when it read all `size` bytes) - a file, or
// memory in the tests.
class Reader {
public:
    using ReadFn = std::function<bool(uint64_t offset, size_t size, char* out)>;
    // False (and *error, Japanese) when it is not a pak, a newer format, broken, or of another build
    // (`build` != 0 and different).
    bool Open(ReadFn read, uint64_t fileSize, uint64_t build, std::string* error);
    const std::vector<IndexEntry>& Entries() const { return entries_; }
    const IndexEntry* Find(std::string_view path) const;
    // The plain bytes of an entry; nullopt (and *error) when it cannot be read or its sign is wrong.
    std::optional<std::string> Read(const IndexEntry& e, std::string* error) const;
    std::optional<std::string> Read(std::string_view path, std::string* error) const;
    uint64_t Build() const { return build_; }

private:
    ReadFn read_;
    uint64_t build_ = 0;
    std::vector<IndexEntry> entries_;
};

// The FNV-1a of the bytes.
uint64_t Sign(std::string_view bytes);

}  // namespace rm::pak
