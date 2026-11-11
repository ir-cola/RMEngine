#include "editor/lang/Lang.h"
#include "editor/inspector/Inspector.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/reflect/Object.h"
#include "editor/tree/TreeModel.h"
#include "editor/widgets/Theme.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace rm::editor::inspector {

using reflect::FieldDesc;
using reflect::FieldPath;
using reflect::FieldType;
using reflect::TypeDesc;
using reflect::Unknowns;
using ui::Rect;

namespace {

constexpr float kRow = 30.f;
constexpr float kIndent = 14.f;
constexpr float kAxisW = 14.f;   // the X / Y / Z in front of a vector's boxes
constexpr float kElemRow = 28.f;

ui::TextStyle Text(float size, ui::Color color, bool bold = false) {
    ui::TextStyle ts;
    ts.size = size;
    ts.color = color;
    ts.bold = bold;
    return ts;
}

// An edit made inside an array element, turned into the edit of the array field (or nothing).
using Wrap = std::function<std::optional<Edit>(Edit)>;

struct Pass {
    ui::Context& c;
    State& st;
    Rect view;
    size_t block = 0;
    float y = 0;
    float labelW = 0;
    std::optional<Edit> edit;
    std::wstring tooltip;
    Wrap wrap;  // inside an array element (null: the edit is the field's own)
    const Refs* refs = nullptr;

    void Put(Edit e) {
        if (!wrap) {
            edit = std::move(e);
        } else if (auto outer = wrap(std::move(e))) {
            edit = std::move(*outer);
        }
    }
};

// Every typing / dragging session gets its own merge key (unique in the program: two inspectors never share one).
uint64_t NewSession() {
    static uint64_t next = 0;
    return ++next;
}

// A text box whose text is used as it is typed: returned after every change, and once more on
// Enter / leaving the field when the value shown differs from it. Esc returns the value from before
// the field was entered. While it is not being edited it simply shows `shown`. `merge` = the key of
// this typing (one undo entry).
std::optional<std::string> LiveField(Pass& x, std::string_view id, const Rect& r, const std::string& shown, bool enabled,
                                     uint64_t& merge) {
    ui::Context& c = x.c;
    const ui::Id uid = c.MakeId(id);
    const bool wasEditing = x.st.editing == uid;
    std::string& buf = x.st.text[uid];
    if (c.Focused() != uid && !wasEditing) buf = shown;
    const bool focusedBefore = c.Focused() == uid;
    const bool enter = focusedBefore && c.KeyPressed(ui::Key::Enter);
    const bool escape = focusedBefore && c.KeyPressed(ui::Key::Escape);
    const std::string typedBefore = buf;
    w::TextField(c, id, r, buf, {}, enabled, true);
    const bool focusedNow = c.Focused() == uid;
    if (focusedNow && !wasEditing) {
        x.st.editing = uid;
        x.st.original = shown;
        x.st.session = NewSession();
    }
    merge = x.st.session;
    if (escape) {
        c.ClearFocus();
        x.st.editing = 0;
        buf = x.st.original;
        return x.st.original != shown ? std::optional<std::string>(x.st.original) : std::nullopt;
    }
    if (enter || (wasEditing && !focusedNow)) {
        if (enter) c.ClearFocus();
        if (x.st.editing == uid) x.st.editing = 0;
        if (buf == shown) return std::nullopt;
        return buf;
    }
    if (focusedNow && buf != typedBefore) return buf;
    return std::nullopt;
}

// Dragging `handle` left / right changes a number, like a slider without end (the cursor hides and
// the mouse goes on past the screen's edge). The speed is chosen when the drag starts: the field's
// Step per pixel, or more for a big number (Unity's rule); Shift x4, Alt x1/4. Returns the new
// value while it changes; `merge` = the drag's key (one undo entry).
std::optional<double> Scrub(Pass& x, std::string_view id, const Rect& handle, double current, const FieldDesc& f, bool enabled,
                            uint64_t& merge) {
    if (!enabled || handle.w <= 0) return std::nullopt;
    ui::Context& c = x.c;
    const ui::Input& in = c.In();
    const ui::Id uid = c.MakeId(id);
    const auto b = c.ButtonBehavior(uid, handle);
    if (b.hovered || b.held) c.SetCursor(ui::Cursor::SizeWE);
    if (!b.held) {
        if (x.st.scrub == uid) x.st.scrub = 0;
        return std::nullopt;
    }
    if (x.st.scrub != uid) {
        x.st.scrub = uid;
        x.st.scrubStart = current;
        x.st.scrubMoved = 0;
        x.st.scrubSession = NewSession();
        x.st.scrubSpeed = f.hasStep && f.step > 0 ? f.step : std::max(1.0, std::sqrt(std::fabs(current))) * 0.03;
        c.ClearFocus();  // a field being typed into is finished first
    }
    c.LockMouse();
    merge = x.st.scrubSession;
    if (in.mouseDX == 0) return std::nullopt;
    x.st.scrubMoved += in.mouseDX * (in.shift ? 4.0 : in.alt ? 0.25 : 1.0);
    const double v = reflect::SanitizeNumber(f, x.st.scrubStart + x.st.scrubMoved * x.st.scrubSpeed, current);
    return v != current ? std::optional<double>(v) : std::nullopt;
}

// Shown in a field whose value is not the same in every object edited together.
constexpr char kMixed[] = "\xE2\x80\x94";  // —

using Others = std::span<const void* const>;

size_t Components(FieldType t) { return t == FieldType::Vec2 ? 2 : t == FieldType::Vec3 ? 3 : t == FieldType::Color ? 4 : 0; }

// One value: a number, a vector, text, a check box or a list - of a field or of an array element.
struct Spot {
    const FieldDesc& f;          // its field (number rules: Min / Max / Int / Step)
    reflect::ValueType vt;       // its type (the field's, or the array's element)
    const void* value = nullptr;
    std::vector<const void*> others;  // the same value in the objects edited together
    const Unknowns* u = nullptr;      // the owner's unknowns (an enum name kept) - null for elements
    const char* enumKey = nullptr;    // the field name the enum's unknown name is kept under
    bool enabled = true;
    Rect r;                       // where the value goes
    Rect scrub;                   // a label that changes a single number when dragged (w = 0: none)
    // The Edit for a new value (path, before, label - the block's); `merge` / `component` are added.
    std::function<Edit(json::Value)> editFor;
};

// The id a reference holds, as text ("" = none).
std::string RefValue(FieldType t, const void* v) {
    if (t == FieldType::AssetRef) return static_cast<const AssetRef*>(v)->guid;
    const uint64_t id = static_cast<const EntityRef*>(v)->id;
    return id == 0 ? std::string() : std::to_string(id);
}

json::Value RefJson(FieldType t, const std::string& value) {
    if (t == FieldType::AssetRef) return json::Value(value);
    uint64_t id = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') return json::Value(uint64_t{0});
        id = id * 10 + static_cast<uint64_t>(ch - '0');
    }
    return json::Value(id);
}

