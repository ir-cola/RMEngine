// The editor's named panel layouts: "save the layout as ...", switching, and the list to rename /
// delete them (Window menu). The list itself is dock::NamedLayouts (pure, tested); it is kept per
// software in UserSettings/Layout.json and written as soon as it changes (it is not project data:
// no undo, never "unsaved").

#include "editor/lang/Lang.h"
#include "editor/shell/EditorScreen.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "editor/widgets/Theme.h"

#include <algorithm>

namespace rm::editor {

using ui::Rect;

namespace {

ui::TextStyle Style(float size, ui::Color color, bool bold = false) {
    ui::TextStyle s;
    s.size = size;
    s.color = color;
    s.bold = bold;
    return s;
}

constexpr float kRowH = 40.f;

}  // namespace

void EditorScreen::BeginSaveLayoutAs() {
    const Workspace& ws = Active();
    // The one switched to last is offered (saving the changes made to it); otherwise a free name.
    layoutName_ = !ws.namedShown.empty() && ws.named.Find(ws.namedShown) ? ws.namedShown : ws.named.FreeName(WideToUtf8(Tr(L"配置")));
    focusLayoutName_ = true;
    modal_ = Modal::SaveLayoutAs;
    Invalidate();
}

bool EditorScreen::SaveNamedLayout(const std::string& name) {
    Workspace& ws = Active();
    CaptureFloatingRects();
    const bool replaced = ws.named.Find(name) != nullptr;
    if (!ws.named.Put(name, ws.dock.GetLayout())) return false;
    ws.namedShown = dock::NamedLayouts::CleanName(name);
    if (replaced) {
        log::Info("配置「{}」を今の配置で上書きしました ({})", ws.namedShown, WideToUtf8(ws.Title()));
    } else {
        log::Info("今の配置を「{}」として保存しました ({})", ws.namedShown, WideToUtf8(ws.Title()));
    }
    if (layoutReadOnly_) log::Warn("パネルの配置は新しい版の RM Engine で保存されているので、この版ではファイルに書きません");
    SaveLayout();
    return true;
}

void EditorScreen::ApplyNamedLayout(const std::string& name) {
    Workspace& ws = Active();
    std::optional<dock::Layout> layout = ws.named.Get(name, ws.dock.PanelIds());
    if (!layout) {
        log::Warn("配置「{}」を読めませんでした", name);
        return;
    }
    ws.dock.SetLayout(std::move(*layout));  // the panel windows follow (RequestSyncFloating)
    ws.namedShown = name;
    log::Info("配置「{}」に切り替えました ({})", name, WideToUtf8(ws.Title()));
}

void EditorScreen::DrawSaveLayoutAs(ui::Context& c) {
    auto& p = c.P();
    const Workspace& ws = Active();
    const Rect cr = w::BeginModal(c, 560, 240, Tr(L"今の配置に名前を付けて保存"));
    float y = cr.y;
    p.Text({cr.x, y, cr.w, 20}, Tr(L"名前"), Style(12, theme::TextDim, true));
    y += 24;
    if (focusLayoutName_) {
        c.FocusText(c.MakeId("ls.name"), Utf8ToWide(layoutName_), true);
        focusLayoutName_ = false;
    }
    w::TextField(c, "ls.name", {cr.x, y, cr.w, 34}, layoutName_, Tr(L"配置の名前"));
    y += 44;

    const auto check = ws.named.Check(layoutName_);
    ui::TextStyle note = Style(12, theme::TextFaint);
    note.wrap = true;
    note.v = ui::VAlign::Top;
    std::wstring text = TrF(L"「{}」の配置として、このプロジェクトに覚えます。「ウィンドウ」メニューから切り替えられます。", ws.Title());
    if (check == dock::NamedLayouts::NameCheck::Empty) {
        text = Tr(L"名前を入れてください。");
    } else if (check == dock::NamedLayouts::NameCheck::Taken) {
        note.color = theme::Warn;
        text = Tr(L"同じ名前の配置があります。今の配置で上書きします。");
    }
    p.Text({cr.x, y, cr.w, cr.Bottom() - 44 - y}, text, note);

    const bool ok = check != dock::NamedLayouts::NameCheck::Empty;
    const Rect save{cr.Right() - 120, cr.Bottom() - 36, 120, 36};
    const Rect cancel{save.x - 12 - 110, save.y, 110, 36};
    if (w::Button(c, "ls.cancel", cancel, Tr(L"キャンセル")) || c.KeyPressed(ui::Key::Escape)) {
        modal_ = Modal::None;
        c.ClearFocus();
        return;
    }
    const bool overwrite = check == dock::NamedLayouts::NameCheck::Taken;
    if ((w::Button(c, "ls.save", save, overwrite ? Tr(L"上書き") : Tr(L"保存"), w::ButtonKind::Primary, ok) ||
         (ok && c.KeyPressed(ui::Key::Enter))) &&
        SaveNamedLayout(layoutName_)) {
        modal_ = Modal::None;
        c.ClearFocus();
    }
}

void EditorScreen::DrawLayouts(ui::Context& c) {
    auto& p = c.P();
    Workspace& ws = Active();
    const auto& entries = ws.named.Entries();
    const float listH = std::clamp(static_cast<float>(std::max<size_t>(entries.size(), 1)) * kRowH, kRowH * 2, kRowH * 9);
    const Rect cr = w::BeginModal(c, 700, 150 + listH, TrF(L"パネルの配置 — {}", ws.Title()));
    p.Text({cr.x, cr.y, cr.w, 20}, Tr(L"名前を付けて保存した配置です。このソフトのものだけが並びます。"), Style(12, theme::TextFaint));
    const Rect view{cr.x, cr.y + 30, cr.w, cr.Bottom() - 52 - (cr.y + 30)};

    // What a button asked for, done after the list is drawn (it changes the list).
    enum class Do { None, Apply, Rename, Delete } todo = Do::None;
    std::string target;
    const bool renaming = !renamingLayout_.empty();

    if (entries.empty()) {
        ui::TextStyle hint = Style(13, theme::TextFaint);
        hint.wrap = true;
        hint.v = ui::VAlign::Top;
        p.Text(view.Inset(4, 8, 4, 0), Tr(L"まだありません。「今の配置を保存…」で今の配置に名前を付けると、ここに並びます。"), hint);
    } else {
        const float content = static_cast<float>(entries.size()) * kRowH;
        const float off = w::BeginScroll(c, "lay.rows", view, content);
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string& name = entries[i].info.name;
            const Rect row{view.x, view.y - off + static_cast<float>(i) * kRowH, view.w - (content > view.h ? 12 : 0), kRowH - 4};
            if (row.Bottom() < view.y || row.y > view.Bottom()) continue;
            c.PushId(static_cast<uint64_t>(i));
            w::DrawRow(c, row, {name == ws.namedShown, c.IsHovered(row), false});
            constexpr float bw = 84, step = bw + 8;
            Rect b{row.Right() - 6 - bw, row.y + 4, bw, row.h - 8};  // right to left
            if (renamingLayout_ == name) {
                const Rect field{row.x + 6, row.y + 3, b.x - step - 8 - (row.x + 6), row.h - 6};  // before the 2 buttons
                if (focusRename_) {
                    c.FocusText(c.MakeId("lay.rename"), Utf8ToWide(renameText_), true);
                    focusRename_ = false;
                }
                w::TextField(c, "lay.rename", field, renameText_);
                const auto check = ws.named.Check(renameText_, name);
                if (w::Button(c, "lay.cancel", b, Tr(L"やめる")) || c.KeyPressed(ui::Key::Escape)) {
                    renamingLayout_.clear();
                    c.ClearFocus();
                }
                b.x -= step;
                if (w::Button(c, "lay.commit", b, Tr(L"決定"), w::ButtonKind::Primary, check == dock::NamedLayouts::NameCheck::Ok) ||
                    (check == dock::NamedLayouts::NameCheck::Ok && c.KeyPressed(ui::Key::Enter))) {
                    todo = Do::Rename;
                    target = name;
                }
                if (check == dock::NamedLayouts::NameCheck::Taken) w::Tooltip(c, Tr(L"同じ名前の配置があります"));
            } else {
                p.Text({row.x + 14, row.y, b.x - 2 * step - 8 - (row.x + 14), row.h}, Utf8ToWide(name),  // before the 3 buttons
                       Style(13, theme::Text, name == ws.namedShown));
                const bool confirming = confirmDelete_ == name;
                if (w::Button(c, "lay.delete", b, confirming ? Tr(L"本当に削除") : Tr(L"削除"),
                              confirming ? w::ButtonKind::Primary : w::ButtonKind::Secondary, !renaming)) {
                    if (confirming) {
                        todo = Do::Delete;
                        target = name;
                    } else {
                        confirmDelete_ = name;
                    }
                }
                b.x -= step;
                if (w::Button(c, "lay.rename.start", b, Tr(L"名前の変更"), w::ButtonKind::Secondary, !renaming)) {
                    renamingLayout_ = name;
                    renameText_ = name;
                    focusRename_ = true;
                    confirmDelete_.clear();
                }
                b.x -= step;
                if (w::Button(c, "lay.apply", b, Tr(L"切り替え"), w::ButtonKind::Secondary, !renaming)) {
                    todo = Do::Apply;
                    target = name;
                }
            }
            c.PopId();
        }
        w::EndScroll(c, "lay.rows", view, content);
    }

