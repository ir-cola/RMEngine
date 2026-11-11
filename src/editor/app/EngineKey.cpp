#include "editor/app/EngineKey.h"

#include <cstdint>
#include <cwctype>
#include <format>

namespace rm::editor {

std::string EngineKey(const std::filesystem::path& exeDirectory) {
    std::wstring full = exeDirectory.lexically_normal().native();
    while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
    for (auto& ch : full) ch = static_cast<wchar_t>(std::towlower(ch == L'/' ? L'\\' : ch));

    uint64_t h = 1469598103934665603ull;  // FNV-1a 64 over the UTF-16 code units
    for (const wchar_t ch : full) {
        h ^= static_cast<uint16_t>(ch);
        h *= 1099511628211ull;
    }
    // A readable part: the folder's own name, ASCII letters / digits / - _ only (at most 24).
    std::string name;
    const std::wstring last = std::filesystem::path(full).filename().native();
    for (const wchar_t ch : last) {
        if (name.size() >= 24) break;
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_') name.push_back(static_cast<char>(ch));
    }
    if (name.empty()) name = "engine";
    return std::format("{}-{:016x}", name, h);
}

}  // namespace rm::editor