// A reference: its name in a box (a click opens the list), × to clear it, and a place to drop one.
template <class Put>
void DrawRef(Pass& x, const Spot& s, const Put& put) {
    ui::Context& c = x.c;
    const bool isAsset = s.vt.type == FieldType::AssetRef;
    const std::string kinds = isAsset && s.f.assetKinds ? s.f.assetKinds : "";
    const std::string cur = RefValue(s.vt.type, s.value);
    const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return RefValue(s.vt.type, o) != cur; });
    std::optional<RefChoice> found;
    if (!cur.empty() && x.refs) {
        if (isAsset && x.refs->asset) found = x.refs->asset(cur);
        if (!isAsset && x.refs->item) found = x.refs->item(static_cast<const EntityRef*>(s.value)->id);
    }
    const std::wstring shown = differ ? Utf8ToWide(kMixed) : cur.empty() ? Tr(L"(なし)") : found ? found->name : Tr(L"(見つかりません)");
    const bool canClear = s.enabled && (!cur.empty() || differ);
    Rect face = s.r;
    if (canClear) face.w -= 26;
    const ui::Id popupId = c.MakeId("v");
    const auto b = w::ComboBox(c, "v", face, shown, s.enabled && x.refs != nullptr,
                               found && !differ ? std::optional<w::Icon>(found->icon) : std::nullopt);
    if (b.hovered && !differ) {
        if (found && !found->detail.empty()) x.tooltip = found->detail;
        if (!found && !cur.empty()) {
            x.tooltip = isAsset ? TrF(L"ID {} のアセットはプロジェクトにありません。値はそのまま残ります", Utf8ToWide(cur))
                                : TrF(L"ID {} の物はありません。値はそのまま残ります", Utf8ToWide(cur));
        }
    }
    if (canClear && w::IconButton(c, "clear", {face.Right() + 2, s.r.y, 24, s.r.h}, w::Icon::Close)) put(RefJson(s.vt.type, ""), 0, -1);
    if (!s.enabled || !x.refs) return;

    // Dropped from the アセット panel / the 階層.
    const Refs* refs = x.refs;
    const auto accepts = [&](const w::DragThing& t) {
        if (t.values.empty()) return false;
        if (!isAsset) return t.kind == "item";
        if (t.kind != "asset" || !refs->asset) return false;
        const auto a = refs->asset(t.values[0]);
        const auto k = a ? asset::KindFromKey(a->kind) : std::nullopt;
        return k && asset::KindMatches(*k, kinds);
    };
    if (const w::DragThing* t = w::DropTarget(c, s.r, accepts)) put(RefJson(s.vt.type, t->values[0]), 0, -1);

    if (b.clicked) {
        x.st.popup.OpenAt(face.x, face.Bottom() + 2);
        x.st.popupId = popupId;
        x.st.pickSearch.clear();
    }
    if (!x.st.popup.open || x.st.popupId != popupId) return;
    // The list (at the end of the frame, over everything): (なし) and what there is of the kinds asked.
    std::vector<RefChoice> choices;
    if (isAsset && refs->assets) {
        for (RefChoice& r : refs->assets()) {
            const auto k = asset::KindFromKey(r.kind);
            if (k && asset::KindMatches(*k, kinds)) choices.push_back(std::move(r));
        }
    } else if (!isAsset && refs->items) {
        choices = refs->items();
    }
    State* st = &x.st;
    const bool withOthers = !s.others.empty();
    const float width = std::max(s.r.w, 300.f);
    const FieldType type = s.vt.type;
    // Assets: their thumbnails in the list (the same ones as the アセット panel's).
    auto thumbnail = isAsset ? refs->thumbnail : nullptr;
    c.AddOverlay([&c, st, choices = std::move(choices), width, withOthers, type, editFor = s.editFor, wrap = x.wrap, thumbnail] {
        std::vector<const RefChoice*> picks;
        std::vector<w::PickRow> rows;
        if (st->pickSearch.empty()) {  // "(なし)" first, while nothing is typed
            rows.push_back({Tr(L"(なし)")});
            picks.push_back(nullptr);
        }
        for (const RefChoice& r : choices) {
            if (!st->pickSearch.empty() && !tree::NameMatches(WideToUtf8(r.name), st->pickSearch)) continue;
            rows.push_back({r.name, r.detail, true, r.icon});
            picks.push_back(&r);
        }
        const bool wasEnabled = c.InputEnabled();
        c.SetInputEnabled(true);
        std::function<ID2D1Bitmap*(size_t, float)> picture;
        if (thumbnail) {
            picture = [&](size_t i, float size) -> ID2D1Bitmap* { return picks[i] ? thumbnail(c, picks[i]->value, size) : nullptr; };
        }
        const int picked = w::PickerPopup(c, st->popup, "refpick", st->pickSearch, rows, width, thumbnail ? 40.f : 26.f, picture);
        c.SetInputEnabled(wasEnabled);
        if (picked < 0) return;
        const RefChoice* r = picks[static_cast<size_t>(picked)];
        Edit ed = editFor(RefJson(type, r ? r->value : std::string()));
        if (ed.before == ed.after && !withOthers) return;
        if (wrap) {
            if (auto outer = wrap(std::move(ed))) st->pending = std::move(*outer);
        } else {
            st->pending = std::move(ed);
        }
    });
}

