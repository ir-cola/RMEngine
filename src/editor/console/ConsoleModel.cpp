#include "editor/console/ConsoleModel.h"

#include <algorithm>
#include <format>

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

std::string ClockText(int64_t clockMs) {
    constexpr int64_t kDay = 24ll * 60 * 60 * 1000;
    const int64_t ms = ((clockMs % kDay) + kDay) % kDay;  // (負の値もその日の時刻に)
    return std::format("{:02}:{:02}:{:02}.{:03}", ms / 3'600'000, ms / 60'000 % 60, ms / 1000 % 60, ms % 1000);
}

std::string_view LevelTag(log::Level level) {
    switch (level) {
        case log::Level::Warn: return "WARN";
        case log::Level::Error: return "ERROR";
        case log::Level::Info: break;
    }
    return "INFO";
}

std::string CopyLine(const log::Entry& e) { return std::format("{} [{}] {}", ClockText(e.clockMs), LevelTag(e.level), e.text); }

bool Selection::Contains(uint64_t seq) const { return std::binary_search(seqs.begin(), seqs.end(), seq); }

std::vector<uint64_t> SeqsOf(std::span<const log::Entry> entries, std::span<const size_t> visible) {
    std::vector<uint64_t> out;
    out.reserve(visible.size());
    for (size_t i : visible) out.push_back(entries[i].seq);
    return out;
}

namespace {

void Insert(std::vector<uint64_t>& seqs, uint64_t seq) {
    const auto it = std::lower_bound(seqs.begin(), seqs.end(), seq);
    if (it == seqs.end() || *it != seq) seqs.insert(it, seq);
}

size_t IndexOf(std::span<const uint64_t> order, uint64_t seq) {
    // order は小さい順 (表示順 = 古い順)。
    const auto it = std::lower_bound(order.begin(), order.end(), seq);
    return it != order.end() && *it == seq ? static_cast<size_t>(it - order.begin()) : order.size();
}

}  // namespace

void SelectRange(Selection& sel, std::span<const uint64_t> order, uint64_t from, uint64_t to, bool add) {
    size_t a = IndexOf(order, from), b = IndexOf(order, to);
    if (a == order.size() || b == order.size()) return;
    if (a > b) std::swap(a, b);
    if (!add) sel.seqs.clear();
    for (size_t i = a; i <= b; ++i) Insert(sel.seqs, order[i]);
    sel.anchor = from;
}

void Click(Selection& sel, std::span<const uint64_t> order, uint64_t seq, bool ctrl, bool shift) {
    if (shift && sel.anchor != 0 && IndexOf(order, sel.anchor) != order.size()) {
        SelectRange(sel, order, sel.anchor, seq, ctrl);
        return;
    }
    // (Shift でも、起点が無い・隠れている: ふつうのクリックとして)
    if (ctrl) {
        const auto it = std::lower_bound(sel.seqs.begin(), sel.seqs.end(), seq);
        if (it != sel.seqs.end() && *it == seq) {
            sel.seqs.erase(it);
        } else {
            sel.seqs.insert(it, seq);
        }
    } else {
        sel.seqs.assign(1, seq);
    }
    sel.anchor = seq;
}

void SelectAll(Selection& sel, std::span<const uint64_t> order) {
    sel.seqs.assign(order.begin(), order.end());
    if (!order.empty() && (sel.anchor == 0 || IndexOf(order, sel.anchor) == order.size())) sel.anchor = order.front();
}

void ClearSelection(Selection& sel) {
    sel.seqs.clear();
    sel.anchor = 0;
}

bool Prune(Selection& sel, std::span<const log::Entry> entries, uint64_t clearedThrough) {
    // ログに残っているのは続いた seq の項目 (古いものから落ちる)。
    const uint64_t first = entries.empty() ? ~0ull : std::max(entries.front().seq, clearedThrough + 1);
    const uint64_t last = entries.empty() ? 0 : entries.back().seq;
    auto gone = [&](uint64_t s) { return s < first || s > last; };
    const size_t before = sel.seqs.size();
    std::erase_if(sel.seqs, gone);
    const bool changed = sel.seqs.size() != before || (sel.anchor != 0 && gone(sel.anchor));
    if (sel.anchor != 0 && gone(sel.anchor)) sel.anchor = 0;
    return changed;
}

std::string CopyText(std::span<const log::Entry> entries, std::span<const size_t> visible, const Selection& sel, bool messagesOnly) {
    std::string out;
    bool firstLine = true;
    for (size_t i : visible) {
        const log::Entry& e = entries[i];
        if (!sel.Contains(e.seq)) continue;
        if (!firstLine) out += "\r\n";
        firstLine = false;
        out += messagesOnly ? e.text : CopyLine(e);
    }
    return out;
}

size_t RowAt(float y, float top, float lineH, size_t count) {
    if (count == 0 || lineH <= 0.f || y < top) return 0;
    const size_t row = static_cast<size_t>((y - top) / lineH);
    return std::min(row, count - 1);
}

}  // namespace rm::editor::console
