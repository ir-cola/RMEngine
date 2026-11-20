// エディターの名前を付けたパネルのレイアウト: 「レイアウトを名前を付けて保存」、切り替え、
// そして名前の変更 / 削除 / 移動の一覧 (ウィンドウメニュー)。ソフトごとに 2 つの一覧があり、
// どちらも dock::NamedLayouts (純粋。テスト済み):
//   このプロジェクトの  <project>/UserSettings/Layout.json  "named"
//   共有               %APPDATA%\RM_Engine\Layouts.json     (この PC の全プロジェクトで使える)
// 一覧は、変わったらすぐに書く。レイアウトはプロジェクトのデータではない: Undo は無く、「未保存」にもならない。

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

// 一覧のどの行か: 一覧と名前 (名前は両方の一覧にあることがある)。
std::string RowKey(bool shared, const std::string& name) { return (shared ? "s:" : "p:") + name; }

std::wstring ListName(bool shared) { return shared ? Tr(L"共通の配置") : Tr(L"このプロジェクトの配置"); }

}  // namespace

void EditorScreen::BeginSaveLayoutAs() {
    Workspace& ws = Active();
    // 最後に切り替えたものを出す (それに加えた変更を保存する)。無ければ空いている名前。
    const bool offerShown = !ws.namedShown.empty() && ws.Named(ws.namedShownShared).Find(ws.namedShown);
    layoutShared_ = offerShown && ws.namedShownShared;
    layoutName_ = offerShown ? ws.namedShown : ws.named.FreeName(WideToUtf8(Tr(L"配置")));
    focusLayoutName_ = true;
    modal_ = Modal::SaveLayoutAs;
    Invalidate();
}

void EditorScreen::SaveNamedList(bool shared) {
    if (shared) {
        SaveSharedLayouts();
    } else {
        if (layoutReadOnly_) log::Warn("パネルの配置は新しい版の RM Engine で保存されているので、この版ではファイルに書きません");
        SaveLayout();
    }
}

bool EditorScreen::BeginNamedEdit(bool shared) {
    if (shared) LoadSharedLayouts();
    if (shared ? sharedReadOnly_ : layoutReadOnly_) {
        log::Warn("{}の配置は変えません (新しい版の RM Engine のファイルか、読めなかったファイルのため)", WideToUtf8(ListName(shared)));
        return false;
    }
    return true;
}

bool EditorScreen::SaveNamedLayout(const std::string& name, bool shared) {
    if (!BeginNamedEdit(shared)) return false;
    Workspace& ws = Active();
    CaptureFloatingRects();
    dock::NamedLayouts& list = ws.Named(shared);
    const bool replaced = list.Find(name) != nullptr;
    if (!list.Put(name, ws.dock.GetLayout())) return false;
    ws.namedShown = dock::NamedLayouts::CleanName(name);
    ws.namedShownShared = shared;
    const std::string where = WideToUtf8(ListName(shared));
    if (replaced) {
        log::Info("配置「{}」を今の配置で上書きしました ({}・{})", ws.namedShown, where, WideToUtf8(ws.Title()));
    } else {
        log::Info("今の配置を「{}」として保存しました ({}・{})", ws.namedShown, where, WideToUtf8(ws.Title()));
    }
    SaveNamedList(shared);
    return true;
}

void EditorScreen::ApplyNamedLayout(const std::string& name, bool shared) {
    Workspace& ws = Active();
    std::optional<dock::Layout> layout = ws.Named(shared).Get(name, ws.dock.PanelIds());
    if (!layout) {
        log::Warn("配置「{}」を読めませんでした", name);
        return;
    }
    ws.dock.SetLayout(std::move(*layout));  // パネルのウィンドウが追従する (RequestSyncFloating)
    ws.namedShown = name;
    ws.namedShownShared = shared;
    log::Info("配置「{}」に切り替えました ({})", name, WideToUtf8(ws.Title()));
}