void DrawValue(Pass& x, const Spot& s) {
    ui::Context& c = x.c;
    auto& p = c.P();
    auto put = [&](json::Value after, uint64_t merge = 0, int component = -1) {
        Edit e = s.editFor(std::move(after));
        e.merge = merge;
        e.component = component;
        // With others, a change counts even when this object has the value already (they may not).
        if (e.before != e.after || !s.others.empty()) x.Put(std::move(e));
    };
    uint64_t merge = 0;

    switch (s.vt.type) {
        case FieldType::Bool: {
            bool v = *static_cast<const bool*>(s.value);
            // Not the same in all the objects edited together: a dash (like the "—" of a number).
            const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return *static_cast<const bool*>(o) != v; });
            if (w::Checkbox(c, "v", {s.r.x, s.r.y, 28, s.r.h}, v, L"", differ) && s.enabled) put(json::Value(v));
            break;
        }
        case FieldType::Int:
        case FieldType::Float: {
            const bool isInt = s.vt.type == FieldType::Int;
            auto read = [&](const void* q) { return isInt ? static_cast<double>(*static_cast<const int*>(q)) : *static_cast<const float*>(q); };
            const double cur = read(s.value);
            const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return read(o) != cur; });
            auto number = [&](double n) { return isInt ? json::Value(static_cast<int64_t>(n)) : json::Value(static_cast<double>(static_cast<float>(n))); };
            if (auto v = Scrub(x, "scrub", s.scrub, cur, s.f, s.enabled, merge)) put(number(*v), merge);
            if (auto typed = LiveField(x, "v", s.r, differ ? kMixed : reflect::FormatNumber(s.f, cur), s.enabled, merge)) {
                if (auto n = reflect::NumberFromInput(s.f, *typed, cur)) put(number(*n), merge);
            }
            break;
        }
        case FieldType::String: {
            const std::string& cur = *static_cast<const std::string*>(s.value);
            const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return *static_cast<const std::string*>(o) != cur; });
            if (auto typed = LiveField(x, "v", s.r, differ ? std::string(kMixed) : cur, s.enabled, merge)) put(json::Value(*typed), merge);
            break;
        }
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Color: {
            const size_t n = Components(s.vt.type);
            const float* values = static_cast<const float*>(s.value);
            const bool isColor = s.vt.type == FieldType::Color;
            Rect area = s.r;
            if (isColor) {
                const Rect swatch = ui::TakeLeft(area, 26);
                const Color col{values[0], values[1], values[2], values[3]};
                p.FillRoundRect(swatch.Inset(0, 1, 4, 1), 4, col);
                p.StrokeRoundRect(swatch.Inset(0, 1, 4, 1), 4, theme::Border);
            }
            const float each = (area.w - 4.f * static_cast<float>(n - 1)) / static_cast<float>(n);
            static const char* kIds[] = {"x", "y", "z", "w"};
            static const wchar_t* kAxes[] = {L"X", L"Y", L"Z", L"W"};
            static const wchar_t* kChannels[] = {L"R", L"G", L"B", L"A"};
            for (size_t i = 0; i < n; ++i) {
                const Rect cell{area.x + (each + 4) * static_cast<float>(i), area.y, each, area.h};
                // A letter in front of each box: which one it is, and a handle to drag it.
                const Rect handle{cell.x, cell.y, kAxisW, cell.h};
                const Rect box{cell.x + kAxisW, cell.y, cell.w - kAxisW, cell.h};
                const ui::Color letter = !s.enabled || isColor ? theme::TextFaint
                                         : i == 0             ? theme::AxisX()
                                         : i == 1             ? theme::AxisY()
                                         : i == 2             ? theme::AxisZ()
                                                              : theme::TextFaint;
                ui::TextStyle ls = Text(11, letter, true);
                ls.h = ui::HAlign::Center;
                p.Text(handle, isColor ? kChannels[i] : kAxes[i], ls);
                const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return static_cast<const float*>(o)[i] != values[i]; });
                auto withComponent = [&](double v) {
                    json::Value arr = json::Value::MakeArray();
                    for (size_t k = 0; k < n; ++k) arr.Push(static_cast<double>(k == i ? static_cast<float>(v) : values[k]));
                    return arr;
                };
                c.PushId(kIds[i]);
                if (auto v = Scrub(x, "scrub", handle, values[i], s.f, s.enabled, merge)) put(withComponent(*v), merge, static_cast<int>(i));
                if (auto typed = LiveField(x, "v", box, differ ? kMixed : reflect::FormatNumber(s.f, values[i]), s.enabled, merge)) {
                    if (auto v = reflect::NumberFromInput(s.f, *typed, values[i])) put(withComponent(*v), merge, static_cast<int>(i));
                }
                c.PopId();
            }
            break;
        }
        case FieldType::Enum: {
            const reflect::EnumDesc& e = *s.vt.enumType;
            const int64_t cur = e.get(s.value);
            const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return e.get(o) != cur; });
            std::wstring shown;
            if (differ) {
                shown = Utf8ToWide(kMixed);
            } else if (const std::string* unknown = s.u && s.enumKey ? s.u->Enum(s.enumKey) : nullptr) {
                shown = Utf8ToWide(*unknown) + Tr(L" (不明)");
            } else if (const reflect::EnumItem* it = reflect::FindItem(e, cur)) {
                shown = Utf8ToWide(TrU8(it->label));
            } else {
                shown = std::to_wstring(cur);
            }
            const ui::Id popupId = c.MakeId("v");
            const auto b = w::ComboBox(c, "v", s.r, shown, s.enabled);
            if (b.clicked) {
                x.st.popup.OpenAt(s.r.x, s.r.Bottom() + 2);
                x.st.popupId = popupId;
            }
            if (x.st.popup.open && x.st.popupId == popupId) {
                // The list is drawn at the end of the frame (not cut off by the panel). The choice is
                // handed back through State and returned by the next Draw.
                const float width = std::max(s.r.w, 180.f);
                State* st = &x.st;
                const bool withOthers = !s.others.empty();
                c.AddOverlay([&c, st, &e, width, withOthers, editFor = s.editFor, wrap = x.wrap] {
                    std::vector<w::MenuItem> items;
                    for (const auto& it : e.items) items.push_back({Utf8ToWide(TrU8(it.label))});
                    const bool wasEnabled = c.InputEnabled();
                    c.SetInputEnabled(true);
                    const int picked = w::MenuPopup(c, st->popup, items, width);
                    c.SetInputEnabled(wasEnabled);
                    if (picked < 0) return;
                    Edit ed = editFor(json::Value(e.items[static_cast<size_t>(picked)].name));
                    if (ed.before == ed.after && !withOthers) return;
                    if (wrap) {
                        if (auto outer = wrap(std::move(ed))) st->pending = std::move(*outer);
                    } else {
                        st->pending = std::move(ed);
                    }
                });
            }
            break;
        }
        case FieldType::AssetRef:
        case FieldType::EntityRef: DrawRef(x, s, put); break;
        case FieldType::Struct:
        case FieldType::Array: break;  // (not single values: DrawField)
    }
}

