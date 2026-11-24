#include "editor/console/ConsoleModel.h"

#include <algorithm>

namespace rm::editor::console {

namespace {

char Lower(char ch) { return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch; }

}  // namespace

size_t LevelCounts::Of(log::Level level) const {
    switch (level) {
        case log::Level::Warn: return warn;
        case log::Level::Error: return error;
        case log::Level::Info: break;
    }
    return info;
}

bool Shows(const Filter& f, log::Level level) {
    switch (level) {
        case log::Level::Warn: return f.warn;
        case log::Level::Error: return f.error;
        case log::Level::Info: break;
    }
    return f.info;
}

bool ContainsNoCase(std::string_view text, std::string_view needle) {
    if (needle.empty()) return true;
    // UTF-8 の 2 バイト目以降は 0x80 以上なので、英字だけを小さくしても文字の切れ目は変わらない。
    const auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                                [](char a, char b) { return Lower(a) == Lower(b); });
    return it != text.end();
}

Result Apply(std::span<const log::Entry> entries, const Filter& f) {
    Result r;
    for (size_t i = 0; i < entries.size(); ++i) {
        const log::Entry& e = entries[i];
        if (e.seq <= f.clearedThrough) continue;
        (e.level == log::Level::Error ? r.counts.error : e.level == log::Level::Warn ? r.counts.warn : r.counts.info)++;
        if (Shows(f, e.level) && ContainsNoCase(e.text, f.search)) r.visible.push_back(i);
    }
    return r;
}

BarFit FitBar(float barW, float full, float compact, float searchMin, float searchMax) {
    BarFit fit;
    float avail = barW - full;
    if (avail < searchMin) {
        fit.compact = true;
        avail = barW - compact;
    }
    // ラベルを外しても入らないときは、欄を下限のまま (右がはみ出して切れる)。
    fit.searchW = std::clamp(avail, searchMin, std::max(searchMin, searchMax));
    return fit;
}

}  // namespace rm::editor::console
