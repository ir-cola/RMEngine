#include "base/Utf.h"

namespace rm {

std::wstring Utf8ToWide(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        size_t len = 0;
        if (c < 0x80) {
            out.push_back(static_cast<wchar_t>(c));
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            len = 4;
        } else {
            out.push_back(L'\xFFFD');
            ++i;
            continue;
        }
        bool ok = i + len <= n;
        for (size_t k = 1; ok && k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
            } else {
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        if (ok) {
            const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
            if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) ok = false;
        }
        if (!ok) {
            out.push_back(L'\xFFFD');
            ++i;
            continue;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
        i += len;
    }
    return out;
}

static void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string WideToUtf8(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    const size_t n = w.size();
    for (size_t i = 0; i < n; ++i) {
        const char32_t c = static_cast<char16_t>(w[i]);
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 < n) {
                const char32_t d = static_cast<char16_t>(w[i + 1]);
                if (d >= 0xDC00 && d <= 0xDFFF) {
                    AppendUtf8(out, 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00));
                    ++i;
                    continue;
                }
            }
            AppendUtf8(out, 0xFFFD);
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            AppendUtf8(out, 0xFFFD);
        } else {
            AppendUtf8(out, c);
        }
    }
    return out;
}

std::filesystem::path PathFromUtf8(std::string_view utf8) { return std::filesystem::path(Utf8ToWide(utf8)); }

std::string PathToUtf8(const std::filesystem::path& path) { return WideToUtf8(path.native()); }

}  // namespace rm
