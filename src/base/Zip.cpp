#include "base/Zip.h"

#include <algorithm>
#include <array>

namespace rm::zip {

namespace {

bool Fail(Problem* problem, Problem why) {
    if (problem) *problem = why;
    return false;
}

// --- deflate (RFC 1951) ------------------------------------------------------------------
// zlib の作者の puff.c と同じ作り: ビットを下から読み、ハフマン符号は長さごとの数と記号の
// 並びで持って、1 ビットずつたどる (速さよりも短く確かなこと。プラグインのパッケージは小さい)。

struct Bits {
    const uint8_t* p = nullptr;
    size_t n = 0, pos = 0;
    uint32_t buf = 0;
    int count = 0;
    bool bad = false;

    uint32_t Get(int need) {
        while (count < need) {
            if (pos >= n) {
                bad = true;
                return 0;
            }
            buf |= static_cast<uint32_t>(p[pos++]) << count;
            count += 8;
        }
        const uint32_t v = buf & ((1u << need) - 1u);
        buf >>= need;
        count -= need;
        return v;
    }
};

constexpr int kMaxBits = 15;

struct Huffman {
    std::array<uint16_t, kMaxBits + 1> count{};
    std::array<uint16_t, 288> symbol{};
};

// 各記号の符号の長さ (0: 使わない) から作る。長さが多すぎる (符号が足りない) と false。
bool Build(Huffman& h, const uint8_t* lengths, int n) {
    h.count.fill(0);
    for (int i = 0; i < n; ++i) h.count[lengths[i]]++;
    if (h.count[0] == n) return true;  // (符号なし: 使われなければよい)
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return false;
    }
    std::array<uint16_t, kMaxBits + 1> offs{};
    for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
    for (int i = 0; i < n; ++i) {
        if (lengths[i] != 0) h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
    }
    return true;
}

int Decode(Bits& s, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code |= static_cast<int>(s.Get(1));
        if (s.bad) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

constexpr uint16_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& s, const Huffman& lens, const Huffman& dists, std::string& out, uint64_t maxSize, Problem* problem) {
    for (;;) {
        int sym = Decode(s, lens);
        if (sym < 0) return Fail(problem, Problem::Corrupt);
        if (sym < 256) {
            if (out.size() >= maxSize) return Fail(problem, Problem::TooBig);
            out.push_back(static_cast<char>(sym));
            continue;
        }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return Fail(problem, Problem::Corrupt);
        const size_t len = kLengthBase[sym] + s.Get(kLengthExtra[sym]);
        const int dsym = Decode(s, dists);
        if (dsym < 0 || dsym >= 30) return Fail(problem, Problem::Corrupt);
        const size_t dist = kDistBase[dsym] + s.Get(kDistExtra[dsym]);
        if (s.bad || dist > out.size()) return Fail(problem, Problem::Corrupt);
        if (out.size() + len > maxSize) return Fail(problem, Problem::TooBig);
        const size_t from = out.size() - dist;
        for (size_t i = 0; i < len; ++i) out.push_back(out[from + i]);  // (重なってよい: 1 バイトずつ)
    }
}

bool Stored(Bits& s, std::string& out, uint64_t maxSize, Problem* problem) {
    s.buf = 0;
    s.count = 0;  // (バイトの境目へ)
    if (s.pos + 4 > s.n) return Fail(problem, Problem::Corrupt);
    const uint32_t len = s.p[s.pos] | (s.p[s.pos + 1] << 8);
    const uint32_t nlen = s.p[s.pos + 2] | (s.p[s.pos + 3] << 8);
    s.pos += 4;
    if (len != (~nlen & 0xffffu)) return Fail(problem, Problem::Corrupt);
    if (s.pos + len > s.n) return Fail(problem, Problem::Corrupt);
    if (out.size() + len > maxSize) return Fail(problem, Problem::TooBig);
    out.append(reinterpret_cast<const char*>(s.p + s.pos), len);
    s.pos += len;
    return true;
}

bool Fixed(Bits& s, std::string& out, uint64_t maxSize, Problem* problem) {
    static const auto tables = [] {
        std::pair<Huffman, Huffman> t;
        uint8_t lengths[288];
        for (int i = 0; i < 144; ++i) lengths[i] = 8;
        for (int i = 144; i < 256; ++i) lengths[i] = 9;
        for (int i = 256; i < 280; ++i) lengths[i] = 7;
        for (int i = 280; i < 288; ++i) lengths[i] = 8;
        Build(t.first, lengths, 288);
        for (int i = 0; i < 30; ++i) lengths[i] = 5;
        Build(t.second, lengths, 30);
        return t;
    }();
    return Codes(s, tables.first, tables.second, out, maxSize, problem);
}

bool Dynamic(Bits& s, std::string& out, uint64_t maxSize, Problem* problem) {
    static constexpr uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = static_cast<int>(s.Get(5)) + 257;
    const int ndist = static_cast<int>(s.Get(5)) + 1;
    const int ncode = static_cast<int>(s.Get(4)) + 4;
    if (s.bad || nlen > 286 || ndist > 30) return Fail(problem, Problem::Corrupt);
    uint8_t lengths[320] = {};
    for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<uint8_t>(s.Get(3));
    Huffman codes;
    if (s.bad || !Build(codes, lengths, 19)) return Fail(problem, Problem::Corrupt);
    std::fill(std::begin(lengths), std::end(lengths), uint8_t{0});
    int index = 0;
    while (index < nlen + ndist) {
        int sym = Decode(s, codes);
        if (sym < 0) return Fail(problem, Problem::Corrupt);
        if (sym < 16) {
            lengths[index++] = static_cast<uint8_t>(sym);
            continue;
        }
        uint8_t len = 0;
        int repeat = 0;
        if (sym == 16) {
            if (index == 0) return Fail(problem, Problem::Corrupt);
            len = lengths[index - 1];
            repeat = 3 + static_cast<int>(s.Get(2));
        } else if (sym == 17) {
            repeat = 3 + static_cast<int>(s.Get(3));
        } else {
            repeat = 11 + static_cast<int>(s.Get(7));
        }
        if (s.bad || index + repeat > nlen + ndist) return Fail(problem, Problem::Corrupt);
        while (repeat-- > 0) lengths[index++] = len;
    }
    if (lengths[256] == 0) return Fail(problem, Problem::Corrupt);  // (終わりの印が無い)
    Huffman lens, dists;
    if (!Build(lens, lengths, nlen) || !Build(dists, lengths + nlen, ndist)) return Fail(problem, Problem::Corrupt);
    return Codes(s, lens, dists, out, maxSize, problem);
}

uint16_t U16(std::string_view b, size_t at) { return static_cast<uint16_t>(static_cast<uint8_t>(b[at]) | (static_cast<uint8_t>(b[at + 1]) << 8)); }
uint32_t U32(std::string_view b, size_t at) { return U16(b, at) | (static_cast<uint32_t>(U16(b, at + 2)) << 16); }

}  // namespace