void DrawStruct(Pass& x, const TypeDesc& t, const void* obj, Others others, const Unknowns* u, FieldPath& prefix, float indent);

// An array: its count and ＋, then every element with its grip, index, value and ×. Every change is
// the whole array, written as it is saved.
void DrawArray(Pass& x, const TypeDesc& ownerType, const FieldDesc& f, const void* owner, Others others, const Unknowns* u,
               const FieldPath& path, float indent, const Rect& valueR, bool enabled, const std::function<Edit(json::Value)>& editFor) {
    ui::Context& c = x.c;
    auto& p = c.P();
    const ui::Input& in = c.In();
    const reflect::ArrayOps& ops = *f.array;
    const void* value = f.Get(owner);
    const size_t count = ops.size(value);
    const json::Value arr = reflect::SaveField(f, owner, u);
    auto put = [&](json::Value after) {
        Edit e = editFor(std::move(after));
        if (e.before != e.after || !others.empty()) x.Put(std::move(e));
    };
    const bool differ = std::any_of(others.begin(), others.end(), [&](const void* o) { return reflect::SaveField(f, o, nullptr) != arr; });

    // "N 個" and ＋ on the field's row.
    const Rect plusR{valueR.Right() - 26, valueR.y, 26, valueR.h};
    p.Text({valueR.x, valueR.y, valueR.w - 30, valueR.h}, differ ? Utf8ToWide(kMixed) : TrF(L"{} 個", count), Text(12, theme::TextDim));
    if (w::IconButton(c, "add", plusR, w::Icon::Plus, enabled && !differ)) {
        json::Value after = arr;
        if (count > 0) {
            after.Push(arr.At(count - 1));  // a copy of the last (what is usually wanted next)
        } else {
            // One element with its default value: a copy of the owner with the array made one longer.
            reflect::Object tmp(ownerType);
            ownerType.copy(tmp.Get(), owner);
            ops.resize(f.Get(tmp.Get()), 1);
            after = reflect::SaveField(f, tmp.Get(), nullptr);
        }
        put(std::move(after));
    }
    if (differ) {
        p.Text({x.view.x + 12 + indent + kIndent, x.y, x.view.w - 24 - indent - kIndent, 22}, Tr(L"選んだ物で中身が違うので、まとめては変えられません。"),
               Text(12, theme::TextFaint));
        x.y += 24;
        return;
    }

    const ui::Id arrayId = c.MakeId("array");
    std::vector<float> tops;  // each element's row, for dropping a dragged one
    const reflect::TypeDesc* elemStruct = ops.element.type == FieldType::Struct ? ops.element.structType : nullptr;
    const float left = x.view.x + 12 + indent + kIndent;
    const float width = x.view.w - 24 - indent - kIndent;
    for (size_t i = 0; i < count; ++i) {
        c.PushId("elem");
        c.PushId(static_cast<uint64_t>(i));
        const void* elem = ops.at(const_cast<void*>(value), i);
        const Rect row{left, x.y, width, kElemRow};
        tops.push_back(row.y);
        x.y += kElemRow;

        // The grip: drag it up / down to move the element.
        const Rect grip{row.x, row.y, 16, row.h};
        if (enabled) {
            const auto g = c.ButtonBehavior(c.MakeId("grip"), grip);
            if (g.hovered || g.held) c.SetCursor(ui::Cursor::SizeNS);
            if (g.held) {
                x.st.arrayDrag = arrayId;
                x.st.arrayDragFrom = i;
            }
        }
        w::DrawIcon(c, grip, w::Icon::Grip, enabled ? theme::TextFaint : theme::TextFaint.Alpha(0.5f));
        const Rect indexR{row.x + 18, row.y, 26, row.h};
        p.Text(indexR, std::to_wstring(i), Text(11, theme::TextFaint));
        const Rect removeR{row.Right() - 24, row.y + 3, 24, row.h - 6};
        if (w::IconButton(c, "remove", removeR, w::Icon::Close, enabled)) {
            json::Value after = arr;
            after.Items().erase(after.Items().begin() + static_cast<std::ptrdiff_t>(i));
            put(std::move(after));
        }

        // The element's value; its edits become the whole array's.
        const Wrap outer = x.wrap;
        const size_t block = x.block;
        const bool withOthers = !others.empty();
        x.wrap = [outer, arr, i, elemStruct, path, block, withOthers](Edit inner) -> std::optional<Edit> {
            json::Value after = arr;
            if (i >= after.Items().size()) return std::nullopt;
            json::Value elemJson = after.At(i);
            if (inner.path.empty()) {
                elemJson = inner.after;
            } else if (elemStruct) {
                reflect::Object tmp(*elemStruct);
                Unknowns uk;
                reflect::Load(*elemStruct, tmp.Get(), elemJson, &uk);
                const reflect::PathTarget pt = reflect::Resolve(*elemStruct, tmp.Get(), &uk, inner.path);
                if (!pt.field) return std::nullopt;
                reflect::LoadField(*pt.field, pt.owner, inner.after, pt.unknowns, nullptr);
                elemJson = reflect::Save(*elemStruct, tmp.Get(), &uk);
            }
            after.At(i) = std::move(elemJson);
            Edit e;
            e.block = block;
            e.path = path;
            e.before = arr;
            e.after = std::move(after);
            e.label = std::move(inner.label);
            e.merge = inner.merge;
            if (e.before == e.after && !withOthers) return std::nullopt;
            if (outer) return outer(std::move(e));
            return e;
        };
        if (elemStruct) {
            // A struct: its fields under the element's row.
            FieldPath inner;
            DrawStruct(x, *elemStruct, elem, {}, nullptr, inner, indent + 2 * kIndent);
        } else {
            Spot s{f, ops.element, elem};
            s.enabled = enabled;
            s.r = {row.x + 46, row.y + 3, std::max(30.f, row.w - 46 - 28), row.h - 6};
            s.scrub = indexR;  // the index: drag it to change a number
            const std::string label = std::format("「{}」を変更", f.Label());
            const json::Value before = arr.At(i);
            s.editFor = [label, before](json::Value after) {
                Edit e;
                e.before = before;
                e.after = std::move(after);
                e.label = label;
                return e;
            };
            DrawValue(x, s);
        }
        x.wrap = outer;
        c.PopId();
        c.PopId();
    }

    // An element being dragged: a line where it would go; let go = moved there.
    if (x.st.arrayDrag == arrayId && count > 1) {
        size_t slot = 0;  // before element `slot` (count = at the end)
        while (slot < tops.size() && in.mouseY > tops[slot] + kElemRow * 0.5f) ++slot;
        const size_t from = std::min(x.st.arrayDragFrom, count - 1);
        if (in.down[0]) {
            const float ly = slot < tops.size() ? tops[slot] : tops.back() + kElemRow;
            if (slot != from && slot != from + 1) p.FillRect({left, ly - 1, width, 2}, theme::Accent);
            c.RequestRedraw();
        } else {
            x.st.arrayDrag = 0;
            if (slot != from && slot != from + 1) {
                json::Value after = arr;
                auto& items = after.Items();
                json::Value moved = items[from];
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(from));
                const size_t to = slot > from ? slot - 1 : slot;
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
                put(std::move(after));
            }
        }
    } else if (x.st.arrayDrag == arrayId && !in.down[0]) {
        x.st.arrayDrag = 0;
    }
}

