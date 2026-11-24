#include "editor/console/ConsolePanel.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/console/ConsoleModel.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"

#include <algorithm>
#include <format>
#include <vector>

namespace rm::editor {

using ui::Rect;

namespace {

constexpr char kScrollId[] = "console";
constexpr char kSearchId[] = "console.search";
constexpr float kBarH = 28.f;       // ツールバーの高さ
constexpr float kItemH = 22.f;      // その中のボタン・チェック・欄の高さ
constexpr float kLineH = 20.f;      // ログの 1 行
constexpr float kGroupGap = 14.f;   // クリア | 自動スクロール | レベル | 検索 の間
constexpr float kLevelGap = 10.f;   // 情報 / 警告 / エラー の間
constexpr float kSearchIcon = 16.f;
constexpr float kSearchMin = 90.f, kSearchMax = 220.f;

// レベルの色: 行の文字と、ツールバーの数の 1 か所。
ui::Color LevelColor(log::Level level) {
    switch (level) {
        case log::Level::Error: return theme::Danger;
        case log::Level::Warn: return theme::Warn;
        case log::Level::Info: break;
    }
    return theme::Text;
}

ui::TextStyle LineStyle(ui::Color color) {
    ui::TextStyle s;
    s.size = 12;
    s.color = color;
    return s;
}

console::Filter MakeFilter(const ConsoleSettings& s, const std::string& search, uint64_t clearedThrough) {
    console::Filter f;
    f.info = s.showInfo;
    f.warn = s.showWarn;
    f.error = s.showError;
    f.search = search;
    f.clearedThrough = clearedThrough;
    return f;
}

}  // namespace

json::Value ConsolePanel::SaveState() const { return reflect::Save(settings_, &unknowns_); }

void ConsolePanel::LoadState(const json::Value& v) {
    if (!v.IsObject()) return;
    reflect::Warnings warnings;
    reflect::Load(settings_, v, &unknowns_, &warnings);
    for (const auto& msg : warnings) log::Warn("コンソールの設定: {}", msg);
}

void ConsolePanel::DrawToolbar(ui::Context& c, const Rect& bar, size_t info, size_t warn, size_t error, uint64_t lastSeq) {
    auto& p = c.P();
    const std::wstring clearLabel = Tr(L"クリア");
    const std::wstring autoLabel = Tr(L"自動スクロール");
    struct Level {
        const char* id;
        std::wstring label;
        bool* shown;
        size_t count;
        log::Level level;
    };
    Level levels[] = {
        {"console.info", Tr(L"情報"), &settings_.showInfo, info, log::Level::Info},
        {"console.warn", Tr(L"警告"), &settings_.showWarn, warn, log::Level::Warn},
        {"console.error", Tr(L"エラー"), &settings_.showError, error, log::Level::Error},
    };

    // 幅: 検索の欄より前のもの (ラベルあり / なし)。狭いときは欄が先に縮み、次にラベルが外れる。
    const float clearW = std::max(52.f, p.TextWidth(clearLabel, theme::FontSize) + 24.f);
    float full = clearW + kGroupGap + w::CheckboxCountWidth(c, autoLabel) + kGroupGap;
    float compact = clearW + kGroupGap + w::CheckboxCountWidth(c, {}) + kGroupGap;
    std::wstring counts[3];
    for (size_t i = 0; i < 3; ++i) {
        counts[i] = std::format(L"{}", levels[i].count);
        full += w::CheckboxCountWidth(c, levels[i].label, counts[i]) + (i < 2 ? kLevelGap : kGroupGap);
        compact += w::CheckboxCountWidth(c, {}, counts[i]) + (i < 2 ? kLevelGap : kGroupGap);
    }
    full += kSearchIcon + 4.f;
    compact += kSearchIcon + 4.f;
    const console::BarFit fit = console::FitBar(bar.w - 16.f, full, compact, kSearchMin, kSearchMax);

    c.PushClip(bar);
    const float y = bar.y + (bar.h - kItemH) * 0.5f;
    float x = bar.x + 8.f;

    const Rect clearRect{x, y, clearW, kItemH};
    if (w::Button(c, "console.clear", clearRect, clearLabel)) {
        clearedThrough_ = std::max(clearedThrough_, lastSeq);
        c.RequestRedraw();
    }
    if (c.IsHovered(clearRect)) w::Tooltip(c, Tr(L"ここまでのログをコンソールから消します (ログのファイルには残ります)"));
    x += clearW + kGroupGap;

    const std::wstring_view autoShown = fit.compact ? std::wstring_view() : std::wstring_view(autoLabel);
    const Rect autoRect{x, y, w::CheckboxCountWidth(c, autoShown), kItemH};
    if (w::Checkbox(c, "console.auto", autoRect, settings_.autoScroll, autoShown)) {
        if (settings_.autoScroll) {
            for (auto& entry : follow_) entry.second.seenSeq = ~0ull;  // どのウィンドウでも、次に描くとき一番下へ
        }
        c.RequestRedraw();
    }
    if (c.IsHovered(autoRect)) {
        w::Tooltip(c, Tr(L"自動スクロール: 新しいログが来たら一番下へ動きます。上へスクロールしている間は止まり、一番下に戻るとまた動きます"));
    }
    x = autoRect.Right() + kGroupGap;

    for (size_t i = 0; i < 3; ++i) {
        const Level& lv = levels[i];
        const std::wstring_view label = fit.compact ? std::wstring_view() : std::wstring_view(lv.label);
        const Rect rect{x, y, w::CheckboxCountWidth(c, label, counts[i]), kItemH};
        const ui::Color countColor = lv.count == 0 ? theme::TextFaint : LevelColor(lv.level);
        if (w::CheckboxCount(c, lv.id, rect, *lv.shown, label, counts[i], countColor)) c.RequestRedraw();
        if (fit.compact && c.IsHovered(rect)) w::Tooltip(c, lv.label);
        x = rect.Right() + (i < 2 ? kLevelGap : kGroupGap);
    }

    w::DrawIcon(c, {x, y, kSearchIcon, kItemH}, w::Icon::Search, theme::TextDim, 14);
    x += kSearchIcon + 4.f;
    if (c.Focused() == c.MakeId(kSearchId) && c.KeyPressed(ui::Key::Escape)) {
        search_.clear();
        c.ClearFocus();
        c.RequestRedraw();
    }
    w::TextField(c, kSearchId, {x, y, fit.searchW, kItemH}, search_, Tr(L"検索…"));
    c.PopClip();
    w::Separator(c, bar.x, bar.Bottom() - 1.f, bar.w);
}

void ConsolePanel::Draw(ui::Context& c, const Rect& r) {
    auto& p = c.P();
    const std::vector<log::Entry> entries = log::Snapshot();
    const uint64_t last = entries.empty() ? 0 : entries.back().seq;

    Rect area = r;
    const Rect bar = ui::TakeTop(area, kBarH);
    {
        // 数はツールバーを描く前のもの。ツールバーで変わったら (クリア・表示・検索)、下で数え直す。
        const console::Result before = console::Apply(entries, MakeFilter(settings_, search_, clearedThrough_));
        DrawToolbar(c, bar, before.counts.info, before.counts.warn, before.counts.error, last);
    }
    const console::Result shown = console::Apply(entries, MakeFilter(settings_, search_, clearedThrough_));

    const Rect view = area.Inset(0, 2, 0, 2);
    const float content = kLineH * static_cast<float>(shown.visible.size()) + 8;

    // 自動スクロール: 新しい行が来た (または見せる行が変わった) とき、前のフレームの終わりに一番下に
    // いたら一番下へ。このフレームで上へホイールしたときは追わない (しないと、毎フレーム出力が
    // 来ている間は上へ戻れない)。新しいウィンドウでは最後から始める。
    Follow& follow = follow_.try_emplace(&c).first->second;
    const bool first = follow.seenSeq == ~0ull;
    const bool changed = last != follow.seenSeq || shown.visible.size() != follow.shown;
    const bool wheelUp = c.IsHovered(view) && c.In().wheel > 0.f;
    if (first || (settings_.autoScroll && follow.atEnd && changed && !wheelUp)) w::ScrollToEnd(c, kScrollId);
    follow.seenSeq = last;
    follow.shown = shown.visible.size();

    const float off = w::BeginScroll(c, kScrollId, view, content);
    if (shown.visible.empty()) {
        ui::TextStyle s = LineStyle(theme::TextFaint);
        s.h = ui::HAlign::Center;
        p.Text({view.x, view.y + 8, view.w, kLineH}, Tr(L"表示するログはありません"), s);
    }
    const size_t firstLine = static_cast<size_t>(std::max(0.f, (off - 4) / kLineH));
    for (size_t i = firstLine; i < shown.visible.size(); ++i) {
        const float y = view.y + 4 + kLineH * static_cast<float>(i) - off;
        if (y > view.Bottom()) break;
        const log::Entry& e = entries[shown.visible[i]];
        p.Text({view.x + 12, y, 70, kLineH}, std::format(L"{:8.2f}", e.time), LineStyle(theme::TextFaint));
        if (e.level != log::Level::Info) {
            w::DrawIcon(c, {view.x + 86, y, 14, kLineH}, e.level == log::Level::Error ? w::Icon::Error : w::Icon::Warning, theme::Text, 13);
        }
        p.Text({view.x + 106, y, view.w - 120, kLineH}, Utf8ToWide(e.text), LineStyle(LevelColor(e.level)));
    }
    w::EndScroll(c, kScrollId, view, content);
    follow.atEnd = w::ScrollAtEnd(c, kScrollId, view, content);
}

}  // namespace rm::editor
