#include "rm_engine/space3d/runtime/Inflate.h"

#include <array>

namespace rm::space3d {

namespace {

constexpr int kMaxBits = 15;

// 符号の長さで与えるハフマン符号 (正準): 長さごとに符号がいくつあるかと、符号の順の記号。
struct Huffman {
    std::array<uint16_t, kMaxBits + 1> count{};
    std::array<uint16_t, 320> symbol{};
};

class Reader {
public:
    Reader(std::span<const uint8_t> in, size_t maxSize) : in_(in), max_(maxSize) {}

    bool Run() {
        bool last = false;
        while (!last) {
            int b = 0;
            if (!Bits(1, b)) return false;
            last = b != 0;
            int type = 0;
            if (!Bits(2, type)) return false;
            bool ok = false;
            switch (type) {
                case 0: ok = Stored(); break;
                case 1: ok = Fixed(); break;
                case 2: ok = Dynamic(); break;
                default: return false;  // 3: ブロックの種類ではない
            }
            if (!ok) return false;
        }
        return true;
    }

    size_t Used() const { return pos_; }  // 読んだ入力のバイト数 (最後の 1 つは丸ごと数える)
    std::vector<uint8_t>& Out() { return out_; }

private:
    bool Bits(int need, int& value) {
        uint32_t v = bitBuf_;
        while (bitCount_ < need) {
            if (pos_ >= in_.size()) return false;
            v |= static_cast<uint32_t>(in_[pos_++]) << bitCount_;
            bitCount_ += 8;
        }
        bitBuf_ = v >> need;
        bitCount_ -= need;
        value = static_cast<int>(v & ((1u << need) - 1));
        return true;
    }

    bool Stored() {
        bitBuf_ = 0;  // 次のバイトへ
        bitCount_ = 0;
        if (pos_ + 4 > in_.size()) return false;
        const unsigned len = in_[pos_] | (in_[pos_ + 1] << 8);
        const unsigned nlen = in_[pos_ + 2] | (in_[pos_ + 3] << 8);
        pos_ += 4;
        if (len != (~nlen & 0xFFFFu) || pos_ + len > in_.size() || out_.size() + len > max_) return false;
        out_.insert(out_.end(), in_.begin() + static_cast<std::ptrdiff_t>(pos_), in_.begin() + static_cast<std::ptrdiff_t>(pos_ + len));
        pos_ += len;
        return true;
    }

    // -1: 壊れている。
    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            int bit = 0;
            if (!Bits(1, bit)) return -1;
            code |= bit;
            const int count = h.count[static_cast<size_t>(len)];
            if (code - count < first) return h.symbol[static_cast<size_t>(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    // 長さが、あり得る数より多くの符号を求めるなら false (不完全な符号は許す: zlib が書く、距離の符号が 1 つだけの場合)。
    static bool Build(Huffman& h, const uint8_t* lengths, int n) {
        h.count.fill(0);
        for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
        if (h.count[0] == n) return true;  // 符号が無い: 復号したものは壊れている
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= h.count[static_cast<size_t>(len)];
            if (left < 0) return false;
        }
        std::array<uint16_t, kMaxBits + 1> offs{};
        for (int len = 1; len < kMaxBits; ++len) offs[static_cast<size_t>(len + 1)] = static_cast<uint16_t>(offs[static_cast<size_t>(len)] + h.count[static_cast<size_t>(len)]);
        for (int s = 0; s < n; ++s) {
            if (lengths[s] != 0) h.symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
        }
        return true;
    }

    bool Codes(const Huffman& lit, const Huffman& dist) {
        static constexpr uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = Decode(lit);
            if (sym < 0) return false;
            if (sym < 256) {
                if (out_.size() >= max_) return false;
                out_.push_back(static_cast<uint8_t>(sym));
            } else if (sym == 256) {
                return true;  // ブロックの終わり
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                int extra = 0;
                if (!Bits(kLenExtra[sym], extra)) return false;
                const size_t len = kLenBase[sym] + static_cast<size_t>(extra);
                const int d = Decode(dist);
                if (d < 0 || d >= 30) return false;
                if (!Bits(kDistExtra[d], extra)) return false;
                const size_t back = kDistBase[d] + static_cast<size_t>(extra);
                if (back > out_.size() || out_.size() + len > max_) return false;
                // 1 バイトずつ: コピーは自分が書いたものと重なってよい (繰り返し)。
                const size_t from = out_.size() - back;
                for (size_t k = 0; k < len; ++k) out_.push_back(out_[from + k]);
            }
        }
    }

