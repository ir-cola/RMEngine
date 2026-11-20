#include "core/pak/Pak.h"

#include <cstring>
#include <format>

namespace rm::pak {

namespace {

constexpr char kMagic[8] = {'R', 'M', 'P', 'A', 'K', 0, 0, 0};
constexpr size_t kHeaderSize = 8 + 4 + 4 + 8 + 8 + 8;
constexpr uint64_t kIndexStream = ~uint64_t{0};  // 索引のストリーム

template <class T>
void Put(std::string& out, T v) {
    for (size_t i = 0; i < sizeof(T); ++i) out.push_back(static_cast<char>(static_cast<uint64_t>(v) >> (8 * i)));
}

template <class T>
bool Get(std::string_view& in, T& v) {
    if (in.size() < sizeof(T)) return false;
    uint64_t x = 0;
    for (size_t i = 0; i < sizeof(T); ++i) x |= static_cast<uint64_t>(static_cast<unsigned char>(in[i])) << (8 * i);
    v = static_cast<T>(x);
    in.remove_prefix(sizeof(T));
    return true;
}

}  // namespace

uint64_t Sign(std::string_view bytes) {
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string Write(std::span<const Entry> entries, uint64_t build, const Cipher& cipher) {
    std::string out(kHeaderSize, '\0');
    std::string index;
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        const uint64_t at = out.size();
        std::string data = e.bytes;
        if (!cipher(data.data(), data.size(), i)) return {};
        out += data;
        Put<uint32_t>(index, static_cast<uint32_t>(e.path.size()));
        index += e.path;
        Put<uint64_t>(index, at);
        Put<uint64_t>(index, e.bytes.size());
        Put<uint64_t>(index, Sign(e.bytes));
    }
    const uint64_t indexAt = out.size();
    if (!cipher(index.data(), index.size(), kIndexStream)) return {};
    out += index;
    std::string header(kMagic, kMagic + 8);
    Put<uint32_t>(header, kFormat);
    Put<uint32_t>(header, static_cast<uint32_t>(entries.size()));
    Put<uint64_t>(header, build);
    Put<uint64_t>(header, indexAt);
    Put<uint64_t>(header, index.size());
    std::memcpy(out.data(), header.data(), kHeaderSize);
    return out;
}

bool Reader::Open(ReadFn read, uint64_t fileSize, uint64_t build, Cipher cipher, std::string* error) {
    auto fail = [&](std::string msg) {
        if (error) *error = std::move(msg);
        entries_.clear();
        return false;
    };
    read_ = std::move(read);
    cipher_ = std::move(cipher);
    entries_.clear();
    std::string header(kHeaderSize, '\0');
    if (fileSize < kHeaderSize || !read_(0, kHeaderSize, header.data())) return fail("アセットのファイルが短すぎます (壊れています)");
    if (std::memcmp(header.data(), kMagic, 8) != 0) return fail("アセットのファイル (.rmpak) ではありません");
    std::string_view in(header);
    in.remove_prefix(8);
    uint32_t format = 0, count = 0;
    uint64_t indexAt = 0, indexSize = 0;
    Get(in, format);
    Get(in, count);
    Get(in, build_);
    Get(in, indexAt);
    Get(in, indexSize);
    if (format > kFormat) return fail(std::format("新しい版のエンジンで書き出したアセットです (形式 {} / この版は {} まで)", format, kFormat));
    if (format != kFormat) return fail(std::format("古い版のエンジンで書き出したアセットです (形式 {})。書き出し直してください", format));
    if (build != 0 && build_ != build) return fail("このゲームの exe と、アセットのファイル (.rmpak) が別の書き出しのものです。同じ書き出しのフォルダのものを使ってください");
    if (indexAt > fileSize || indexSize > fileSize - indexAt || indexSize > (uint64_t{1} << 31)) return fail("アセットのファイルの目次が壊れています");
    std::string index(static_cast<size_t>(indexSize), '\0');
    if (!read_(indexAt, index.size(), index.data())) return fail("アセットのファイルの目次を読めません");
    if (!cipher_(index.data(), index.size(), kIndexStream)) return fail("アセットのファイルの目次を解けません (暗号の機能を使えません)");
    std::string_view ix(index);
    for (uint32_t i = 0; i < count; ++i) {
        IndexEntry e;
        uint32_t len = 0;
        if (!Get(ix, len) || len > ix.size()) return fail("アセットのファイルの目次が壊れています");
        e.path.assign(ix.substr(0, len));
        ix.remove_prefix(len);
        if (!Get(ix, e.at) || !Get(ix, e.size) || !Get(ix, e.sign)) return fail("アセットのファイルの目次が壊れています");
        if (e.at > indexAt || e.size > indexAt - e.at) return fail("アセットのファイルの目次が壊れています");
        entries_.push_back(std::move(e));
    }
    return true;
}

const IndexEntry* Reader::Find(std::string_view path) const {
    for (const IndexEntry& e : entries_) {
        if (e.path == path) return &e;
    }
    return nullptr;
}

std::optional<std::string> Reader::Read(const IndexEntry& e, std::string* error) const {
    std::string data(static_cast<size_t>(e.size), '\0');
    if (!read_ || (e.size > 0 && !read_(e.at, data.size(), data.data()))) {
        if (error) *error = std::format("アセット「{}」を読めません", e.path);
        return std::nullopt;
    }
    if (!cipher_(data.data(), data.size(), static_cast<uint64_t>(&e - entries_.data()))) {
        if (error) *error = std::format("アセット「{}」を解けません (暗号の機能を使えません)", e.path);
        return std::nullopt;
    }
    if (Sign(data) != e.sign) {
        if (error) *error = std::format("アセット「{}」が壊れています (中身が書き出したときと違います)", e.path);
        return std::nullopt;
    }
    return data;
}

std::optional<std::string> Reader::Read(std::string_view path, std::string* error) const {
    const IndexEntry* e = Find(path);
    if (!e) {
        if (error) *error = std::format("アセット「{}」が書き出しに入っていません", path);
        return std::nullopt;
    }
    return Read(*e, error);
}

}  // namespace rm::pak
