#pragma once
// The language of the editor's screens (日本語 / English).
//
// Every string shown on screen is written in Japanese in the code and passed through Tr:
//
//   w::Button(c, "save", r, Tr(L"保存"));
//   p.Text(r, TrF(L"「{}」を削除", name), style);          // with std::format arguments
//
// The Japanese text is the key. English comes from catalogs - JSON objects { "日本語": "English" }:
// the core's (resources/lang/en.json -> <exe>/Lang/en.json) and each plugin's
// (plugins/<author>/<name>/lang/en.json). A string with no English stays Japanese, and
// tools/check_lang.ps1 (run by build.ps1) fails the build when a Tr string has no English.
// Labels of reflected types (RM_PROPERTY(Label=...)) are translated where they are drawn.
//
// Messages that only go to the log (console / editor.log) stay Japanese.

#include "base/Json.h"

#include <format>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rm::editor::lang {

enum class Language { Japanese, English };

// Pure: one language's table (tested in tests/TestLang.cpp).
class Catalog {
public:
    // Adds the entries of { "日本語": "English", ... }. Returns how many were added; bad entries
    // (not strings, empty) are skipped.
    size_t Add(const json::Value& object);
    const std::string* Find(std::string_view japanese) const;
    size_t Size() const { return entries_.size(); }
    void Clear() { entries_.clear(); }

private:
    std::unordered_map<std::string, std::string> entries_;
};

void SetLanguage(Language language);
Language Current();
Catalog& EnglishCatalog();  // filled at start-up (App)

std::wstring Tr(std::wstring_view japanese);
// Marks a string that is kept and translated later, where it is drawn (a panel's or a software's
// title is registered once, the language may change afterwards). check_lang.ps1 sees it.
constexpr const wchar_t* TrKey(const wchar_t* japanese) { return japanese; }
std::string TrU8(std::string_view japanese);

// A translated std::format string. When the translation does not take the same arguments (a
// mistake in a catalog), the Japanese one is used instead of failing.
template <class... A>
std::wstring TrF(std::wstring_view japanese, const A&... args) {
    const std::wstring fmt = Tr(japanese);
    try {
        return std::vformat(fmt, std::make_wformat_args(args...));
    } catch (const std::format_error&) {
        return std::vformat(japanese, std::make_wformat_args(args...));
    }
}

template <class... A>
std::string TrU8F(std::string_view japanese, const A&... args) {
    const std::string fmt = TrU8(japanese);
    try {
        return std::vformat(fmt, std::make_format_args(args...));
    } catch (const std::format_error&) {
        return std::vformat(japanese, std::make_format_args(args...));
    }
}

}  // namespace rm::editor::lang

namespace rm::editor {
using lang::Tr;
using lang::TrKey;
using lang::TrF;
using lang::TrU8;
using lang::TrU8F;
}  // namespace rm::editor