void DrawField(Pass& x, const TypeDesc& ownerType, const FieldDesc& f, size_t index, const void* owner, Others others, const Unknowns* u,
               FieldPath& prefix, float indent) {
    if (!f.edit && !f.readOnly) return;  // saved, not shown
    ui::Context& c = x.c;
    auto& p = c.P();
    const bool enabled = f.edit && !f.readOnly;
    const void* value = f.Get(owner);
    std::vector<const void*> otherValues;  // the same field in the objects edited together
    for (const void* o : others) otherValues.push_back(f.Get(o));
    FieldPath path = prefix;
    path.push_back(index);

    const Rect row{x.view.x, x.y, x.view.w, kRow};
    const Rect labelR{row.x + 12 + indent, row.y, x.labelW - indent, row.h};
    Rect valueR{row.x + 12 + x.labelW + 8, row.y + 3, std::max(40.f, row.w - x.labelW - 34), row.h - 6};
    x.y += kRow;
    // Vectors and colours get their own line when the panel is too narrow for their boxes.
    const size_t comps = Components(f.value.type);
    if (comps > 0 && valueR.w / static_cast<float>(comps) < 64.f + kAxisW) {
        valueR = {row.x + 12 + indent + kIndent, x.y + 1, row.w - 24 - indent - kIndent, kRow - 6};
        x.y += kRow;
    }

    p.Text(labelR, Utf8ToWide(TrU8(f.Label())), Text(12, enabled ? theme::TextDim : theme::TextFaint, f.value.type == FieldType::Struct));
    if (f.tooltip && c.IsHovered(labelR)) x.tooltip = Utf8ToWide(TrU8(f.tooltip));

    c.PushId("field");
    for (const size_t i : path) c.PushId(static_cast<uint64_t>(i));

    const json::Value before = reflect::SaveField(f, owner, u);
    const std::string label = std::format("「{}」を変更", f.Label());
    const size_t block = x.block;
    auto editFor = [block, path, before, label](json::Value after) {
        Edit e;
        e.block = block;
        e.path = path;
        e.before = before;
        e.after = std::move(after);
        e.label = label;
        return e;
    };

    switch (f.value.type) {
        case FieldType::Struct: {
            FieldPath inner = path;
            DrawStruct(x, *f.value.structType, value, otherValues, u ? u->Child(f.name) : nullptr, inner, indent + kIndent);
            break;
        }
        case FieldType::Array:
            DrawArray(x, ownerType, f, owner, others, u, path, indent, valueR, enabled, editFor);
            break;
        default: {
            Spot s{f, f.value, value, otherValues, u, f.name};
            s.enabled = enabled;
            s.r = valueR;
            // A single number: its label is the handle to drag it.
            if (f.value.type == FieldType::Int || f.value.type == FieldType::Float) s.scrub = labelR;
            s.editFor = editFor;
            DrawValue(x, s);
            break;
        }
    }
    for (size_t i = 0; i < path.size(); ++i) c.PopId();
    c.PopId();
}

