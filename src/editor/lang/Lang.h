#pragma once
// エディターの画面の言語 (日本語 / English)。
//
// 画面に出す文字列はすべてコードに日本語で書いて、Tr を通す:
//
//   w::Button(c, "save", r, Tr(L"保存"));
//   p.Text(r, TrF(L"「{}」を削除", name), style);          // std::format の引数つき
//
// 日本語の文章がキーになる。英語はカタログ - JSON オブジェクト { "日本語": "English" } - から来る:
// 核のもの (resources/lang/en.json -> <exe>/Lang/en.json) と、各プラグインのもの
// (plugins/<author>/<name>/lang/en.json)。英語の無い文字列は日本語のままで、
// tools/check_lang.ps1 (build.ps1 が走らせる) は、英語の無い Tr の文字列があるとビルドを止める。
// リフレクションした型のラベル (RM_PROPERTY(Label=...)) は、描くときに翻訳する。
//
// ログ (コンソール / editor.log) にしか出ないメッセージは日本語のままにする。

#include "base/Json.h"

#include <format>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rm::editor::lang {

enum class Language { Japanese, English };

// 純粋: 1 つの言語の表 (tests/TestLang.cpp でテストする)。
class Catalog {
public:
    // { "日本語": "English", ... } の項目を足す。足した数を返す。悪い項目
    // (文字列でない、空) は飛ばす。
    size_t Add(const json::Value& object);
    const std::string* Find(std::string_view japanese) const;
    size_t Size() const { return entries_.size(); }
    void Clear() { entries_.clear(); }

private:
    std::unordered_map<std::string, std::string> entries_;
};

void SetLanguage(Language language);
Language Current();
Catalog& EnglishCatalog();  // 起動時に埋める (App)

std::wstring Tr(std::wstring_view japanese);
// 取っておいて後で、描くときに翻訳する文字列に印を付ける (パネルやソフトのタイトルは
// 1 回だけ登録するが、その後で言語が変わることがある)。check_lang.ps1 はこれも見る。
constexpr const wchar_t* TrKey(const wchar_t* japanese) { return japanese; }
std::string TrU8(std::string_view japanese);

// 翻訳した std::format 文字列。翻訳が同じ引数を取らないとき (カタログの間違い) は、
// 失敗せずに日本語の方を使う。
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
