#pragma once
// URL から読む (プラグインの一覧と zip: docs/PLUGIN_DISTRIBUTION.md §5)。
//
//   http:// / https://   Windows の WinHTTP で GET (リダイレクトは追う・プロキシは Windows の設定)
//   file:///C:/x/y.json  ローカルのファイル (試すとき。%XX はほどく)
//   C:\x\y.json / \\server\x   そのままローカルのファイル
//
// どのスレッドからでも (Worker で呼ぶ)。ログも翻訳も使わない: 何が起きたかは Fail と番号で返し、
// 画面の言葉にするのは呼んだ側 (UI スレッド)。

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>

namespace rm::platform {

struct FetchResult {
    enum class Fail { None, BadUrl, Network, Status, TooBig, File, Stopped };
    std::optional<std::string> body;
    Fail fail = Fail::None;
    unsigned long code = 0;  // Windows のエラー番号 (Network / File: ErrorText で言葉に)
    int status = 0;          // HTTP の番号 (Status: 200 以外)
};

FetchResult Fetch(const std::string& url, uint64_t maxBytes, std::stop_token stop = {});
// ローカルのパスか file:// なら、そのパス (それ以外は空)。
std::wstring LocalPathOf(const std::string& url);

}  // namespace rm::platform
