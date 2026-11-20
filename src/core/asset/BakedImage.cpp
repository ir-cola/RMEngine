#include "core/asset/BakedImage.h"

#include "base/Json.h"

#include <algorithm>
#include <cstring>

namespace rm::asset {

namespace {

constexpr char kMagic[8] = {'R', 'M', 'I', 'M', 'A', 'G', 'E', 0};

uint32_t U32(const json::Value& v, std::string_view key) {
    return static_cast<uint32_t>(std::clamp<int64_t>(v.Get(key).AsInt(0), 0, UINT32_MAX));
}

}  // namespace

std::string WriteBakedImage(const BakedImageInfo& info, std::string_view picture) {
    json::Value head;
    head.Set("width", json::Value(static_cast<int64_t>(info.width)));
    head.Set("height", json::Value(static_cast<int64_t>(info.height)));
    head.Set("sharp", json::Value(info.sharp));
    head.Set("maxSize", json::Value(static_cast<int64_t>(info.maxSize)));
    const std::string text = json::Write(head, false);
    std::string out(kMagic, 8);
    const uint32_t n = static_cast<uint32_t>(text.size());
    out.append(reinterpret_cast<const char*>(&n), 4);
    out += text;
    out += picture;
    return out;
}

std::optional<BakedImage> ReadBakedImage(std::string_view bytes, std::string* error) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kMagic, 8) != 0) return BakedImage{{}, bytes};  // 絵をそのまま
    auto fail = [&](const char* why) -> std::optional<BakedImage> {
        if (error) *error = why;
        return std::nullopt;
    };
    uint32_t n = 0;
    if (bytes.size() < 12) return fail("焼いた画像が壊れています (途中で切れています)");
    std::memcpy(&n, bytes.data() + 8, 4);
    if (n > bytes.size() - 12) return fail("焼いた画像が壊れています (途中で切れています)");
    const auto head = json::Parse(bytes.substr(12, n));
    if (!head || !head->IsObject()) return fail("焼いた画像の頭が壊れています");
    BakedImage out;
    out.info.width = U32(*head, "width");
    out.info.height = U32(*head, "height");
    out.info.sharp = head->Get("sharp").AsBool(false);
    out.info.maxSize = U32(*head, "maxSize");
    out.picture = bytes.substr(12 + n);
    return out;
}

}  // namespace rm::asset
