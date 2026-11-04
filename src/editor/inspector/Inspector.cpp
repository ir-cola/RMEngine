#include "editor/inspector/Inspector.h"

#include "base/Utf.h"
#include "editor/widgets/Theme.h"

#include <algorithm>
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

ui::TextStyle Text(float size, ui::Color color, bool bold = false) {
    ui::TextStyle ts;
    ts.size = size;
    ts.color = color;
    ts.bold = bold;
    return ts;
}

struct Pass {
    ui::Context& c;
    State& st;
    Rect view;
    float y = 0;
    float labelW = 0;
    std::optional<Edit> edit;
    std::wstring tooltip;
};

// A text box that reports its text only when the user is done: Enter, or leaving the field.
// Esc puts the value back. While it is not being edited it simply shows `shown`.
std::optional<std::string> CommitField(Pass& x, std::string_view id, const Rect& r, const std::string& shown, bool enabled) {
    ui::Context& c = x.c;
    const ui::Id uid = c.MakeId(id);
    const bool wasEditing = x.st.editing == uid;
    std::string& buf = x.st.text[uid];
    if (c.Focused() != uid && !wasEditing) buf = shown;
    const bool focusedBefore = c.Focused() == uid;
    const bool enter = focusedBefore && c.KeyPressed(ui::Key::Enter);
    const bool escape = focusedBefore && c.KeyPressed(ui::Key::Escape);
    w::TextField(c, id, r, buf, {}, enabled);
    const bool focusedNow = c.Focused() == uid;
    if (focusedNow) x.st.editing = uid;
    if (escape) {
        c.ClearFocus();
        buf = shown;
        x.st.editing = 0;
        return std::nullopt;
    }
    if (enter || (wasEditing && !focusedNow)) {
        if (enter) c.ClearFocus();
        if (x.st.editing == uid) x.st.editing = 0;
        if (buf == shown) return std::nullopt;
        return buf;
    }
    return std::nullopt;
}

std::string Summary(const FieldDesc& f, const reflect::ValueType& vt, const void* p) {
    switch (vt.type) {
        case FieldType::Bool: return *static_cast<const bool*>(p) ? "はい" : "いいえ";
        case FieldType::Int: return std::to_string(*static_cast<const int*>(p));
        case FieldType::Float: return reflect::FormatNumber(f, *static_cast<const float*>(p));
        case FieldType::String: return "\"" + *static_cast<const std::string*>(p) + "\"";
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Color: {
            const size_t n = vt.type == FieldType::Vec2 ? 2 : vt.type == FieldType::Vec3 ? 3 : 4;
            std::string s = "(";
            for (size_t i = 0; i < n; ++i) s += (i ? ", " : "") + reflect::FormatNumber(f, static_cast<const float*>(p)[i]);
            return s + ")";
        }
        case FieldType::Enum: {
            const reflect::EnumItem* it = reflect::FindItem(*vt.enumType, vt.enumType->get(p));
            return it ? it->label : std::to_string(vt.enumType->get(p));
        }
        case FieldType::Struct: return "{…}";
        case FieldType::Array: return "[…]";
    }
    return {};
}

void DrawStruct(Pass& x, const TypeDesc& t, const void* obj, const Unknowns* u, FieldPath& prefix, float indent);

