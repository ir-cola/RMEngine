#pragma once
// プラグイン ID (設計 RME_PLATFORM 3-a、2026-09-28 決定)。
//
//   * 値は 2 つに分ける: 作者 + 名前。データの中で 1 つの文字列に繋げない
//     ("rm_engine" "render3d" と "akatuki" "render3d" は別のプラグイン)
//   * 使えるのは a-z、0-9、'_' だけ。先頭は英字。末尾に '_' を置かない。"__" も不可
//   * 作者は 3〜20 文字で、Windows のデバイス名 (con, prn, aux, nul, com1-9, lpt1-9) は不可
//   * 名前は 5〜64 文字
//   * 公式の作者は "rm_engine"
//
// これらの決まりを書くのは PluginIdProblem だけ。プラグインは自分の ID をヘッダーに 1 回だけ、
// PluginIdLiteral として書く - 間違った ID はコンパイルが通らない。

#include <string>
#include <string_view>

namespace rm {

namespace plugin_id_detail {

constexpr bool IsLower(char c) { return c >= 'a' && c <= 'z'; }
constexpr bool IsDigit(char c) { return c >= '0' && c <= '9'; }

constexpr const char* PartProblem(std::string_view s, size_t minLen, size_t maxLen, const char* tooShort,
                                  const char* tooLong) {
    if (s.size() < minLen) return tooShort;
    if (s.size() > maxLen) return tooLong;
    if (!IsLower(s[0])) return "先頭は英小文字にしてください";
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (!IsLower(c) && !IsDigit(c) && c != '_') return "使える文字は英小文字・数字・_ だけです";
        if (c == '_' && i + 1 < s.size() && s[i + 1] == '_') return "_ を続けて書くことはできません";
    }
    if (s.back() == '_') return "末尾に _ は置けません";
    return nullptr;
}

constexpr bool IsDeviceName(std::string_view s) {
    if (s == "con" || s == "prn" || s == "aux" || s == "nul") return true;
    if (s.size() == 4 && (s.substr(0, 3) == "com" || s.substr(0, 3) == "lpt") && s[3] >= '1' && s[3] <= '9') return true;
    return false;
}

}  // namespace plugin_id_detail

// ID が正しければ nullptr、そうでなければ理由 (日本語、UTF-8)。
constexpr const char* PluginIdProblem(std::string_view author, std::string_view name) {
    using namespace plugin_id_detail;
    if (const char* p = PartProblem(author, 3, 20, "作者名は 3 文字以上にしてください", "作者名は 20 文字までです")) return p;
    if (IsDeviceName(author)) return "作者名に Windows の予約名 (con / prn / aux / nul / com1〜9 / lpt1〜9) は使えません";
    if (const char* p = PartProblem(name, 5, 64, "プラグイン名は 5 文字以上にしてください", "プラグイン名は 64 文字までです")) {
        return p;
    }
    return nullptr;
}

struct PluginId {
    std::string author;
    std::string name;

    bool operator==(const PluginId&) const = default;
    // メッセージ専用 ("rm_engine / render3d")。1 つの文字列としては決して保存しない。
    std::string Text() const { return author + " / " + name; }
};

// プラグイン自身の ID。ヘッダーに 1 回だけ書く:
//   inline constexpr rm::PluginIdLiteral kPluginId{"rm_engine", "game_production"};
struct PluginIdLiteral {
    std::string_view author;
    std::string_view name;

    consteval PluginIdLiteral(const char* a, const char* n) : author(a), name(n) {
        if (PluginIdProblem(author, name) != nullptr) throw "invalid plugin id: see the rules in core/PluginId.h";
    }
    PluginId Id() const { return {std::string(author), std::string(name)}; }
};

}  // namespace rm
