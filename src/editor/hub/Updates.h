#pragma once
// アップデート情報 (Hub のページ): 各バージョンで何が変わったか、両方の言語で。
// 文章は resources/updates.json で、exe に組み込まれる (IDR_UPDATES)。新しい順。
//   { "kind": "rm_engine.updates", "updates": [ { "version", "date", "items": [ {"ja", "en"} ] } ] }
// 人が気づく変更はすべて、そこに項目を足す。純粋 (I/O も描画もしない)。tests/TestLang.cpp でテストする。

#include "base/Json.h"

#include <string>
#include <vector>

namespace rm::editor {

struct UpdateEntry {
    std::string version;
    std::string date;
    std::vector<std::string> items;  // 頼まれた言語で (英語が無ければ日本語)
};

std::vector<UpdateEntry> ParseUpdates(const json::Value& v, bool english);

}  // namespace rm::editor