void DrawField(Pass& x, const FieldDesc& f, size_t index, const void* owner, const Unknowns* u, FieldPath& prefix, float indent) {
    if (!f.edit && !f.readOnly) return;  // saved, not shown
    ui::Context& c = x.c;
    auto& p = c.P();
    const bool enabled = f.edit && !f.readOnly;
    const void* value = f.Get(owner);
    FieldPath path = prefix;
    path.push_back(index);

    const Rect row{x.view.x, x.y, x.view.w, kRow};
    const Rect labelR{row.x + 12 + indent, row.y, x.labelW - indent, row.h};
    Rect valueR{row.x + 12 + x.labelW + 8, row.y + 3, std::max(40.f, row.w - x.labelW - 34), row.h - 6};
    x.y += kRow;
    // Vectors and colours get their own line when the panel is too narrow for their boxes.
    const size_t comps = f.value.type == FieldType::Vec2 ? 2 : f.value.type == FieldType::Vec3 ? 3 : f.value.type == FieldType::Color ? 4 : 0;
    if (comps > 0 && valueR.w / static_cast<float>(comps) < 64.f) {
        valueR = {row.x + 12 + indent + kIndent, x.y + 1, row.w - 24 - indent - kIndent, kRow - 6};
        x.y += kRow;
    }

    p.Text(labelR, Utf8ToWide(f.Label()), Text(12, enabled ? theme::TextDim : theme::TextFaint, f.value.type == FieldType::Struct));
    if (f.tooltip && c.IsHovered(labelR)) x.tooltip = Utf8ToWide(f.tooltip);

    c.PushId("field");
    for (const size_t i : path) c.PushId(static_cast<uint64_t>(i));

    auto makeEdit = [&](json::Value after) {
        Edit e;
        e.path = path;
        e.before = reflect::SaveField(f, owner, u);
        e.after = std::move(after);
        e.label = std::format("「{}」を変更", f.Label());
        if (e.before != e.after) x.edit = std::move(e);
    };

    switch (f.value.type) {
        case FieldType::Bool: {
            bool v = *static_cast<const bool*>(value);
            if (w::Checkbox(c, "v", {valueR.x, valueR.y, 28, valueR.h}, v, L"") && enabled) makeEdit(json::Value(v));
            break;
        }
        case FieldType::Int:
        case FieldType::Float: {
            const bool isInt = f.value.type == FieldType::Int;
            const double cur = isInt ? *static_cast<const int*>(value) : *static_cast<const float*>(value);
            if (auto typed = CommitField(x, "v", valueR, reflect::FormatNumber(f, cur), enabled)) {
                if (auto n = reflect::NumberFromInput(f, *typed, cur)) {
                    makeEdit(isInt ? json::Value(static_cast<int64_t>(*n)) : json::Value(static_cast<double>(static_cast<float>(*n))));
                }
            }
            break;
        }
        case FieldType::String: {
            if (auto typed = CommitField(x, "v", valueR, *static_cast<const std::string*>(value), enabled)) makeEdit(json::Value(*typed));
            break;
        }
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Color: {
            const size_t n = comps;
            const float* values = static_cast<const float*>(value);
            Rect area = valueR;
            if (f.value.type == FieldType::Color) {
                const Rect swatch = ui::TakeLeft(area, 26);
                const Color col{values[0], values[1], values[2], values[3]};
                p.FillRoundRect(swatch.Inset(0, 1, 4, 1), 4, col);
                p.StrokeRoundRect(swatch.Inset(0, 1, 4, 1), 4, theme::Border);
            }
            const float each = (area.w - 4.f * static_cast<float>(n - 1)) / static_cast<float>(n);
            static const char* kIds[] = {"x", "y", "z", "w"};
            for (size_t i = 0; i < n; ++i) {
                const Rect r{area.x + (each + 4) * static_cast<float>(i), area.y, each, area.h};
                if (auto typed = CommitField(x, kIds[i], r, reflect::FormatNumber(f, values[i]), enabled)) {
                    if (auto v = reflect::NumberFromInput(f, *typed, values[i])) {
                        json::Value arr = json::Value::MakeArray();
                        for (size_t k = 0; k < n; ++k) arr.Push(static_cast<double>(k == i ? static_cast<float>(*v) : values[k]));
                        makeEdit(std::move(arr));
                    }
                }
            }
            break;
        }
        case FieldType::Enum: {
            const reflect::EnumDesc& e = *f.value.enumType;
            std::wstring shown;
            if (const std::string* unknown = u ? u->Enum(f.name) : nullptr) {
                shown = Utf8ToWide(*unknown) + L" (不明)";
            } else if (const reflect::EnumItem* it = reflect::FindItem(e, e.get(value))) {
                shown = Utf8ToWide(it->label);
            } else {
                shown = std::to_wstring(e.get(value));
            }
            const auto b = enabled ? c.ButtonBehavior(c.MakeId("v"), valueR) : ui::Context::ButtonResult{};
            p.FillRoundRect(valueR, theme::Radius, b.hovered ? theme::Hover : theme::Field);
            p.StrokeRoundRect(valueR, theme::Radius, theme::Border);
            p.Text(valueR.Inset(8, 0, 22, 0), shown, Text(theme::FontSize, enabled ? theme::Text : theme::TextFaint));
            p.Text({valueR.Right() - 20, valueR.y, 14, valueR.h}, L"▾", Text(11, theme::TextDim));
            if (b.clicked) {
                x.st.popup.OpenAt(valueR.x, valueR.Bottom() + 2);
                x.st.popupPath = path;
            }
            if (x.st.popup.open && x.st.popupPath == path) {
                // The list is drawn at the end of the frame (not cut off by the panel). The choice is
                // handed back through State and returned by the next Draw.
                const json::Value before = reflect::SaveField(f, owner, u);
                const float width = std::max(valueR.w, 180.f);
                const std::string label = std::format("「{}」を変更", f.Label());
                State* st = &x.st;
                c.AddOverlay([&c, st, &e, path, before, width, label] {
                    std::vector<w::MenuItem> items;
                    for (const auto& it : e.items) items.push_back({Utf8ToWide(it.label)});
                    const bool wasEnabled = c.InputEnabled();
                    c.SetInputEnabled(true);
                    const int picked = w::MenuPopup(c, st->popup, items, width);
                    c.SetInputEnabled(wasEnabled);
                    if (picked >= 0) {
                        Edit ed;
                        ed.path = path;
                        ed.before = before;
                        ed.after = json::Value(e.items[static_cast<size_t>(picked)].name);
                        ed.label = label;
                        if (ed.before != ed.after) st->pending = std::move(ed);
                    }
                });
            }
            break;
        }
        case FieldType::Struct: {
            FieldPath inner = path;
            DrawStruct(x, *f.value.structType, value, u ? u->Child(f.name) : nullptr, inner, indent + kIndent);
            break;
        }
        case FieldType::Array: {
            const reflect::ArrayOps& ops = *f.array;
            const size_t count = ops.size(value);
            p.Text(valueR, std::format(L"{} 個  (追加・削除・並べ替えはこれから)", count), Text(12, theme::TextFaint));
            const size_t shownCount = std::min<size_t>(count, 20);
            for (size_t i = 0; i < shownCount; ++i) {
                const Rect r{x.view.x + 12 + indent + kIndent, x.y, x.view.w - 24 - indent - kIndent, 24};
                const void* elem = ops.at(const_cast<void*>(value), i);
                p.Text({r.x, r.y, 50, r.h}, std::format(L"[{}]", i), Text(12, theme::TextFaint));
                p.Text({r.x + 50, r.y, r.w - 50, r.h}, Utf8ToWide(Summary(f, ops.element, elem)), Text(12, theme::TextDim));
                x.y += 24;
            }
            if (count > shownCount) {
                p.Text({x.view.x + 12 + indent + kIndent, x.y, 300, 22}, std::format(L"… ほか {} 個", count - shownCount),
                       Text(12, theme::TextFaint));
                x.y += 22;
            }
            break;
        }
    }
    for (size_t i = 0; i < path.size(); ++i) c.PopId();
    c.PopId();
}

