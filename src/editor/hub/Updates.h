#pragma once
// アップデート情報 (the Hub's page): what changed in each version, in both languages.
// The text is resources/updates.json, built into the exe (IDR_UPDATES), newest first:
//   { "kind": "rm_engine.updates", "updates": [ { "version", "date", "items": [ {"ja", "en"} ] } ] }
// Add an entry there with every change people will notice. Pure - tested in tests/TestLang.cpp.

#include "base/Json.h"

#include <string>
#include <vector>

namespace rm::editor {

struct UpdateEntry {
    std::string version;
    std::string date;
    std::vector<std::string> items;  // in the language asked for (Japanese when the English is missing)
};

std::vector<UpdateEntry> ParseUpdates(const json::Value& v, bool english);

}  // namespace rm::editor
