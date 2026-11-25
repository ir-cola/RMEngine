#include "editor/lang/Lang.h"
#include "editor/tree/TreeView.h"

#include "base/Utf.h"
#include "editor/widgets/Theme.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace rm::editor::tree {

using ui::Rect;

namespace {

constexpr size_t npos = static_cast<size_t>(-1);
constexpr float kIconW = 18.f;  // 行の絵 (Icon)。矢印と名前の間
constexpr float kLineH = 24.f;  // どんな高さの行でも、名前 (と名前変更のフィールド) が入る行
constexpr float kIndent = 16.f;
constexpr float kArrow = 16.f;
constexpr float kDragStart = 6.f;  // 押したのがドラッグになるまでにマウスが動く DIP
constexpr float kTop = 4.f;        // 最初の行の上の空き
constexpr char kScrollId[] = "tree.scroll";

ui::TextStyle Style(float size, ui::Color color, ui::HAlign h = ui::HAlign::Left) {
    ui::TextStyle s;
    s.size = size;
    s.color = color;
    s.h = h;
    return s;
}

size_t RowOf(std::span<const Item> items, std::span<const Row> rows, ItemId id) {
    if (id == 0) return npos;
    for (size_t r = 0; r < rows.size(); ++r) {
        if (items[rows[r].item].id == id) return r;
    }
    return npos;
}

// `id` の祖先をすべて開いて、その行が見えるようにする。
void Reveal(State& st, std::span<const Item> items, const ItemIndex& index, ItemId id) {
    size_t i = index.Find(id);
    for (size_t steps = 0; i != npos && steps <= items.size(); ++steps) {
        const ItemId parent = items[i].parent;
        if (parent == 0) break;
        st.collapsed.erase(parent);
        i = index.Find(parent);
    }
}

void StartRename(State& st, const Item& it) {
    st.renaming = it.id;
    st.renameStart = true;
    st.renameText = it.name;
    st.scrollTo = it.id;
}

std::vector<Action> MenuFor(Source& src, State& st, std::span<const Item> items) {
    std::vector<Action> list;
    if (st.sel.ids.size() == 1 && src.CanRename(st.sel.ids[0])) {
        const ItemId id = st.sel.ids[0];
        State* sp = &st;
        const size_t i = Find(items, id);
        const std::string name = i != npos ? items[i].name : std::string();
        Action rename;
        rename.label = Tr(L"名前を変更");
        rename.key = ui::Key::F2;
        rename.run = [sp, id, name] {
            sp->renaming = id;
            sp->renameStart = true;
            sp->renameText = name;
            sp->scrollTo = id;
        };
        list.push_back(std::move(rename));
    }
    std::vector<Action> more = src.Actions(st.sel.ids);
    if (!list.empty() && !more.empty()) {
        Action sep;
        sep.separator = true;
        list.push_back(std::move(sep));
    }
    for (auto& a : more) list.push_back(std::move(a));
    return list;
}

}  // namespace

w::Icon PictureOf(Icon icon) {
    switch (icon) {
        case Icon::Cube:
        case Icon::Model: return w::Icon::Cube;
        case Icon::Camera: return w::Icon::Camera;
        case Icon::Light: return w::Icon::Light;
        case Icon::Square: return w::Icon::Square;
        case Icon::Effect: return w::Icon::Effect;
        case Icon::Scene: return w::Icon::Scene;
        case Icon::Folder: return w::Icon::Folder;
        case Icon::File: return w::Icon::File;
        case Icon::Image: return w::Icon::Image;
        case Icon::Audio: return w::Icon::Audio;
        case Icon::Font: return w::Icon::Font;
        case Icon::Text: return w::Icon::Text;
        default: return w::Icon::Object;
    }
}

w::DragThing Source::DragOut(std::span<const ItemId> items) {
    w::DragThing t;
    t.kind = "item";
    for (const ItemId id : items) t.values.push_back(std::to_string(id));
    return t;
}

void RequestSelect(State& st, std::vector<ItemId> ids, bool rename) {
    st.hasPending = true;
    st.pending = std::move(ids);
    st.pendingRename = rename;
}