void DrawStruct(Pass& x, const TypeDesc& t, const void* obj, const Unknowns* u, FieldPath& prefix, float indent) {
    for (size_t i = 0; i < t.fields.size(); ++i) DrawField(x, t.fields[i], i, obj, u, prefix, indent);
}

}  // namespace

std::optional<Edit> Draw(ui::Context& c, State& st, const Rect& area, const TypeDesc& type, const void* object, const Unknowns* unknowns) {
    std::optional<Edit> result = std::move(st.pending);
    st.pending.reset();

    const float off = w::BeginScroll(c, "inspector", area, st.contentHeight);
    Pass x{c, st, area};
    x.y = area.y + 6 - off;
    x.labelW = std::clamp(area.w * 0.38f, 96.f, 220.f);
    // While a drop-down is open, the fields under it only show.
    const bool inputWas = c.InputEnabled();
    if (st.popup.open) c.SetInputEnabled(false);
    FieldPath prefix;
    DrawStruct(x, type, object, unknowns, prefix, 0);
    c.SetInputEnabled(inputWas);
    st.contentHeight = x.y + off - area.y + 6;
    w::EndScroll(c, "inspector", area, st.contentHeight);
    if (!st.popup.open) st.popupPath.clear();

    if (!x.tooltip.empty()) {
        const std::wstring tip = x.tooltip;
        c.AddOverlay([&c, tip] {
            auto& p = c.P();
            const float wdt = std::min(420.f, p.TextWidth(tip, 12) + 20);
            const Rect r{c.In().mouseX + 14, c.In().mouseY + 18, wdt, 28};
            p.FillRoundRect({r.x + 1, r.y + 2, r.w, r.h}, 5, theme::Shadow);
            p.FillRoundRect(r, 5, theme::PanelHeader);
            p.StrokeRoundRect(r, 5, theme::Border);
            p.Text(r.Inset(10, 0, 10, 0), tip, Text(12, theme::Text));
        });
    }
    if (x.edit) result = std::move(x.edit);
    return result;
}

}  // namespace rm::editor::inspector