    switch (todo) {
        case Do::Apply:
            confirmDelete_.clear();
            ApplyNamedLayout(target);
            break;
        case Do::Rename: {
            const std::string to = dock::NamedLayouts::CleanName(renameText_);
            if (ws.named.Rename(target, to)) {
                if (ws.namedShown == target) ws.namedShown = to;
                log::Info("配置の名前を変えました: 「{}」→「{}」", target, to);
                SaveLayout();
            }
            renamingLayout_.clear();
            c.ClearFocus();
            break;
        }
        case Do::Delete:
            if (ws.named.Remove(target)) {
                if (ws.namedShown == target) ws.namedShown.clear();
                log::Info("配置「{}」を削除しました", target);
                SaveLayout();
            }
            confirmDelete_.clear();
            break;
        case Do::None:
            break;
    }

    // Bottom row: save / back to the first layout on the left, close on the right.
    const Rect saveAs{cr.x, cr.Bottom() - 36, 170, 36};
    const Rect reset{saveAs.Right() + 12, saveAs.y, 170, 36};
    const Rect close{cr.Right() - 110, saveAs.y, 110, 36};
    if (w::Button(c, "lay.saveas", saveAs, Tr(L"今の配置を保存…"), w::ButtonKind::Secondary, !renaming)) {
        BeginSaveLayoutAs();
        return;
    }
    if (w::Button(c, "lay.reset", reset, Tr(L"最初の配置に戻す"), w::ButtonKind::Secondary, !renaming)) {
        confirmDelete_.clear();
        ResetLayout();
    }
    if (w::Button(c, "lay.close", close, Tr(L"閉じる"), w::ButtonKind::Primary, !renaming) ||
        (!renaming && c.KeyPressed(ui::Key::Escape))) {
        modal_ = Modal::None;
        renamingLayout_.clear();
        confirmDelete_.clear();
        c.ClearFocus();
    }
}

}  // namespace rm::editor
