#include "editor/lang/Lang.h"
#include "editor/inspector/Inspector.h"

#include "base/Utf.h"
#include "core/asset/AssetIndex.h"
#include "core/reflect/Object.h"
#include "editor/tree/TreeModel.h"
#include "editor/widgets/IconAtlas.h"
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
constexpr float kAxisW = 14.f;   // ベクトルの欄の前の X / Y / Z
constexpr float kElemRow = 28.f;

ui::TextStyle Text(float size, ui::Color color, bool bold = false) {
    ui::TextStyle ts;
    ts.size = size;
    ts.color = color;
    ts.bold = bold;
    return ts;
}

// 配列の要素の中で行った編集を、配列フィールドの編集に変える (または何もしない)。
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
    Wrap wrap;  // 配列の要素の中 (null: その編集はフィールド自身のもの)
    const Refs* refs = nullptr;
    // フィールドのブロックのメニュー (Block::fieldMenu) と、今右クリックで開いてよいか。
    const std::function<std::vector<w::Action>(const reflect::FieldPath&, std::string_view)>* fieldMenu = nullptr;
    bool menusOk = false;
    // 文字のフィールドの選択肢 (Block::choices)。
    const std::function<std::optional<std::vector<Choice>>(const reflect::FieldPath&)>* choices = nullptr;
    // 配列の要素の中を描いている間: 選択肢に渡す道の頭 (配列のフィールドの道 + 要素の番号。外: 空)。
    reflect::FieldPath choicePrefix;

    void Put(Edit e) {
        if (!wrap) {
            edit = std::move(e);
        } else if (auto outer = wrap(std::move(e))) {
            edit = std::move(*outer);
        }
    }
};

// 入力・ドラッグの 1 回ごとに独自のマージキーを持つ (プログラム内で一意: 2 つのインスペクターは同じものを共有しない)。
uint64_t NewSession() {
    static uint64_t next = 0;
    return ++next;
}

// 打っている途中のテキストをそのまま使う入力欄: 変わるたびに返し、Enter か欄を出たときに、
// 見えている値がそれと違えばもう一度返す。Esc は欄に入る前の値を返す。編集中でなければ
// `shown` をそのまま見せる。`merge` = この入力のキー (Undo は 1 つの項目)。
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

// `handle` を左右にドラッグして数を変える。終わりのないスライダーのようなもの (カーソルは隠れ、
// マウスは画面の端を越えて動き続ける)。速さはドラッグを始めたときに決める: フィールドの
// ピクセルあたりの Step か、大きな数ならそれより大きく (Unity のルール)。Shift で 4 倍、Alt で 1/4。
// 変わっている間は新しい値を返す。`merge` = そのドラッグのキー (Undo は 1 つの項目)。
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
        c.ClearFocus();  // 入力中の欄を先に終わらせる
    }
    c.LockMouse();
    merge = x.st.scrubSession;
    if (in.mouseDX == 0) return std::nullopt;
    x.st.scrubMoved += in.mouseDX * (in.shift ? 4.0 : in.alt ? 0.25 : 1.0);
    const double v = reflect::SanitizeNumber(f, x.st.scrubStart + x.st.scrubMoved * x.st.scrubSpeed, current);
    return v != current ? std::optional<double>(v) : std::nullopt;
}

// いっしょに編集している全オブジェクトで値が同じでないフィールドに出す。
constexpr char kMixed[] = "\xE2\x80\x94";  // —

using Others = std::span<const void* const>;

size_t Components(FieldType t) { return t == FieldType::Vec2 ? 2 : t == FieldType::Vec3 ? 3 : t == FieldType::Color ? 4 : 0; }

// 1 つの値: 数、ベクトル、文字、チェックボックス、リスト - フィールドか配列の要素の。
struct Spot {
    const FieldDesc& f;          // そのフィールド (数のルール: Min / Max / Int / Step)
    reflect::ValueType vt;       // その型 (フィールドの型か、配列の要素の型)
    const void* value = nullptr;
    std::vector<const void*> others;  // いっしょに編集しているオブジェクトの同じ値
    const Unknowns* u = nullptr;      // 持ち主の unknowns (enum の名前を残す) - 要素では null
    const char* enumKey = nullptr;    // enum の未知の名前を残すフィールド名
    bool enabled = true;
    Rect r;                       // 値を置く場所
    Rect scrub;                   // ドラッグすると 1 つの数を変えるラベル (w = 0: なし)
    // 新しい値の Edit (path、before、label - ブロックのもの)。`merge` / `component` を足す。
    std::function<Edit(json::Value)> editFor;
    // 文字: 選べる値 (Block::choices。nullopt: 文字の欄)。
    std::optional<std::vector<Choice>> choices;
};

