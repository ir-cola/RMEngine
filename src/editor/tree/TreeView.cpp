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
constexpr float kRowH = 24.f;
constexpr float kIndent = 16.f;
constexpr float kArrow = 16.f;
constexpr float kDragStart = 6.f;  // DIPs the mouse moves before a press becomes a drag
constexpr float kTop = 4.f;        // space above the first row
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

// Opens every ancestor of `id`, so the row can be seen.
void Reveal(State& st, std::span<const Item> items, ItemId id) {
    size_t i = Find(items, id);
    for (size_t steps = 0; i != npos && steps <= items.size(); ++steps) {
        const ItemId parent = items[i].parent;
        if (parent == 0) break;
        st.collapsed.erase(parent);
        i = Find(items, parent);
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

void RequestSelect(State& st, std::vector<ItemId> ids, bool rename) {
    st.hasPending = true;
    st.pending = std::move(ids);
    st.pendingRename = rename;
}

void Draw(ui::Context& c, State& st, Source& src, const Rect& area) {
    auto& p = c.P();
    const ui::Input& in = c.In();
    std::vector<Item> items;
    src.Items(items);
    const std::vector<ItemId> before = st.sel.ids;

    // --- What the software asked for, and what disappeared ------------------------------
    if (st.hasPending) {
        st.hasPending = false;
        st.sel.ids = std::move(st.pending);
        st.pending.clear();
        Prune(st.sel, items);
        st.sel.anchor = st.sel.focus = st.sel.ids.empty() ? 0 : st.sel.ids.front();
        for (const ItemId id : st.sel.ids) Reveal(st, items, id);
        st.scrollTo = st.sel.focus;
        if (st.pendingRename && st.sel.ids.size() == 1 && src.CanRename(st.sel.ids[0])) {
            StartRename(st, items[Find(items, st.sel.ids[0])]);
            st.filter.clear();  // the new item must be visible to be renamed
        }
        st.pendingRename = false;
        if (!st.sel.ids.empty()) st.hasKeys = true;  // Delete / F2 work on what was just made
    }
    Prune(st.sel, items);
    if (st.renaming != 0 && Find(items, st.renaming) == npos) st.renaming = 0;
    std::erase_if(st.moving, [&](ItemId id) { return Find(items, id) == npos; });

    // --- Search box ------------------------------------------------------------------------
    Rect body = area;
    const Rect search = ui::TakeTop(body, 36).Inset(8, 6, 8, 4);
    const ui::Id searchId = c.MakeId("tree.search");
    if (c.Focused() == searchId && c.KeyPressed(ui::Key::Escape)) {
        st.filter.clear();
        c.ClearFocus();
    }
    w::TextField(c, "tree.search", search, st.filter, Tr(L"検索"));
    const Rect view = body.Inset(0, 2, 0, 2);

    std::vector<Row> rows = VisibleRows(items, st.collapsed, st.filter);

    // While the menu is open, the rows under it only show.
    const bool inputWas = c.InputEnabled();
    if (st.menu.open) c.SetInputEnabled(false);
    if (c.MousePressed() || c.MousePressed(ui::Mouse::Right)) st.hasKeys = c.IsHovered(view);

    // --- Keyboard (when the last click was in this tree and no text field has the keys) ---
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
        // The menu's shortcuts (F2 for 名前を変更 among them).
        for (const Action& a : MenuFor(src, st, items)) {
            if (a.key != ui::Key::None && a.enabled && a.run && c.KeyPressed(a.key, a.ctrl, a.shift)) {
                a.run();
                break;
            }
        }
        c.RequestRedraw();
        rows = VisibleRows(items, st.collapsed, st.filter);
    }

    // --- Scrolling ---------------------------------------------------------------------------
    const float content = kTop + kRowH * static_cast<float>(rows.size()) + kRowH;  // + room to drop at the end
    if (st.scrollTo != 0) {
        const size_t r = RowOf(items, rows, st.scrollTo);
        if (r != npos) {
            float& off = c.FloatState(c.MakeId(kScrollId));
            const float top = kTop + kRowH * static_cast<float>(r);
            if (top < off) off = top;
            if (top + kRowH > off + view.h) off = top + kRowH - view.h + kTop;
        }
        st.scrollTo = 0;
    }
    if (st.dragging && in.down[0]) {
        // Near the top / bottom edge while dragging: scroll towards it.
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

    // --- Rows ------------------------------------------------------------------------------------
    bool rowPressed = false, rowRightClicked = false, renameDrawn = false;
    for (size_t r = 0; r < rows.size(); ++r) {
        const Rect row = rowRect(r);
        if (row.Bottom() < view.y || row.y > view.Bottom()) continue;
        const Row& rw = rows[r];
        const Item& it = items[rw.item];
        const bool selected = st.sel.Contains(it.id);
        const float indentX = view.x + 8 + kIndent * static_cast<float>(rw.depth);
        const Rect arrow{indentX, row.y, kArrow, row.h};
        const Rect label{textX(rw.depth), row.y, row.Right() - textX(rw.depth) - 4, row.h};
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
                st.pressDeferred = st.sel.ids.size() > 1;  // keep the others for a drag
                st.sel.focus = it.id;
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
        if (rw.hasChildren) {
            w::DrawIcon(c, arrow, rw.expanded ? w::Icon::ChevronDown : w::Icon::ChevronRight, onArrow ? theme::Text : theme::TextDim);
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
            p.Text(label, Utf8ToWide(it.name), Style(theme::FontSize, it.dimmed ? theme::TextFaint : theme::Text));
        }
        c.PopId();
    }
    // A rename whose row scrolled out of view is finished with what was typed.
    if (st.renaming != 0 && !renameDrawn && !st.renameStart) {
        const size_t i = Find(items, st.renaming);
        if (i != npos && !st.renameText.empty() && st.renameText != items[i].name) src.Rename(st.renaming, st.renameText);
        st.renaming = 0;
    }

    // --- Clicks on empty space ------------------------------------------------------------------
    const Rect emptyArea{view.x, view.y, view.w - 12, view.h};  // not the scroll bar
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

    // --- Dragging -----------------------------------------------------------------------------
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
            c.RequestRedraw();
        }
    }
    if (st.dragging && in.mouseInside && view.Contains(in.mouseX, in.mouseY)) {
        const float rel = in.mouseY - rowsTop;
        if (rel < 0) {
            drop = DropOnRow(items, rows, 0, 0.f, st.moving);
        } else {
            const size_t r = static_cast<size_t>(rel / kRowH);
            const float frac = (rel - kRowH * static_cast<float>(r)) / kRowH;
            drop = r < rows.size() ? DropOnRow(items, rows, r, frac, st.moving) : DropAtEnd(items, st.moving);
        }
    }
    if (st.dragging && drop) {
        // Where it goes: a line between rows, or a frame around the row it goes into.
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

    if (st.pressItem != 0 && !in.down[0]) {
        // Released.
        if (st.dragging && drop) {
            const std::vector<ItemId> moving = st.moving;
            src.Move(moving, drop->parent, drop->index);
            st.collapsed.erase(drop->parent);  // show where they went
        } else if (!st.dragging && st.pressDeferred) {
            SelectOnly(st.sel, st.pressItem);
        }
        st.pressItem = 0;
        st.pressDeferred = false;
        st.dragging = false;
        st.moving.clear();
        c.RequestRedraw();
    }
    if (st.dragging) {
        // What is being dragged, next to the mouse (over everything, also outside the panel).
        const size_t i = st.moving.size() == 1 ? Find(items, st.moving[0]) : npos;
        const std::wstring tag = i != npos ? Utf8ToWide(items[i].name) : TrF(L"{} 個", st.moving.size());
        const bool ok = drop.has_value();
        c.AddOverlay([&c, tag, ok] {
            auto& pp = c.P();
            const float wdt = std::min(260.f, pp.TextWidth(tag, 12) + 20);
            const Rect r{c.In().mouseX + 14, c.In().mouseY + 10, wdt, 24};
            w::DrawSurface(c, r, w::Surface::Popup, 5.f);
            if (ok) pp.StrokeRoundRect(r, 5, theme::Accent);
            pp.Text(r.Inset(10, 0, 10, 0), tag, Style(12, ok ? theme::Text : theme::TextDim));
        });
    }

    c.SetInputEnabled(inputWas);

    // --- The right-click menu (at the end of the frame: not cut off by the panel) -----------------
    w::ActionMenu(c, st.menu, st.menuActions);

    if (st.sel.ids != before) src.SelectionChanged(st.sel.ids);
}

}  // namespace rm::editor::tree