void EditorScreen::DrawSaveLayoutAs(ui::Context& c) {
    auto& p = c.P();
    Workspace& ws = Active();
    const Rect cr = w::BeginModal(c, 580, 280, Tr(L"今の配置に名前を付けて保存"));
    float y = cr.y;
    p.Text({cr.x, y, cr.w, 20}, Tr(L"名前"), Style(12, theme::TextDim, true));
    y += 24;
    if (focusLayoutName_) {
        c.FocusText(c.MakeId("ls.name"), Utf8ToWide(layoutName_), true);
        focusLayoutName_ = false;
    }
    w::TextField(c, "ls.name", {cr.x, y, cr.w, 34}, layoutName_, Tr(L"配置の名前"));
    y += 44;
    w::Checkbox(c, "ls.shared", {cr.x, y, cr.w, 26}, layoutShared_, Tr(L"どのプロジェクトでも使う (共通の配置)"));
    y += 34;

    const auto check = ws.Named(layoutShared_).Check(layoutName_);
    ui::TextStyle note = Style(12, theme::TextFaint);
    note.wrap = true;
    note.v = ui::VAlign::Top;
    std::wstring text = layoutShared_
                            ? TrF(L"「{}」の配置として、この PC のどのプロジェクトからも使えるように覚えます。", ws.Title())
                            : TrF(L"「{}」の配置として、このプロジェクトに覚えます。「ウィンドウ」メニューから切り替えられます。", ws.Title());
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
        SaveNamedLayout(layoutName_, layoutShared_)) {
        modal_ = Modal::None;
        c.ClearFocus();
    }
}

