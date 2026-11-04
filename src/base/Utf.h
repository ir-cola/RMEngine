#pragma once
// UTF-8 <-> UTF-16 conversion (pure; no Windows API).
// Rule of the project: data (JSON, project files, logs) is UTF-8 std::string.
// Windows APIs and DirectWrite take UTF-16 std::wstring. Convert only at that boundary.

#include <filesystem>
#include <string>
#include <string_view>

namespace rm {

static_assert(sizeof(wchar_t) == 2, "RM Engine targets Windows (UTF-16 wchar_t)");

// Invalid sequences become U+FFFD (one per offending byte).
std::wstring Utf8ToWide(std::string_view utf8);
// Lone surrogates become U+FFFD.
std::string WideToUtf8(std::wstring_view wide);

// std::filesystem::path(std::string) uses the ANSI code page on MSVC.
// Always go through these two instead.
std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string PathToUtf8(const std::filesystem::path& path);

}  // namespace rm
