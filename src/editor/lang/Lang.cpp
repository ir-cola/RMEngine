#include "editor/lang/Lang.h"

#include "base/Utf.h"

namespace rm::editor::lang {

namespace {
Language g_language = Language::Japanese;
}  // namespace

size_t Catalog::Add(const json::Value& object) {
    size_t added = 0;
    if (!object.IsObject()) return 0;
    for (const auto& [ja, en] : object.Members()) {
        if (ja.empty() || !en.IsString() || en.AsString().empty()) continue;
        entries_[ja] = en.AsString();
        ++added;
    }
    return added;
}

const std::string* Catalog::Find(std::string_view japanese) const {
    const auto it = entries_.find(std::string(japanese));
    return it == entries_.end() ? nullptr : &it->second;
}

void SetLanguage(Language language) { g_language = language; }
Language Current() { return g_language; }

Catalog& EnglishCatalog() {
    static Catalog c;
    return c;
}

std::string TrU8(std::string_view japanese) {
    if (g_language == Language::English) {
        if (const std::string* en = EnglishCatalog().Find(japanese)) return *en;
    }
    return std::string(japanese);
}

std::wstring Tr(std::wstring_view japanese) {
    if (g_language == Language::Japanese) return std::wstring(japanese);
    // Converted once per string: the screens are drawn often.
    static std::unordered_map<std::wstring, std::wstring> cache;
    static size_t cachedFor = static_cast<size_t>(-1);
    if (cachedFor != EnglishCatalog().Size()) {
        cache.clear();
        cachedFor = EnglishCatalog().Size();
    }
    const std::wstring key(japanese);
    if (const auto it = cache.find(key); it != cache.end()) return it->second;
    const std::string* en = EnglishCatalog().Find(WideToUtf8(key));
    std::wstring out = en ? Utf8ToWide(*en) : key;
    cache.emplace(key, out);
    return out;
}

}  // namespace rm::editor::lang
