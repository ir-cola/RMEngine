#include "editor/hub/Updates.h"

namespace rm::editor {

std::vector<UpdateEntry> ParseUpdates(const json::Value& v, bool english) {
    std::vector<UpdateEntry> out;
    if (!v.IsObject() || v.Get("kind").AsString() != "rm_engine.updates") return out;
    for (const auto& u : v.Get("updates").Items()) {
        UpdateEntry e;
        e.version = u.Get("version").AsString();
        e.date = u.Get("date").AsString();
        for (const auto& item : u.Get("items").Items()) {
            const std::string& en = item.Get("en").AsString();
            const std::string& ja = item.Get("ja").AsString();
            const std::string& text = english && !en.empty() ? en : ja;
            if (!text.empty()) e.items.push_back(text);
        }
        if (!e.items.empty()) out.push_back(std::move(e));
    }
    return out;
}

}  // namespace rm::editor
