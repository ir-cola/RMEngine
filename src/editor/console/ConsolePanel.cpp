#include "editor/console/ConsolePanel.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/console/ConsoleModel.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Theme.h"
#include "editor/widgets/Widgets.h"

#include <algorithm>
#include <format>
#include <optional>
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
constexpr float kScrollBarW = 12.f;  // 行の右の、スクロールバーのための空き
constexpr float kMarkW = 14.f;       // 行の頭の印 (警告 / エラー) の列

// レベルの色: 行の文字と、ツールバーの数の 1 か所。
ui::Color LevelColor(log::Level level) {
    switch (level) {
        case log::Level::Error: return theme::Danger;
        case log::Level::Warn: return theme::Warn;
        case log::Level::Info: break;
    }
    return theme::Text;
}

// レベルの印: 行の頭と、ツールバーの数の前の 1 か所。情報は印なし。
std::optional<w::Icon> LevelMark(log::Level level) {
    switch (level) {
        case log::Level::Error: return w::Icon::Error;
        case log::Level::Warn: return w::Icon::Warning;
        case log::Level::Info: break;
    }
    return std::nullopt;
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
        full += w::CheckboxCountWidth(c, levels[i].label, counts[i], LevelMark(levels[i].level)) + (i < 2 ? kLevelGap : kGroupGap);
        compact += w::CheckboxCountWidth(c, {}, counts[i], LevelMark(levels[i].level)) + (i < 2 ? kLevelGap : kGroupGap);
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
        console::ClearSelection(selection_);
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
        const Rect rect{x, y, w::CheckboxCountWidth(c, label, counts[i], LevelMark(lv.level)), kItemH};
        const ui::Color countColor = lv.count == 0 ? theme::TextFaint : LevelColor(lv.level);
        if (w::CheckboxCount(c, lv.id, rect, *lv.shown, label, counts[i], countColor, LevelMark(lv.level), lv.count == 0 ? 0.4f : 1.f)) {
            c.RequestRedraw();
        }
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

void ConsolePanel::CopySelected(ui::Context& c, bool messagesOnly) const {
    const std::vector<log::Entry> entries = log::Snapshot();
    const console::Result shown = console::Apply(entries, MakeFilter(settings_, search_, clearedThrough_));
    const std::string text = console::CopyText(entries, shown.visible, selection_, messagesOnly);
    if (!text.empty() && c.setClipboardText) c.setClipboardText(Utf8ToWide(text));
}

void ConsolePanel::SelectAllShown() {
    const std::vector<log::Entry> entries = log::Snapshot();
    const console::Result shown = console::Apply(entries, MakeFilter(settings_, search_, clearedThrough_));
    console::SelectAll(selection_, console::SeqsOf(entries, shown.visible));
}

std::vector<w::Action> ConsolePanel::MenuActions(ui::Context& c, bool anyShown) {
    // (メニューが開いている間に行が増えても、選んだ行は seq で覚えているので run の時に調べ直す)
    ui::Context* ctx = &c;
    ConsolePanel* self = this;
    const bool any = !selection_.Empty();
    std::vector<w::Action> list;
    w::Action copy;
    copy.label = Tr(L"コピー");
    copy.key = ui::Key::C;
    copy.ctrl = true;
    copy.enabled = any;
    copy.run = [self, ctx] { self->CopySelected(*ctx, false); };
    list.push_back(std::move(copy));
    w::Action message;
    message.label = Tr(L"メッセージだけをコピー");
    message.enabled = any;
    message.run = [self, ctx] { self->CopySelected(*ctx, true); };
    list.push_back(std::move(message));
    w::Action sep;
    sep.separator = true;
    list.push_back(sep);
    w::Action all;
    all.label = Tr(L"すべて選択");
    all.key = ui::Key::A;
    all.ctrl = true;
    all.enabled = anyShown;
    all.run = [self, ctx] {
        self->SelectAllShown();
        ctx->RequestRedraw();
    };
    list.push_back(std::move(all));
    return list;
}

void ConsolePanel::Draw(ui::Context& c, const Rect& r) {
    auto& p = c.P();
    const auto& in = c.In();
    const std::vector<log::Entry> entries = log::Snapshot();
    const uint64_t last = entries.empty() ? 0 : entries.back().seq;
    Follow& follow = follow_.try_emplace(&c).first->second;

    // 右クリックのメニューが開いている間、その下 (このパネル) は表示だけ。
    const bool inputWas = c.InputEnabled();
    if (follow.menu.open) c.SetInputEnabled(false);
    if (c.MousePressed() || c.MousePressed(ui::Mouse::Right)) follow.hasKeys = c.IsHovered(r);

    const bool fieldFree = c.Focused() == 0;  // (検索の欄で Esc を押したフレームは、選択を外さない)

    Rect area = r;
    const Rect bar = ui::TakeTop(area, kBarH);
    {
        // 数はツールバーを描く前のもの。ツールバーで変わったら (クリア・表示・検索)、下で数え直す。
        const console::Result before = console::Apply(entries, MakeFilter(settings_, search_, clearedThrough_));
        DrawToolbar(c, bar, before.counts.info, before.counts.warn, before.counts.error, last);
    }
    const console::Result shown = console::Apply(entries, MakeFilter(settings_, search_, clearedThrough_));
    console::Prune(selection_, entries, clearedThrough_);
    const std::vector<uint64_t> order = console::SeqsOf(entries, shown.visible);

    // --- キー (最後のクリックがこのパネルで、どの入力欄もキーを持っていないとき) ---
    if (follow.hasKeys && fieldFree && c.Focused() == 0 && c.InputEnabled() && !in.keys.empty()) {
        if (c.KeyPressed(ui::Key::A, true)) {
            console::SelectAll(selection_, order);
            c.RequestRedraw();
        }
        if (c.KeyPressed(ui::Key::C, true)) CopySelected(c, false);
        if (c.KeyPressed(ui::Key::Escape) && !selection_.Empty()) {
            console::ClearSelection(selection_);
            c.RequestRedraw();
        }
    }

    const Rect view = area.Inset(0, 2, 0, 2);
    const float content = kLineH * static_cast<float>(shown.visible.size()) + 8;

    // 自動スクロール: 新しい行が来た (または見せる行が変わった) とき、前のフレームの終わりに一番下に
    // いたら一番下へ。このフレームで上へホイールしたときは追わない (しないと、毎フレーム出力が
    // 来ている間は上へ戻れない)。新しいウィンドウでは最後から始める。
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
    // 行の並びの上端 (スクロールした分を含む) と、行の横幅 (右のスクロールバーにはかからない)。
    const float top = view.y + 4 - off;
    const float rowX = view.x + 2, rowW = std::max(0.f, view.w - 2 - kScrollBarW);
    const Rect rowsArea{rowX, view.y, rowW, view.h};
    // 列: 時刻 | 印 (情報は空けたまま: 本文の頭がそろう) | 本文
    const float timeX = rowX + 8;
    const float timeW = p.TextWidth(L"00:00:00.000", 12) + 6;
    const float markX = timeX + timeW;
    const float textX = markX + kMarkW + 6;

    // --- マウス: 押した行を選ぶ / 何もない所で外す / 右クリックでメニュー ---
    bool menuNow = false;
    if (c.IsHovered(rowsArea) && (c.MousePressed() || c.MousePressed(ui::Mouse::Right))) {
        const bool right = c.MousePressed(ui::Mouse::Right);
        const bool onRow = !order.empty() && in.mouseY >= top && in.mouseY < top + kLineH * static_cast<float>(order.size());
        if (onRow) {
            const uint64_t seq = order[console::RowAt(in.mouseY, top, kLineH, order.size())];
            if (right) {
                if (!selection_.Contains(seq)) console::Click(selection_, order, seq, false, false);
            } else {
                follow.dragBase = selection_;
                console::Click(selection_, order, seq, in.ctrl, in.shift);
                follow.dragging = true;
                follow.dragMoved = false;
                follow.dragAdd = in.ctrl;
                follow.dragFrom = in.shift && selection_.anchor != 0 ? selection_.anchor : seq;
                if (!in.ctrl) follow.dragBase = {};
            }
        } else if (!right && !in.ctrl && !in.shift) {
            console::ClearSelection(selection_);
        }
        menuNow = right;
        c.RequestRedraw();
    }
    // ドラッグ: 押した行から、マウスのある行までを選ぶ (上や下へはみ出したら、見えている端の行まで)。
    if (follow.dragging) {
        if (!in.down[0] || !c.InputEnabled()) {
            follow.dragging = false;
        } else if (!order.empty()) {
            const float my = std::clamp(in.mouseY, view.y, view.Bottom() - 1);
            const uint64_t to = order[console::RowAt(my, top, kLineH, order.size())];
            if (to != follow.dragFrom || follow.dragMoved) {
                follow.dragMoved = true;
                console::Selection next = follow.dragAdd ? follow.dragBase : console::Selection{};
                console::SelectRange(next, order, follow.dragFrom, to, true);
                if (next != selection_) {
                    selection_ = std::move(next);
                    c.RequestRedraw();
                }
            }
        }
    }

    const size_t firstLine = static_cast<size_t>(std::max(0.f, (off - 4) / kLineH));
    for (size_t i = firstLine; i < shown.visible.size(); ++i) {
        const float y = top + kLineH * static_cast<float>(i);
        if (y > view.Bottom()) break;
        const log::Entry& e = entries[shown.visible[i]];
        const Rect row{rowX, y, rowW, kLineH};
        const bool selected = selection_.Contains(e.seq);
        const bool hovered = c.IsHovered(row) && !follow.dragging;
        if (selected || hovered) w::DrawRow(c, row, {selected, hovered, follow.hasKeys});
        p.Text({timeX, y, timeW, kLineH}, Utf8ToWide(console::ClockText(e.clockMs)), LineStyle(theme::TextFaint));
        if (const auto mark = LevelMark(e.level)) w::DrawIcon(c, {markX, y, kMarkW, kLineH}, *mark, theme::Text, 13);
        p.Text({textX, y, std::max(0.f, row.Right() - 6 - textX), kLineH}, Utf8ToWide(e.text), LineStyle(LevelColor(e.level)));
    }
    w::EndScroll(c, kScrollId, view, content);
    follow.atEnd = w::ScrollAtEnd(c, kScrollId, view, content);

    c.SetInputEnabled(inputWas);
    if (menuNow) {
        follow.actions = MenuActions(c, !order.empty());
        follow.menu.OpenAt(in.mouseX, in.mouseY);
    }
    // 右クリックのメニュー (フレームの終わりに: パネルで切られないように)。
    w::ActionMenu(c, follow.menu, follow.actions);
}

}  // namespace rm::editor