// 参照が持つ ID を文字にしたもの ("" = なし)。
std::string RefValue(const FieldDesc& f, FieldType t, const void* v) {
    if (t == FieldType::AssetRef) return reflect::ReadAssetId(f, v);
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

// これより項目の多い enum は、メニューではなく検索欄つき (スクロールする) のリストを開く。
constexpr size_t kShortEnum = 16;

// 参照: 箱に名前 (クリックでリストを開く)、× で消す、そして 1 つ落とせる場所。
template <class Put>
void DrawRef(Pass& x, const Spot& s, const Put& put) {
    ui::Context& c = x.c;
    const bool isAsset = s.vt.type == FieldType::AssetRef;
    const std::string kinds = isAsset && s.f.assetKinds ? s.f.assetKinds : "";
    const std::string cur = RefValue(s.f, s.vt.type, s.value);
    const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return RefValue(s.f, s.vt.type, o) != cur; });
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

    // アセットパネル / 階層からドロップした。
    const Refs* refs = x.refs;
    const auto accepts = [&](const w::DragThing& t) {
        if (t.values.empty()) return false;
        if (!isAsset) return t.kind == "item";
        if (t.kind != "asset" || !refs->asset) return false;
        const auto a = refs->asset(t.values[0]);
        const auto k = a ? asset::KindFromKey(a->kind) : std::nullopt;
        return k && asset::KindMatches(*k, kinds, a->file);
    };
    if (const w::DragThing* t = w::DropTarget(c, s.r, accepts)) put(RefJson(s.vt.type, t->values[0]), 0, -1);

    if (b.clicked) {
        x.st.popup.OpenAt(face.x, face.Bottom() + 2);
        x.st.popupId = popupId;
        x.st.pickSearch.clear();
    }
    if (!x.st.popup.open || x.st.popupId != popupId) return;
    // リスト (フレームの終わりに全部の上に): (なし) と、頼まれた種類のあるもの。
    std::vector<RefChoice> choices;
    if (isAsset && refs->assets) {
        for (RefChoice& r : refs->assets()) {
            const auto k = asset::KindFromKey(r.kind);
            if (k && asset::KindMatches(*k, kinds, r.file)) choices.push_back(std::move(r));
        }
    } else if (!isAsset && refs->items) {
        choices = refs->items();
    }
    State* st = &x.st;
    const bool withOthers = !s.others.empty();
    const float width = std::max(s.r.w, 300.f);
    const FieldType type = s.vt.type;
    // アセット: リストにサムネイル (アセットパネルのものと同じ)。
    auto thumbnail = isAsset ? refs->thumbnail : nullptr;
    c.AddOverlay([&c, st, choices = std::move(choices), width, withOthers, type, editFor = s.editFor, wrap = x.wrap, thumbnail] {
        std::vector<const RefChoice*> picks;
        std::vector<w::PickRow> rows;
        if (st->pickSearch.empty()) {  // 何も打っていない間は先頭に "(なし)"
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

// 選択肢のある文字: 箱に今の値の名前 (クリックでリスト)。一覧に無い値はそのまま見せて残す。
void DrawChoices(Pass& x, const Spot& s, const std::string& cur, bool differ) {
    ui::Context& c = x.c;
    const std::vector<Choice>& list = *s.choices;
    const auto found = std::find_if(list.begin(), list.end(), [&](const Choice& ch) { return ch.value == cur; });
    const std::wstring shown = differ ? Utf8ToWide(kMixed) : found != list.end() ? found->label : Utf8ToWide(cur) + L" " + Tr(L"(見つかりません)");
    const ui::Id popupId = c.MakeId("v");
    if (w::ComboBox(c, "v", s.r, shown, s.enabled).clicked) {
        x.st.popup.OpenAt(s.r.x, s.r.Bottom() + 2);
        x.st.popupId = popupId;
        x.st.pickSearch.clear();
    }
    if (!x.st.popup.open || x.st.popupId != popupId) return;
    State* st = &x.st;
    const bool longList = list.size() > kShortEnum;
    const float width = std::max(s.r.w, longList ? 260.f : 180.f);
    const bool withOthers = !s.others.empty();
    c.AddOverlay([&c, st, list, width, withOthers, longList, editFor = s.editFor, wrap = x.wrap] {
        const bool wasEnabled = c.InputEnabled();
        c.SetInputEnabled(true);
        int picked = -1;
        if (longList) {
            std::vector<size_t> which;
            std::vector<w::PickRow> rows;
            for (size_t i = 0; i < list.size(); ++i) {
                if (!st->pickSearch.empty() && !tree::NameMatches(WideToUtf8(list[i].label), st->pickSearch)) continue;
                rows.push_back({list[i].label});
                which.push_back(i);
            }
            const int row = w::PickerPopup(c, st->popup, "choicepick", st->pickSearch, rows, width);
            if (row >= 0) picked = static_cast<int>(which[static_cast<size_t>(row)]);
        } else {
            std::vector<w::MenuItem> items;
            for (const Choice& ch : list) items.push_back({ch.label});
            picked = w::MenuPopup(c, st->popup, items, width);
        }
        c.SetInputEnabled(wasEnabled);
        if (picked < 0) return;
        Edit ed = editFor(json::Value(list[static_cast<size_t>(picked)].value));
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
        // 他のものがあるときは、このオブジェクトがすでにその値でも変更とみなす (他は違うかもしれない)。
        if (e.before != e.after || !s.others.empty()) x.Put(std::move(e));
    };
    uint64_t merge = 0;

    switch (s.vt.type) {
        case FieldType::Bool: {
            bool v = *static_cast<const bool*>(s.value);
            // いっしょに編集している全オブジェクトで同じでない: ダッシュ (数の "—" と同じ)。
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
            const std::string cur = reflect::ReadString(s.f, s.value);
            const bool differ = std::any_of(s.others.begin(), s.others.end(), [&](const void* o) { return reflect::ReadString(s.f, o) != cur; });
            if (s.choices) {
                DrawChoices(x, s, cur, differ);
                break;
            }
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
                w::DrawSwatch(c, swatch.Inset(0, 1, 4, 1), Color{values[0], values[1], values[2], values[3]});
            }
            const float each = (area.w - 4.f * static_cast<float>(n - 1)) / static_cast<float>(n);
            static const char* kIds[] = {"x", "y", "z", "w"};
            static const wchar_t* kAxes[] = {L"X", L"Y", L"Z", L"W"};
            static const wchar_t* kChannels[] = {L"R", L"G", L"B", L"A"};
            for (size_t i = 0; i < n; ++i) {
                const Rect cell{area.x + (each + 4) * static_cast<float>(i), area.y, each, area.h};
                // 各欄の前に文字: どれかを示し、ドラッグのつまみにもなる。
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
                x.st.pickSearch.clear();
            }
            if (x.st.popup.open && x.st.popupId == popupId) {
                // リストはフレームの終わりに描く (パネルで切られない)。選んだものは State を通して
                // 渡し、次の Draw が返す。長いリスト (入力のキーなど) は、検索できるスクロールつきにする。
                const bool longList = e.items.size() > kShortEnum;
                const float width = std::max(s.r.w, longList ? 260.f : 180.f);
                State* st = &x.st;
                const bool withOthers = !s.others.empty();
                c.AddOverlay([&c, st, &e, width, withOthers, longList, editFor = s.editFor, wrap = x.wrap] {
                    const bool wasEnabled = c.InputEnabled();
                    c.SetInputEnabled(true);
                    int picked = -1;
                    if (longList) {
                        std::vector<size_t> which;
                        std::vector<w::PickRow> rows;
                        for (size_t i = 0; i < e.items.size(); ++i) {
                            const std::string label = TrU8(e.items[i].label);
                            if (!st->pickSearch.empty() && !tree::NameMatches(label, st->pickSearch) && !tree::NameMatches(e.items[i].name, st->pickSearch)) continue;
                            rows.push_back({Utf8ToWide(label)});
                            which.push_back(i);
                        }
                        const int row = w::PickerPopup(c, st->popup, "enumpick", st->pickSearch, rows, width);
                        if (row >= 0) picked = static_cast<int>(which[static_cast<size_t>(row)]);
                    } else {
                        std::vector<w::MenuItem> items;
                        for (const auto& it : e.items) items.push_back({Utf8ToWide(TrU8(it.label))});
                        picked = w::MenuPopup(c, st->popup, items, width);
                    }
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
        case FieldType::Array: break;  // (単独の値ではない: DrawField)
    }
}

void DrawStruct(Pass& x, const TypeDesc& t, const void* obj, Others others, const Unknowns* u, FieldPath& prefix, float indent);

// 配列: 個数と ＋、そして各要素のつまみ・番号・値・×。変更は毎回、保存するときの形で
// 配列全体になる。
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

    // フィールドの行に "N 個" と ＋。
    const Rect plusR{valueR.Right() - 26, valueR.y, 26, valueR.h};
    p.Text({valueR.x, valueR.y, valueR.w - 30, valueR.h}, differ ? Utf8ToWide(kMixed) : TrF(L"{} 個", count), Text(12, theme::TextDim));
    if (w::IconButton(c, "add", plusR, w::Icon::Plus, enabled && !differ)) {
        json::Value after = arr;
        if (count > 0) {
            after.Push(arr.At(count - 1));  // 最後のもののコピー (次に欲しいのはたいていこれ)
        } else {
            // 既定値の要素 1 つ: 持ち主のコピーで、配列を 1 つ長くしたもの。
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
    std::vector<float> tops;  // 各要素の行。ドラッグしてきたものを落とすため
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

        // つまみ: 上下にドラッグして要素を動かす。
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

        // 要素の値。その編集は配列全体の編集になる。
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
            // 構造体: 要素の行の下にそのフィールド。選択肢には「配列の道 + 要素の番号 + 要素の中の道」を渡す。
            FieldPath inner;
            const FieldPath outerPrefix = x.choicePrefix;
            x.choicePrefix.insert(x.choicePrefix.end(), path.begin(), path.end());
            x.choicePrefix.push_back(i);
            DrawStruct(x, *elemStruct, elem, {}, nullptr, inner, indent + 2 * kIndent);
            x.choicePrefix = outerPrefix;
        } else {
            Spot s{f, ops.element, elem};
            s.enabled = enabled;
            s.r = {row.x + 46, row.y + 3, std::max(30.f, row.w - 46 - 28), row.h - 6};
            s.scrub = indexR;  // 番号: ドラッグして数を変える
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

    // ドラッグ中の要素: 置かれる所に線。離す = そこへ動かす。
    if (x.st.arrayDrag == arrayId && count > 1) {
        size_t slot = 0;  // 要素 `slot` の前 (count = 末尾)
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
    if (!f.edit && !f.readOnly) return;  // 保存はするが表示しない
    if (!reflect::FieldShown(ownerType, f, owner)) return;  // ShowIf: 今のものには出さない (保存はする)
    ui::Context& c = x.c;
    auto& p = c.P();
    const bool enabled = f.edit && !f.readOnly;
    const void* value = f.Get(owner);
    std::vector<const void*> otherValues;  // いっしょに編集しているオブジェクトの同じフィールド
    for (const void* o : others) otherValues.push_back(f.Get(o));
    FieldPath path = prefix;
    path.push_back(index);

    const Rect row{x.view.x, x.y, x.view.w, kRow};
    const Rect labelR{row.x + 12 + indent, row.y, x.labelW - indent, row.h};
    Rect valueR{row.x + 12 + x.labelW + 8, row.y + 3, std::max(40.f, row.w - x.labelW - 34), row.h - 6};
    x.y += kRow;
    // ベクトルと色は、パネルが狭くて欄が入らないとき自分の行を持つ。
    const size_t comps = Components(f.value.type);
    if (comps > 0 && valueR.w / static_cast<float>(comps) < 64.f + kAxisW) {
        valueR = {row.x + 12 + indent + kIndent, x.y + 1, row.w - 24 - indent - kIndent, kRow - 6};
        x.y += kRow;
    }

    Rect nameR = labelR;
    if (const auto icon = f.icon ? w::IconNamed(f.icon) : std::nullopt) {  // (RM_PROPERTY の Icon="...")
        w::DrawIcon(c, {labelR.x, labelR.y, 14, labelR.h}, *icon, enabled ? theme::TextDim : theme::TextFaint, 14);
        nameR = labelR.Inset(19, 0, 0, 0);
    }
    p.Text(nameR, Utf8ToWide(TrU8(f.Label())), Text(12, enabled ? theme::TextDim : theme::TextFaint, f.value.type == FieldType::Struct));
    if (f.tooltip && c.IsHovered(labelR)) x.tooltip = Utf8ToWide(TrU8(f.tooltip));
    // 行を右クリック: このフィールドのブロックのメニュー (入力から直に読む: 表示だけの
    // ブロックのフィールドは入力を受けないが、これは動く)。
    if (x.fieldMenu && *x.fieldMenu && x.menusOk && !x.wrap) {
        const ui::Input& in = c.In();
        if (in.pressed[static_cast<int>(ui::Mouse::Right)] && row.Contains(in.mouseX, in.mouseY) && x.view.Contains(in.mouseX, in.mouseY)) {
            std::vector<w::Action> actions = (*x.fieldMenu)(path, f.Label());
            if (!actions.empty()) {
                x.st.menuActions = std::move(actions);
                x.st.menu.OpenAt(in.mouseX, in.mouseY);
                x.menusOk = false;  // (メニューは 1 つ)
            }
        }
    }

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
            // 単独の数: ラベルがドラッグのつまみ。
            if (f.value.type == FieldType::Int || f.value.type == FieldType::Float) s.scrub = labelR;
            s.editFor = editFor;
            if (f.value.type == FieldType::String && x.choices && *x.choices) {
                FieldPath full = x.choicePrefix;
                full.insert(full.end(), path.begin(), path.end());
                s.choices = (*x.choices)(full);
            }
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

uint64_t NewMergeKey() { return NewSession(); }

std::optional<Edit> Draw(ui::Context& c, State& st, const Rect& area, std::span<const Block> blocks, std::wstring_view addLabel,
                         const std::function<std::vector<w::Action>()>& addMenu, const Refs* refs) {
    std::optional<Edit> result = std::move(st.pending);
    st.pending.reset();

    const float off = w::BeginScroll(c, "inspector", area, st.contentHeight);
    Pass x{c, st, area};
    x.refs = refs;
    x.y = area.y + 6 - off;
    x.labelW = std::clamp(area.w * 0.38f, 96.f, 220.f);
    // ドロップダウンやメニューが開いている間、その下のフィールドは表示だけ。
    const bool inputWas = c.InputEnabled();
    x.menusOk = inputWas && !st.popup.open && !st.menu.open && !st.addPopup.open;
    if (st.popup.open || st.menu.open || st.addPopup.open) c.SetInputEnabled(false);
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        const Block& b = blocks[bi];
        x.block = bi;
        // タイトルごとの ID (部品を足したり消したりして一覧が変わっても、フィールドが混ざらない)。
        c.PushId(b.title.empty() ? std::string("block") : WideToUtf8(b.title));
        const bool folded = !b.title.empty() && st.folded.contains(b.title);
        if (!b.title.empty()) DrawHeader(x, b, folded);
        if (!folded) {
            if (!b.note.empty()) {
                ui::TextStyle ns = Text(12, theme::TextFaint);
                ns.wrap = true;
                ns.v = ui::VAlign::Top;
                // 注記の高さのとおり (長いと 3 行以上に折り返す)。
                const float h = std::ceil(c.P().TextHeight(b.note, ns.size, x.view.w - 28));
                c.P().Text({x.view.x + 14, x.y, x.view.w - 28, h + 2}, b.note, ns);
                x.y += h + 8;
            }
            if (b.modes.size() == 2) {
                // ブロックの見せ方が 2 つ: フィールドの上のスイッチ (左 = 最初の見せ方)。
                bool second = b.mode == 1;
                if (w::Switch(c, "mode", x.view.x + 12, x.y + 4, second, b.modes[0], b.modes[1]) && b.setMode) b.setMode(second ? 1 : 0);
                x.y += 32;
            } else if (!b.modes.empty()) {
                // もっと多い見せ方: 選択肢を 1 列に並べる。
                std::vector<std::wstring_view> labels(b.modes.begin(), b.modes.end());
                const float wdt = std::min(84.f * static_cast<float>(labels.size()) + 4.f, x.view.w - 24);
                const int picked = w::Segmented(c, "mode", {x.view.x + 12, x.y + 2, wdt, 26}, labels, b.mode);
                if (picked >= 0 && picked != b.mode && b.setMode) b.setMode(picked);
                x.y += 34;
            }
            if (!b.presets.empty()) {
                // できあいの値: フィールドと同じような行 - 名前と、選ぶと値を設定するドロップダウン。
                const Rect row{x.view.x, x.y, x.view.w, kRow};
                const Rect labelR{row.x + 12, row.y, x.labelW, row.h};
                const Rect valueR{row.x + 12 + x.labelW + 8, row.y + 3, std::max(40.f, row.w - x.labelW - 34), row.h - 6};
                c.P().Text(labelR, b.presetLabel, Text(12, theme::TextDim));
                if (w::ComboBox(c, "preset", valueR, Tr(L"選ぶと、すぐその値に"), c.InputEnabled()).clicked) {
                    x.st.menuActions = b.presets;
                    x.st.menu.OpenAt(valueR.x, valueR.Bottom() + 2);
                }
                x.y += kRow + 6;
            }
            if (b.customHeight && b.customDraw) {
                // 自分で描く中身: 欄と同じ左右の余白で。
                const float wdt = std::max(0.f, x.view.w - 24);
                const float h = std::max(0.f, b.customHeight(c, wdt));
                const bool inputBefore = c.InputEnabled();
                if (b.readOnly) c.SetInputEnabled(false);
                b.customDraw(c, {x.view.x + 12, x.y, wdt, h});
                c.SetInputEnabled(inputBefore);
                x.y += h + 4;
            }
            if (b.type && b.object) {
                x.fieldMenu = b.fieldMenu ? &b.fieldMenu : nullptr;
                x.choices = b.choices ? &b.choices : nullptr;
                FieldPath prefix;
                const bool inputBefore = c.InputEnabled();
                if (b.readOnly) c.SetInputEnabled(false);  // 表示だけ: どの欄もクリックもキーも受けない
                if (b.fields.empty() && b.hidden.empty()) {
                    DrawStruct(x, *b.type, b.object, b.others, b.unknowns, prefix, 0);
                } else {
                    for (size_t i = 0; i < b.type->fields.size(); ++i) {
                        const std::string& name = b.type->fields[i].name;
                        if (!b.fields.empty() && std::find(b.fields.begin(), b.fields.end(), name) == b.fields.end()) continue;
                        if (std::find(b.hidden.begin(), b.hidden.end(), name) != b.hidden.end()) continue;
                        DrawField(x, *b.type, b.type->fields[i], i, b.object, b.others, b.unknowns, prefix, 0);
                    }
                }
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
            st.addActions = addMenu();
            st.addPopup.OpenAt(btn.x, btn.Bottom() + 2);
        }
        x.y += 44;
    }
    // パネルのタイトル: ボタンの言葉から "＋" を除いたもの。
    std::wstring addTitle(addLabel);
    if (addTitle.starts_with(L"＋")) addTitle.erase(0, 1);
    while (!addTitle.empty() && addTitle.front() == L' ') addTitle.erase(0, 1);
    c.SetInputEnabled(inputWas);
    st.contentHeight = x.y + off - area.y + 6;
    w::EndScroll(c, "inspector", area, st.contentHeight);
    if (!st.popup.open) st.popupId = 0;

    if (!x.tooltip.empty()) {
        w::Tooltip(c, x.tooltip);
    }
    w::ActionMenu(c, st.menu, st.menuActions);
    w::ActionPicker(c, st.addPopup, st.addPicker, st.addActions, std::move(addTitle));
    if (x.edit) result = std::move(x.edit);
    return result;
}

}  // namespace rm::editor::inspector
