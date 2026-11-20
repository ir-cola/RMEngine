#include "editor/app/EngineKey.h"

#include <cstdint>
#include <cwctype>
#include <format>

namespace rm::editor {

std::string EngineKey(const std::filesystem::path& exeDirectory) {
    std::wstring full = exeDirectory.lexically_normal().native();
    while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
    for (auto& ch : full) ch = static_cast<wchar_t>(std::towlower(ch == L'/' ? L'\\' : ch));

    uint64_t h = 1469598103934665603ull;  // UTF-16 のコード単位に対する FNV-1a 64
    for (const wchar_t ch : full) {
        h ^= static_cast<uint16_t>(ch);
        h *= 1099511628211ull;
    }
    // 読める部分: フォルダー自身の名前。ASCII の英数字と - _ だけ (24 文字まで)。
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