void DrawStruct(Pass& x, const TypeDesc& t, const void* obj, Others others, const Unknowns* u, FieldPath& prefix, float indent) {
    for (size_t i = 0; i < t.fields.size(); ++i) DrawField(x, t, t.fields[i], i, obj, others, u, prefix, indent);
}

void DrawHeader(Pass& x, const Block& b, bool folded) {
    ui::Context& c = x.c;
    const Rect row{x.view.x + 6, x.y, x.view.w - 12, 28};
    x.y += 32;
    const bool hasMenu = !b.menu.empty();
    const Rect menuR{row.Right() - 28, row.y + 3, 24, row.h - 6};
    bool open = !folded;
    if (w::CollapsingHeader(c, "header", row, b.title, open, hasMenu ? 32.f : 0.f)) {
        if (open) {
            x.st.folded.erase(b.title);
        } else {
            x.st.folded.insert(b.title);
        }
    }
    if (hasMenu && w::IconButton(c, "menu", menuR, w::Icon::DotsVertical)) {
        x.st.menuActions = b.menu;
        x.st.menu.OpenAt(menuR.x, menuR.Bottom() + 2);
    }
}

}  // namespace

std::optional<Edit> Draw(ui::Context& c, State& st, const Rect& area, std::span<const Block> blocks, std::wstring_view addLabel,
                         const std::function<std::vector<w::Action>()>& addMenu, const Refs* refs) {
    std::optional<Edit> result = std::move(st.pending);
    st.pending.reset();

    const float off = w::BeginScroll(c, "inspector", area, st.contentHeight);
    Pass x{c, st, area};
    x.refs = refs;
    x.y = area.y + 6 - off;
    x.labelW = std::clamp(area.w * 0.38f, 96.f, 220.f);
    // While a drop-down or a menu is open, the fields under it only show.
    const bool inputWas = c.InputEnabled();
    if (st.popup.open || st.menu.open) c.SetInputEnabled(false);
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const Block& b = blocks[bi];
        x.block = bi;
        // Ids by title (the list may change - a component added or removed - without mixing up fields).
        c.PushId(b.title.empty() ? std::string("block") : WideToUtf8(b.title));
        const bool folded = !b.title.empty() && st.folded.contains(b.title);
        if (!b.title.empty()) DrawHeader(x, b, folded);
        if (!folded) {
            if (!b.note.empty()) {
                ui::TextStyle ns = Text(12, theme::TextFaint);
                ns.wrap = true;
                ns.v = ui::VAlign::Top;
                // As tall as the note is (a long one wraps to three lines and more).
                const float h = std::ceil(c.P().TextHeight(b.note, ns.size, x.view.w - 28));
                c.P().Text({x.view.x + 14, x.y, x.view.w - 28, h + 2}, b.note, ns);
                x.y += h + 8;
            }
            if (b.modes.size() == 2) {
                // Two ways of showing the block: a switch above its fields (left = the first way).
                bool second = b.mode == 1;
                if (w::Switch(c, "mode", x.view.x + 12, x.y + 4, second, b.modes[0], b.modes[1]) && b.setMode) b.setMode(second ? 1 : 0);
                x.y += 32;
            } else if (!b.modes.empty()) {
                // More ways: one strip of choices.
                std::vector<std::wstring_view> labels(b.modes.begin(), b.modes.end());
                const float wdt = std::min(84.f * static_cast<float>(labels.size()) + 4.f, x.view.w - 24);
                const int picked = w::Segmented(c, "mode", {x.view.x + 12, x.y + 2, wdt, 26}, labels, b.mode);
                if (picked >= 0 && picked != b.mode && b.setMode) b.setMode(picked);
                x.y += 34;
            }
            if (b.type && b.object) {
                FieldPath prefix;
                const bool inputBefore = c.InputEnabled();
                if (b.readOnly) c.SetInputEnabled(false);  // shown only: no field takes a click or a key
                DrawStruct(x, *b.type, b.object, b.others, b.unknowns, prefix, 0);
                c.SetInputEnabled(inputBefore);
            }
            x.y += 8;
        }
        c.PopId();
    }
    if (!addLabel.empty()) {
        x.y += 4;
        const float wdt = std::min(260.f, area.w - 24);
        const Rect btn{area.x + (area.w - wdt) * 0.5f, x.y, wdt, 32};
        if (w::Button(c, "inspector.add", btn, addLabel) && addMenu) {
            st.menuActions = addMenu();
            st.menu.OpenAt(btn.x, btn.Bottom() + 2);
        }
        x.y += 44;
    }
    c.SetInputEnabled(inputWas);
    st.contentHeight = x.y + off - area.y + 6;
    w::EndScroll(c, "inspector", area, st.contentHeight);
    if (!st.popup.open) st.popupId = 0;

    if (!x.tooltip.empty()) {
        w::Tooltip(c, x.tooltip);
    }
    w::ActionMenu(c, st.menu, st.menuActions);
    if (x.edit) result = std::move(x.edit);
    return result;
}

}  // namespace rm::editor::inspector