std::optional<std::string> Inflate(std::string_view deflated, uint64_t maxSize, Problem* problem) {
    if (problem) *problem = Problem::None;
    Bits s;
    s.p = reinterpret_cast<const uint8_t*>(deflated.data());
    s.n = deflated.size();
    std::string out;
    for (bool last = false; !last;) {
        last = s.Get(1) != 0;
        const uint32_t type = s.Get(2);
        if (s.bad) {
            Fail(problem, Problem::Corrupt);
            return std::nullopt;
        }
        bool ok = false;
        if (type == 0) {
            ok = Stored(s, out, maxSize, problem);
        } else if (type == 1) {
            ok = Fixed(s, out, maxSize, problem);
        } else if (type == 2) {
            ok = Dynamic(s, out, maxSize, problem);
        } else {
            Fail(problem, Problem::Corrupt);
        }
        if (!ok) return std::nullopt;
    }
    return out;
}

uint32_t Crc32(std::string_view bytes) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (const char ch : bytes) c = table[(c ^ static_cast<uint8_t>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::optional<std::vector<Entry>> ReadDirectory(std::string_view zip, Problem* problem) {
    if (problem) *problem = Problem::None;
    // 終わりの印 (End of central directory、22 バイト + コメント 65535 まで) を後ろから探す。
    constexpr uint32_t kEnd = 0x06054b50, kCentral = 0x02014b50;
    if (zip.size() < 22) {
        Fail(problem, Problem::NotZip);
        return std::nullopt;
    }
    size_t end = std::string_view::npos;
    const size_t lowest = zip.size() > 22 + 65535 ? zip.size() - 22 - 65535 : 0;
    for (size_t at = zip.size() - 22 + 1; at-- > lowest;) {
        if (U32(zip, at) == kEnd) {
            end = at;
            break;
        }
    }
    if (end == std::string_view::npos) {
        Fail(problem, Problem::NotZip);
        return std::nullopt;
    }
    const uint16_t disk = U16(zip, end + 4), cdDisk = U16(zip, end + 6);
    const uint16_t count = U16(zip, end + 10);
    const uint32_t cdSize = U32(zip, end + 12), cdOffset = U32(zip, end + 16);
    if (disk != 0 || cdDisk != 0 || count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
        Fail(problem, Problem::Unsupported);
        return std::nullopt;
    }
    if (static_cast<uint64_t>(cdOffset) + cdSize > end) {
        Fail(problem, Problem::Corrupt);
        return std::nullopt;
    }
    std::vector<Entry> out;
    size_t at = cdOffset;
    for (uint16_t i = 0; i < count; ++i) {
        if (at + 46 > end || U32(zip, at) != kCentral) {
            Fail(problem, Problem::Corrupt);
            return std::nullopt;
        }
        const uint16_t flags = U16(zip, at + 8);
        Entry e;
        e.method = U16(zip, at + 10);
        e.crc = U32(zip, at + 16);
        e.packed = U32(zip, at + 20);
        e.size = U32(zip, at + 24);
        const uint16_t nameLen = U16(zip, at + 28), extraLen = U16(zip, at + 30), commentLen = U16(zip, at + 32);
        e.localOffset = U32(zip, at + 42);
        if (at + 46 + nameLen > end) {
            Fail(problem, Problem::Corrupt);
            return std::nullopt;
        }
        e.name.assign(zip.data() + at + 46, nameLen);
        e.directory = !e.name.empty() && (e.name.back() == '/' || e.name.back() == '\\');
        if ((flags & 1) != 0 || e.packed == 0xFFFFFFFFu || e.size == 0xFFFFFFFFu || e.localOffset == 0xFFFFFFFFu ||
            (e.method != 0 && e.method != 8)) {
            Fail(problem, Problem::Unsupported);
            return std::nullopt;
        }
        out.push_back(std::move(e));
        at += 46 + static_cast<size_t>(nameLen) + extraLen + commentLen;
    }
    return out;
}

std::optional<std::string> ReadEntry(std::string_view zip, const Entry& e, uint64_t maxSize, Problem* problem) {
    if (problem) *problem = Problem::None;
    if (e.size > maxSize) {
        Fail(problem, Problem::TooBig);
        return std::nullopt;
    }
    constexpr uint32_t kLocal = 0x04034b50;
    if (e.localOffset + 30 > zip.size() || U32(zip, e.localOffset) != kLocal) {
        Fail(problem, Problem::Corrupt);
        return std::nullopt;
    }
    const uint64_t data = e.localOffset + 30 + U16(zip, e.localOffset + 26) + U16(zip, e.localOffset + 28);
    if (data + e.packed > zip.size()) {
        Fail(problem, Problem::Corrupt);
        return std::nullopt;
    }
    const std::string_view packed = zip.substr(data, e.packed);
    std::optional<std::string> out;
    if (e.method == 0) {
        if (e.packed != e.size) {
            Fail(problem, Problem::Corrupt);
            return std::nullopt;
        }
        out = std::string(packed);
    } else {
        out = Inflate(packed, e.size, problem);
        if (!out) return std::nullopt;
    }
    if (out->size() != e.size) {
        Fail(problem, Problem::Corrupt);
        return std::nullopt;
    }
    if (Crc32(*out) != e.crc) {
        Fail(problem, Problem::Crc);
        return std::nullopt;
    }
    return out;
}

}  // namespace rm::zip
