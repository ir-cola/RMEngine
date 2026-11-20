#pragma once
// UTF-8 <-> UTF-16 の変換 (純粋。Windows API なし)。
// プロジェクトの決まり: データ (JSON、プロジェクトのファイル、ログ) は UTF-8 の std::string。
// Windows API と DirectWrite は UTF-16 の std::wstring を取る。変換はその境目でだけ行う。

#include <filesystem>
#include <string>
#include <string_view>

namespace rm {

static_assert(sizeof(wchar_t) == 2, "RM Engine targets Windows (UTF-16 wchar_t)");

// 不正な並びは U+FFFD になる (おかしなバイト 1 つにつき 1 つ)。
std::wstring Utf8ToWide(std::string_view utf8);
// 単独のサロゲートは U+FFFD になる。
std::string WideToUtf8(std::wstring_view wide);

// std::filesystem::path(std::string) は MSVC では ANSI コードページを使う。
// 必ずこの 2 つを通すこと。
std::filesystem::path PathFromUtf8(std::string_view utf8);
std::string PathToUtf8(const std::filesystem::path& path);

}  // namespace rm
