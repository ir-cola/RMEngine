#include "core/paint/Runs.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace rm::paint {

namespace {

template <class T>
std::string Encode(std::span<const T> values) {
    std::string out;
    for (size_t i = 0; i < values.size();) {
        size_t n = 1;
        while (i + n < values.size() && values[i + n] == values[i]) ++n;
        if (!out.empty()) out += ',';
        out += std::to_string(values[i]);
        if (n > 1) out += '*' + std::to_string(n);
        i += n;
    }
    return out;
}

template <class T>
std::vector<T> Decode(std::string_view text, size_t count) {
    std::vector<T> out;
    out.reserve(count);
    size_t pos = 0;
    while (pos < text.size() && out.size() < count) {
        size_t end = text.find(',', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view token = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t star = token.find('*');
        const std::string_view valueText = token.substr(0, star);
        long long value = 0;
        if (std::from_chars(valueText.data(), valueText.data() + valueText.size(), value).ec != std::errc{} || value < 0 ||
            value > static_cast<long long>(std::numeric_limits<T>::max())) {
            value = 0;
        }
        long long n = 1;
        if (star != std::string_view::npos) {
            const std::string_view nText = token.substr(star + 1);
            if (std::from_chars(nText.data(), nText.data() + nText.size(), n).ec != std::errc{} || n < 0) n = 0;
        }
        const size_t take = std::min(static_cast<size_t>(n), count - out.size());
        out.insert(out.end(), take, static_cast<T>(value));
    }
    out.resize(count, T{0});
    return out;
}

}  // namespace

std::string EncodeRuns(std::span<const int> values) { return Encode(values); }
std::string EncodeRuns(std::span<const uint32_t> values) { return Encode(values); }
std::vector<int> DecodeRuns(std::string_view text, size_t count) { return Decode<int>(text, count); }
std::vector<uint32_t> DecodeRunsU32(std::string_view text, size_t count) { return Decode<uint32_t>(text, count); }

}  // namespace rm::paint