json::Value SaveState(const State& st) {
    json::Value o = json::Value::MakeObject();
    std::vector<ItemId> closed(st.collapsed.begin(), st.collapsed.end());
    std::sort(closed.begin(), closed.end());  // 同じツリーなら同じファイルになる
    json::Value list = json::Value::MakeArray();
    for (const ItemId id : closed) list.Push(static_cast<uint64_t>(id));
    o.Set("collapsed", std::move(list));
    json::Value sel = json::Value::MakeArray();
    for (const ItemId id : st.hasPending ? st.pending : st.sel.ids) sel.Push(static_cast<uint64_t>(id));
    o.Set("selected", std::move(sel));
    return o;
}

void LoadState(State& st, const json::Value& v) {
    st.collapsed.clear();
    for (const auto& id : v.Get("collapsed").Items()) {
        if (id.IsInt() && id.AsInt() > 0) st.collapsed.insert(static_cast<ItemId>(id.AsInt()));
    }
    std::vector<ItemId> sel;
    for (const auto& id : v.Get("selected").Items()) {
        if (id.IsInt() && id.AsInt() > 0) sel.push_back(static_cast<ItemId>(id.AsInt()));
    }
    if (!sel.empty()) RequestSelect(st, std::move(sel));
}

void Draw(ui::Context& c, State& st, Source& src, const Rect& area) {
    auto& p = c.P();
    const ui::Input& in = c.In();
    std::vector<Item> items;
    src.Items(items);
    const float kRowH = std::clamp(src.RowHeight(), 20.f, 200.f);
    // 絵: 低い行では Icon の幅、高い行では正方形で、行が許す限りの高さ。
    const float pictureW = std::max(kIconW, kRowH - 6.f);
    const std::vector<ItemId> before = st.sel.ids;
    bool reselected = false;  // 選択中の行がもう一度押された (下)
    const ItemIndex index(items);  // フレームの間ずっと ID で探す (何千もの項目でも軽いまま)
    // 無くなった (削除された) 閉じた項目は忘れる。残している一覧が際限なく増えないように。
    std::erase_if(st.collapsed, [&](ItemId id) { return !index.Has(id); });

    // --- ソフトが頼んだこと、消えたもの ------------------------------
    if (st.hasPending) {
        st.hasPending = false;
        st.sel.ids = std::move(st.pending);
        st.pending.clear();
        Prune(st.sel, items);
        st.sel.anchor = st.sel.focus = st.sel.ids.empty() ? 0 : st.sel.ids.front();
        for (const ItemId id : st.sel.ids) Reveal(st, items, index, id);
        st.scrollTo = st.sel.focus;
        if (st.pendingRename && st.sel.ids.size() == 1 && src.CanRename(st.sel.ids[0])) {
            StartRename(st, items[index.Find(st.sel.ids[0])]);
            st.filter.clear();  // 新しい項目は、名前を変えるために見えていなければならない
        }
        st.pendingRename = false;
        if (!st.sel.ids.empty()) st.hasKeys = true;  // Delete / F2 が、作ったばかりのものに効く
    }
    Prune(st.sel, items);
    if (st.renaming != 0 && !index.Has(st.renaming)) st.renaming = 0;
    std::erase_if(st.moving, [&](ItemId id) { return !index.Has(id); });

    // --- 検索ボックス ------------------------------------------------------------------------
    Rect body = area;
    if (src.Searchable()) {
        const Rect search = ui::TakeTop(body, 36).Inset(8, 6, 8, 4);
        const ui::Id searchId = c.MakeId("tree.search");
        if (c.Focused() == searchId && c.KeyPressed(ui::Key::Escape)) {
            st.filter.clear();
            c.ClearFocus();
        }
        w::TextField(c, "tree.search", search, st.filter, Tr(L"検索"));
    } else {
        st.filter.clear();
        ui::TakeTop(body, 4);
    }
    // 一番上の行 (Source::Top): 検索ボックスの下で、スクロールする行の外。
    const std::optional<Source::TopRow> top = src.Top();
    std::optional<Rect> topRect;
    if (top) topRect = ui::TakeTop(body, kRowH).Inset(4, 0, 4, 0);
    const Rect view = body.Inset(0, 2, 0, 2);

    std::vector<Row> rows = VisibleRows(items, st.collapsed, st.filter);

    // メニューが開いている間、その下の行は表示だけする。
    const bool inputWas = c.InputEnabled();
    if (st.menu.open) c.SetInputEnabled(false);
    if (c.MousePressed() || c.MousePressed(ui::Mouse::Right)) st.hasKeys = c.IsHovered(view);

    // --- キーボード (最後のクリックがこのツリーで、どのテキストフィールドもキーを持っていないとき) ---
    if (st.hasKeys && c.Focused() == 0 && st.renaming == 0 && !st.dragging && c.InputEnabled() && !in.keys.empty()) {
        const size_t fr = RowOf(items, rows, st.sel.focus);
        auto moveTo = [&](size_t r, bool extend) {
            if (r >= rows.size()) return;
            const ItemId id = items[rows[r].item].id;
            if (extend) {
                if (st.sel.anchor == 0) st.sel.anchor = st.sel.focus != 0 ? st.sel.focus : id;
                Click(st.sel, items, rows, id, false, true);
            } else {
                SelectOnly(st.sel, id);
            }
            st.scrollTo = id;
        };
        const bool shift = in.shift;
        if (!rows.empty()) {
            if (c.KeyPressed(ui::Key::Up) || c.KeyPressed(ui::Key::Up, false, true)) moveTo(fr == npos ? rows.size() - 1 : (fr > 0 ? fr - 1 : 0), shift);
            if (c.KeyPressed(ui::Key::Down) || c.KeyPressed(ui::Key::Down, false, true)) moveTo(fr == npos ? 0 : std::min(rows.size() - 1, fr + 1), shift);
            if (c.KeyPressed(ui::Key::Home) || c.KeyPressed(ui::Key::Home, false, true)) moveTo(0, shift);
            if (c.KeyPressed(ui::Key::End) || c.KeyPressed(ui::Key::End, false, true)) moveTo(rows.size() - 1, shift);
            if (c.KeyPressed(ui::Key::A, true)) SelectAll(st.sel, items, rows);
        }
        if (fr != npos && st.filter.empty()) {
            const Row& row = rows[fr];
            const Item& it = items[row.item];
            if (c.KeyPressed(ui::Key::Left)) {
                if (row.expanded) {
                    st.collapsed.insert(it.id);
                } else if (it.parent != 0) {
                    SelectOnly(st.sel, it.parent);
                    st.scrollTo = it.parent;
                }
            }
            if (c.KeyPressed(ui::Key::Right)) {
                if (row.hasChildren && !row.expanded) {
                    st.collapsed.erase(it.id);
                } else if (row.expanded && fr + 1 < rows.size()) {
                    moveTo(fr + 1, false);
                }
            }
        }
        // メニューのショートカット (その中の 名前を変更 の F2 など)。
        for (const Action& a : MenuFor(src, st, items)) {
            if (a.key != ui::Key::None && a.enabled && a.run && c.KeyPressed(a.key, a.ctrl, a.shift)) {
                a.run();
                break;
            }
        }
        c.RequestRedraw();
        rows = VisibleRows(items, st.collapsed, st.filter);
    }

    // --- スクロール ---------------------------------------------------------------------------
    const float content = kTop + kRowH * static_cast<float>(rows.size()) + kRowH;  // + 最後にドロップするための空き
    if (st.scrollTo != 0) {
        const size_t r = RowOf(items, rows, st.scrollTo);
        if (r != npos) {
            float& off = c.FloatState(c.MakeId(kScrollId));
            const float rowTop = kTop + kRowH * static_cast<float>(r);
            if (rowTop < off) off = rowTop;
            if (rowTop + kRowH > off + view.h) off = rowTop + kRowH - view.h + kTop;
        }
        st.scrollTo = 0;
    }
    if (st.dragging && in.down[0]) {
        // ドラッグ中、上端 / 下端の近くでは: そちらへスクロールする。
        float& off = c.FloatState(c.MakeId(kScrollId));
        const float maxOff = std::max(0.f, content - view.h);
        float step = 0;
        if (in.mouseY < view.y + 20 && in.mouseY >= view.y - 40) step = -8;
        if (in.mouseY > view.Bottom() - 20 && in.mouseY <= view.Bottom() + 40) step = 8;
        const float nv = std::clamp(off + step, 0.f, maxOff);
        if (nv != off) {
            off = nv;
            c.RequestRedraw();
        }
    }
    // --- 一番上の行 (行のスクロールより前: そのクリップの外。2026-11-18 画面で切れていた) ---
    bool topClicked = false, topRightClicked = false;
    if (topRect) {
        const bool hovered = c.IsHovered(*topRect);
        if (hovered && c.MousePressed()) {
            st.sel.ids.clear();
            st.sel.anchor = st.sel.focus = 0;
            st.hasKeys = true;
            topClicked = true;
            c.RequestRedraw();
        }
        if (hovered && c.MousePressed(ui::Mouse::Right)) {
            topRightClicked = true;
            st.sel.ids.clear();
            st.sel.anchor = st.sel.focus = 0;
            st.menuActions = src.TopActions();
            std::vector<Action> rest = MenuFor(src, st, items);
            if (!st.menuActions.empty() && !rest.empty()) {
                Action sep;
                sep.separator = true;
                st.menuActions.push_back(std::move(sep));
            }
            for (auto& a : rest) st.menuActions.push_back(std::move(a));
            if (!st.menuActions.empty()) st.menu.OpenAt(in.mouseX, in.mouseY);
        }
        w::DrawRow(c, *topRect, {top->selected, hovered && !st.dragging, st.hasKeys});
        const float x = topRect->x + 4;
        w::DrawIcon(c, {x + (kArrow - kIconW) * 0.5f + 2, topRect->y, kIconW, topRect->h}, top->icon, theme::TextDim);
        ui::TextStyle name = Style(theme::FontSize, theme::Text);
        name.bold = true;
        p.Text({x + kArrow + 6, topRect->y + (topRect->h - kLineH) * 0.5f, topRect->Right() - x - kArrow - 10, kLineH}, top->name, name);
    }

    const float off = w::BeginScroll(c, kScrollId, view, content);
    const float rowsTop = view.y + kTop - off;
    auto rowRect = [&](size_t r) { return Rect{view.x + 4, rowsTop + kRowH * static_cast<float>(r), view.w - 8, kRowH}; };
    auto textX = [&](int depth) { return view.x + 8 + kIndent * static_cast<float>(depth) + kArrow + 2; };

    if (items.empty()) {
        ui::TextStyle hint = Style(12, theme::TextFaint);
        hint.wrap = true;
        hint.v = ui::VAlign::Top;
        p.Text({view.x + 12, view.y + 6, view.w - 24, 80}, src.EmptyText(), hint);
    } else if (rows.empty()) {
        p.Text({view.x + 12, view.y + 6, view.w - 24, 22}, Tr(L"見つかりません。"), Style(12, theme::TextFaint));
    }

    // --- 行 ------------------------------------------------------------------------------------
    bool rowPressed = false, rowRightClicked = topRightClicked, renameDrawn = false;
    const std::unordered_set<ItemId> selectedSet = st.sel.Set();  // 行ごとではなく 1 度だけ
    for (size_t r = 0; r < rows.size(); ++r) {
        const Rect row = rowRect(r);
        if (row.Bottom() < view.y || row.y > view.Bottom()) continue;
        const Row& rw = rows[r];
        const Item& it = items[rw.item];
        const bool selected = selectedSet.contains(it.id);
        const float indentX = view.x + 8 + kIndent * static_cast<float>(rw.depth);
        const Rect arrow{indentX, row.y, kArrow, row.h};
        const float nameX = textX(rw.depth) + (it.icon != Icon::None ? pictureW + 2 + (pictureW > kIconW ? 4.f : 0.f) : 0.f);
        const Rect label{nameX, row.y + (row.h - kLineH) * 0.5f, row.Right() - nameX - 4, kLineH};
        c.PushId(it.id);

        bool onArrow = false;
        if (rw.hasChildren) {
            const auto b = c.ButtonBehavior(c.MakeId("arrow"), arrow);
            onArrow = b.hovered;
            if (b.clicked && st.filter.empty()) {
                if (rw.expanded) {
                    st.collapsed.insert(it.id);
                } else {
                    st.collapsed.erase(it.id);
                }
            }
        }
        const bool hovered = c.IsHovered(row);
        if (hovered && !onArrow && c.MousePressed() && st.renaming != it.id) {
            rowPressed = true;
            st.pressItem = it.id;
            st.pressX = in.mouseX;
            st.pressY = in.mouseY;
            st.pressDeferred = false;
            if (in.doubleClicked && !in.ctrl && !in.shift && src.CanRename(it.id)) {
                SelectOnly(st.sel, it.id);
                StartRename(st, it);
                st.pressItem = 0;
            } else if (selected && !in.ctrl && !in.shift) {
                st.pressDeferred = st.sel.ids.size() > 1;  // ほかの行はドラッグのために残す
                st.sel.focus = it.id;
                // 選択中の 1 行がもう一度押された: それでも選択として知らせる。そうするとインスペクターは、
                // ほかのものを見せたあとでもそれをもう一度見せる (2026-11-17 画面で: 選択中の
                // エンティティをクリックしても、パーティクル タブのエミッターがそのまま残っていた)。
                reselected = !st.pressDeferred;
            } else {
                Click(st.sel, items, rows, it.id, in.ctrl, in.shift);
            }
            c.RequestRedraw();
        }
        if (hovered && c.MousePressed(ui::Mouse::Right)) {
            rowRightClicked = true;
            if (!selected) SelectOnly(st.sel, it.id);
            st.menuActions = MenuFor(src, st, items);
            if (!st.menuActions.empty()) st.menu.OpenAt(in.mouseX, in.mouseY);
        }

        w::DrawRow(c, row, {selected, hovered && !st.dragging, st.hasKeys});
        // ツリーの外からのドラッグ (Source::CanDrop): この行に落とせる。
        if (!st.dragging && w::DraggedThing()) {
            const ItemId onto = it.id;
            if (const w::DragThing* t = w::DropTarget(c, row, [&](const w::DragThing& d) { return src.CanDrop(d, onto); })) {
                const w::DragThing dropped = *t;
                src.Drop(dropped, onto);
                c.RequestRedraw();
            }
        }
        if (rw.hasChildren) {
            w::DrawIcon(c, arrow, rw.expanded ? w::Icon::ChevronDown : w::Icon::ChevronRight, onArrow ? theme::Text : theme::TextDim);
        }
        if (it.icon != Icon::None && !src.DrawPicture(c, it, {textX(rw.depth), row.y + (row.h - pictureW) * 0.5f, pictureW, pictureW})) {
            const ui::Color col = it.dimmed ? theme::TextFaint : it.icon == Icon::Folder ? theme::Folder : theme::TextDim;
            w::DrawIcon(c, {textX(rw.depth) + (pictureW - kIconW) * 0.5f, row.y, kIconW, row.h}, PictureOf(it.icon), col);
        }

        if (st.renaming == it.id) {
            renameDrawn = true;
            const ui::Id fid = c.MakeId("rename");
            if (st.renameStart) {
                c.FocusText(fid, Utf8ToWide(st.renameText), true);
                st.renameStart = false;
            }
            const bool focusedBefore = c.Focused() == fid;
            const bool enter = focusedBefore && c.KeyPressed(ui::Key::Enter);
            const bool escape = focusedBefore && c.KeyPressed(ui::Key::Escape);
            w::TextField(c, "rename", label.Inset(-4, 1, 0, 1), st.renameText);
            if (escape) {
                c.ClearFocus();
                st.renaming = 0;
            } else if (enter || c.Focused() != fid) {
                if (enter) c.ClearFocus();
                const ItemId id = st.renaming;
                st.renaming = 0;
                if (!st.renameText.empty() && st.renameText != it.name) src.Rename(id, st.renameText);
            }
        } else {
            const float badgeW = it.badge.empty() ? 0.f : p.TextWidth(it.badge, 11) + 12;
            p.Text(label.Inset(0, 0, badgeW, 0), Utf8ToWide(it.name), Style(theme::FontSize, it.dimmed ? theme::TextFaint : theme::Text));
            if (!it.badge.empty()) p.Text(label.Inset(0, 0, 6, 0), it.badge, Style(11, theme::Accent, ui::HAlign::Right));
        }
        c.PopId();
    }
    // スクロールで見えなくなった行の名前変更は、入力した内容で終わらせる。
    if (st.renaming != 0 && !renameDrawn && !st.renameStart) {
        const size_t i = Find(items, st.renaming);
        if (i != npos && !st.renameText.empty() && st.renameText != items[i].name) src.Rename(st.renaming, st.renameText);
        st.renaming = 0;
    }

    // --- 何もない所のクリック ------------------------------------------------------------------
    const Rect emptyArea{view.x, view.y, view.w - 12, view.h};  // スクロールバーは含めない
    if (!rowPressed && c.IsHovered(emptyArea) && c.MousePressed() && !in.ctrl && !in.shift) {
        st.sel.ids.clear();
        st.sel.anchor = st.sel.focus = 0;
    }
    if (!rowRightClicked && c.IsHovered(emptyArea) && c.MousePressed(ui::Mouse::Right)) {
        st.sel.ids.clear();
        st.sel.anchor = st.sel.focus = 0;
        st.menuActions = MenuFor(src, st, items);
        if (!st.menuActions.empty()) st.menu.OpenAt(in.mouseX, in.mouseY);
    }

    // --- ドラッグ -----------------------------------------------------------------------------
    std::optional<Drop> drop;
    if (st.pressItem != 0) {
        if (!st.dragging && in.down[0] && src.CanMove() &&
            std::hypot(in.mouseX - st.pressX, in.mouseY - st.pressY) >= kDragStart) {
            if (!st.sel.Contains(st.pressItem)) SelectOnly(st.sel, st.pressItem);
            st.moving = Roots(items, st.sel.ids);
            st.dragging = !st.moving.empty();
            st.pressDeferred = false;
        }
        if (st.dragging && c.KeyPressed(ui::Key::Escape)) {
            st.dragging = false;
            st.pressItem = 0;
            st.moving.clear();
            w::EndDragThing(c);
            c.RequestRedraw();
        }
    }
    // ツリーの外では、行を別のパネル (参照フィールド) にドロップできる。
    if (st.dragging) w::SetDragThing(src.DragOut(st.moving));
    const bool dropOnTop = st.dragging && in.mouseInside && topRect && topRect->Contains(in.mouseX, in.mouseY);
    if (dropOnTop) {
        drop = DropAtEnd(items, st.moving);  // 最上位の最後
    } else if (st.dragging && in.mouseInside && view.Contains(in.mouseX, in.mouseY)) {
        const float rel = in.mouseY - rowsTop;
        if (rel < 0) {
            drop = DropOnRow(items, rows, 0, 0.f, st.moving);
        } else {
            const size_t r = static_cast<size_t>(rel / kRowH);
            const float frac = (rel - kRowH * static_cast<float>(r)) / kRowH;
            drop = r < rows.size() ? DropOnRow(items, rows, r, frac, st.moving) : DropAtEnd(items, st.moving);
        }
    }
    if (st.dragging && drop && !dropOnTop) {
        // 行き先: 行の間の線、または入る行を囲む枠。
        if (drop->mark == Drop::Mark::Inside) {
            const size_t r = RowOf(items, rows, drop->target);
            if (r != npos) p.StrokeRoundRect(rowRect(r), 4, theme::Accent, 1.5f);
        } else {
            float y = rowsTop + kRowH * static_cast<float>(rows.size());
            int depth = 0;
            if (drop->mark != Drop::Mark::End) {
                const size_t r = RowOf(items, rows, drop->target);
                if (r != npos) {
                    const Row& rw = rows[r];
                    y = rowRect(r).y + (drop->mark == Drop::Mark::After ? kRowH : 0.f);
                    depth = rw.depth + (drop->mark == Drop::Mark::After && rw.expanded ? 1 : 0);
                }
            }
            const float x0 = textX(depth) - 4;
            p.FillRect({x0, y - 1, view.Right() - 8 - x0, 2}, theme::Accent);
            p.FillRoundRect({x0 - 3, y - 3, 6, 6}, 3, theme::Accent);
        }
    }
    w::EndScroll(c, kScrollId, view, content);
    if (dropOnTop) p.StrokeRoundRect(*topRect, 4, theme::Accent, 1.5f);  // 一番上の行の中へ: その周りに枠

    if (st.pressItem != 0 && !in.down[0]) {
        // 離した。
        if (st.dragging && drop) {
            const std::vector<ItemId> moving = st.moving;
            src.Move(moving, drop->parent, drop->index);
            st.collapsed.erase(drop->parent);  // 行き先が見えるようにする
        } else if (!st.dragging && st.pressDeferred) {
            SelectOnly(st.sel, st.pressItem);
        }
        if (st.dragging) w::EndDragThing(c);  // (これのあとに描くパネルも、離したのを見る)
        st.pressItem = 0;
        st.pressDeferred = false;
        st.dragging = false;
        st.moving.clear();
        c.RequestRedraw();
    }
    if (st.dragging) {
        // ドラッグしているものを、マウスの横に (すべての上に。パネルの外でも)。
        const size_t i = st.moving.size() == 1 ? Find(items, st.moving[0]) : npos;
        const std::wstring tag = i != npos ? Utf8ToWide(items[i].name) : TrF(L"{} 個", st.moving.size());
        w::DragTag(c, tag, drop.has_value());
    }

    c.SetInputEnabled(inputWas);

    // --- 右クリックメニュー (フレームの終わりに: パネルで切られないように) -----------------
    w::ActionMenu(c, st.menu, st.menuActions);

    if (st.sel.ids != before || reselected) src.SelectionChanged(st.sel.ids);
    if (topClicked) src.TopClicked();
}

}  // namespace rm::editor::tree
