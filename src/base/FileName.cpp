#include "base/FileName.h"

#include "base/Utf.h"

#include <string>

namespace rm {

FileNameError ValidateFileName(std::string_view utf8, size_t maxChars) {
    if (utf8.empty()) return FileNameError::Empty;
    const std::wstring w = Utf8ToWide(utf8);
    if (w.size() > maxChars) return FileNameError::TooLong;
    for (const wchar_t c : w) {
        if (c < 0x20) return FileNameError::InvalidChar;
        switch (c) {
            case L'<': case L'>': case L':': case L'"': case L'/': case L'\\': case L'|': case L'?': case L'*':
                return FileNameError::InvalidChar;
            default: break;
        }
    }
    if (w.front() == L' ') return FileNameError::LeadingSpace;
    if (w.back() == L'.' || w.back() == L' ') return FileNameError::TrailingDotOrSpace;

    // Reserved device names apply to the part before the first dot ("con.txt" is reserved too).
    std::wstring stem = w.substr(0, w.find(L'.'));
    while (!stem.empty() && stem.back() == L' ') stem.pop_back();
    for (auto& c : stem) {
        if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
    }
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL") return FileNameError::ReservedName;
    if (stem.size() == 4 && (stem.starts_with(L"COM") || stem.starts_with(L"LPT")) && stem[3] >= L'1' && stem[3] <= L'9') {
        return FileNameError::ReservedName;
    }
    return FileNameError::None;
}

}  // namespace rm