    bool Fixed() {
        if (!fixedReady_) {
            uint8_t lengths[288 + 30];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            Build(fixedLit_, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            Build(fixedDist_, lengths, 30);
            fixedReady_ = true;
        }
        return Codes(fixedLit_, fixedDist_);
    }

    bool Dynamic() {
        static constexpr uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = 0, ndist = 0, ncode = 0;
        if (!Bits(5, nlen) || !Bits(5, ndist) || !Bits(4, ncode)) return false;
        nlen += 257;
        ndist += 1;
        ncode += 4;
        if (nlen > 286 || ndist > 30) return false;
        uint8_t lengths[320] = {};
        for (int i = 0; i < ncode; ++i) {
            int v = 0;
            if (!Bits(3, v)) return false;
            lengths[kOrder[i]] = static_cast<uint8_t>(v);
        }
        Huffman lencode;
        if (!Build(lencode, lengths, 19)) return false;
        int at = 0;
        while (at < nlen + ndist) {
            int sym = Decode(lencode);
            if (sym < 0) return false;
            if (sym < 16) {
                lengths[at++] = static_cast<uint8_t>(sym);
                continue;
            }
            int len = 0, repeat = 0;
            if (sym == 16) {
                if (at == 0) return false;  // 繰り返すものが無い
                len = lengths[at - 1];
                if (!Bits(2, repeat)) return false;
                repeat += 3;
            } else if (sym == 17) {
                if (!Bits(3, repeat)) return false;
                repeat += 3;
            } else {
                if (!Bits(7, repeat)) return false;
                repeat += 11;
            }
            if (at + repeat > nlen + ndist) return false;
            while (repeat-- > 0) lengths[at++] = static_cast<uint8_t>(len);
        }
        if (lengths[256] == 0) return false;  // ブロック終了の符号が無い
        Huffman lit, dist;
        if (!Build(lit, lengths, nlen) || !Build(dist, lengths + nlen, ndist)) return false;
        return Codes(lit, dist);
    }

    std::span<const uint8_t> in_;
    size_t max_;
    size_t pos_ = 0;
    uint32_t bitBuf_ = 0;
    int bitCount_ = 0;
    std::vector<uint8_t> out_;
    Huffman fixedLit_, fixedDist_;
    bool fixedReady_ = false;
};

uint32_t Adler32(std::span<const uint8_t> data) {
    uint32_t a = 1, b = 0;
    for (const uint8_t byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

}  // namespace

std::optional<std::vector<uint8_t>> Inflate(std::span<const uint8_t> data, size_t maxSize) {
    Reader r(data, maxSize);
    if (!r.Run()) return std::nullopt;
    return std::move(r.Out());
}

std::optional<std::vector<uint8_t>> InflateZlib(std::span<const uint8_t> data, size_t maxSize) {
    // CMF / FLG: deflate (8)、窓は多くて 32 KB、プリセット辞書なし、そしてこの 2 つの組が 31 の倍数に
    // なるチェック。
    if (data.size() < 6) return std::nullopt;
    const unsigned cmf = data[0], flg = data[1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7 || (flg & 0x20) != 0 || ((cmf << 8) | flg) % 31 != 0) return std::nullopt;
    Reader r(data.subspan(2), maxSize);
    if (!r.Run()) return std::nullopt;
    const size_t end = 2 + r.Used();
    if (end + 4 > data.size()) return std::nullopt;
    const uint32_t want = (uint32_t{data[end]} << 24) | (uint32_t{data[end + 1]} << 16) | (uint32_t{data[end + 2]} << 8) | data[end + 3];
    if (Adler32(r.Out()) != want) return std::nullopt;
    return std::move(r.Out());
}

}  // namespace rm::space3d