void EditorScreen::DrawLayouts(ui::Context& c) {
    auto& p = c.P();
    Workspace& ws = Active();
    struct Row {
        bool shared;
        std::string name;
    };
    std::vector<Row> rows;
    for (const bool sharedList : {false, true}) {
        for (const auto& e : ws.Named(sharedList).Entries()) rows.push_back({sharedList, e.info.name});
    }
    const float listH = std::clamp(static_cast<float>(std::max<size_t>(rows.size(), 1)) * kRowH, kRowH * 2, kRowH * 9);
    const Rect cr = w::BeginModal(c, 900, 150 + listH, TrF(L"パネルの配置 — {}", ws.Title()));
    p.Text({cr.x, cr.y, cr.w, 20},
           Tr(L"名前を付けて保存した配置です (このソフトのもの)。「共通」の配置は、この PC のどのプロジェクトでも使えます。"),
           Style(12, theme::TextFaint));
    const Rect view{cr.x, cr.y + 30, cr.w, cr.Bottom() - 52 - (cr.y + 30)};

    // ボタンが求めたことを、一覧を描いた後で行う (一覧が変わるため)。
    enum class Do { None, Apply, Rename, Delete, Move } todo = Do::None;
    Row target{};
    const bool renaming = !renamingLayout_.empty();

    if (rows.empty()) {
        ui::TextStyle hint = Style(13, theme::TextFaint);
        hint.wrap = true;
        hint.v = ui::VAlign::Top;
        p.Text(view.Inset(4, 8, 4, 0), Tr(L"まだありません。「今の配置を保存…」で今の配置に名前を付けると、ここに並びます。"), hint);
    } else {
        const float content = static_cast<float>(rows.size()) * kRowH;
        const float off = w::BeginScroll(c, "lay.rows", view, content);
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            const std::string key = RowKey(r.shared, r.name);
            const bool shown = r.name == ws.namedShown && r.shared == ws.namedShownShared;
            const Rect row{view.x, view.y - off + static_cast<float>(i) * kRowH, view.w - (content > view.h ? 12 : 0), kRowH - 4};
            if (row.Bottom() < view.y || row.y > view.Bottom()) continue;
            c.PushId(static_cast<uint64_t>(i));
            w::DrawRow(c, row, {shown, c.IsHovered(row), false});
            // ボタンは右から。`next` が、その左の次のボタンの四角を返す。
            float right = row.Right() - 6;
            auto next = [&](float width) {
                right -= width;
                const Rect b{right, row.y + 4, width, row.h - 8};
                right -= 8;
                return b;
            };
            if (renamingLayout_ == key) {
                const auto check = ws.Named(r.shared).Check(renameText_, r.name);
                if (w::Button(c, "lay.cancel", next(84), Tr(L"やめる")) || c.KeyPressed(ui::Key::Escape)) {
                    renamingLayout_.clear();
                    c.ClearFocus();
                }
                if (w::Button(c, "lay.commit", next(84), Tr(L"決定"), w::ButtonKind::Primary, check == dock::NamedLayouts::NameCheck::Ok) ||
                    (check == dock::NamedLayouts::NameCheck::Ok && c.KeyPressed(ui::Key::Enter))) {
                    todo = Do::Rename;
                    target = r;
                }
                if (focusRename_) {
                    c.FocusText(c.MakeId("lay.rename"), Utf8ToWide(renameText_), true);
                    focusRename_ = false;
                }
                w::TextField(c, "lay.rename", {row.x + 6, row.y + 3, right - (row.x + 6), row.h - 6}, renameText_);
                if (check == dock::NamedLayouts::NameCheck::Taken) w::Tooltip(c, Tr(L"同じ名前の配置があります"));
            } else {
                const bool confirming = confirmDelete_ == key;
                if (w::Button(c, "lay.delete", next(84), confirming ? Tr(L"本当に削除") : Tr(L"削除"),
                              confirming ? w::ButtonKind::Primary : w::ButtonKind::Secondary, !renaming)) {
                    if (confirming) {
                        todo = Do::Delete;
                        target = r;
                    } else {
                        confirmDelete_ = key;
                    }
                }
                // もう片方の一覧へ - その一覧に同じ名前が既にあるときはしない。
                const bool canMove = !ws.Named(!r.shared).Find(r.name);
                if (w::Button(c, "lay.move", next(150), r.shared ? Tr(L"このプロジェクトへ") : Tr(L"共通にする"),
                              w::ButtonKind::Secondary, !renaming && canMove)) {
                    todo = Do::Move;
                    target = r;
                }
                if (!canMove && c.IsHovered(row) && !renaming) {
                    w::Tooltip(c, r.shared ? Tr(L"このプロジェクトに同じ名前の配置があります") : Tr(L"共通の配置に同じ名前の配置があります"));
                }
                if (w::Button(c, "lay.rename.start", next(100), Tr(L"名前の変更"), w::ButtonKind::Secondary, !renaming)) {
                    renamingLayout_ = key;
                    renameText_ = r.name;
                    focusRename_ = true;
                    confirmDelete_.clear();
                }
                if (w::Button(c, "lay.apply", next(84), Tr(L"切り替え"), w::ButtonKind::Secondary, !renaming)) {
                    todo = Do::Apply;
                    target = r;
                }
                // それがある一覧、次に名前。
                const std::wstring where = r.shared ? Tr(L"共通") : Tr(L"プロジェクト");
                const float whereW = 90;
                p.Text({right - whereW, row.y, whereW, row.h}, where, Style(11, theme::TextFaint));
                p.Text({row.x + 14, row.y, right - whereW - 8 - (row.x + 14), row.h}, Utf8ToWide(r.name), Style(13, theme::Text, shown));
            }
            c.PopId();
        }
        w::EndScroll(c, "lay.rows", view, content);
    }

    dock::NamedLayouts& list = ws.Named(target.shared);
    const bool targetShown = target.name == ws.namedShown && target.shared == ws.namedShownShared;
    switch (todo) {
        case Do::Apply:
            confirmDelete_.clear();
            ApplyNamedLayout(target.name, target.shared);
            break;
        case Do::Rename: {
            const std::string to = dock::NamedLayouts::CleanName(renameText_);
            if (BeginNamedEdit(target.shared) && list.Rename(target.name, to)) {
                if (targetShown) ws.namedShown = to;
                log::Info("配置の名前を変えました: 「{}」→「{}」", target.name, to);
                SaveNamedList(target.shared);
            }
            renamingLayout_.clear();
            c.ClearFocus();
            break;
        }
        case Do::Delete:
            if (BeginNamedEdit(target.shared) && list.Remove(target.name)) {
                if (targetShown) ws.namedShown.clear();
                log::Info("配置「{}」を削除しました", target.name);
                SaveNamedList(target.shared);
            }
            confirmDelete_.clear();
            break;
        case Do::Move:
            confirmDelete_.clear();
            if (BeginNamedEdit(false) && BeginNamedEdit(true) && dock::NamedLayouts::Move(list, target.name, ws.Named(!target.shared))) {
                if (targetShown) ws.namedShownShared = !target.shared;
                log::Info("配置「{}」を{}へ移しました", target.name, WideToUtf8(ListName(!target.shared)));
                SaveNamedList(false);
                SaveNamedList(true);
            }
            break;
        case Do::None:
            break;
    }

    // 一番下の行: 左に 保存 / 最初のレイアウトに戻す、右に閉じる。
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
