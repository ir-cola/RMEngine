#include "core/reflect/Data.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <string>

namespace rm::reflect {

// ---------------------------------------------------------------------------
// Type lookup
// ---------------------------------------------------------------------------

const TypeDesc* FindType(const std::type_info& t) {
    for (const TypeDesc* d : AllTypes()) {
        if (d->cppType() == t) return d;
    }
    return nullptr;
}

const EnumDesc* FindEnum(const std::type_info& t) {
    for (const EnumDesc* d : AllEnums()) {
        if (d->cppType() == t) return d;
    }
    return nullptr;
}

const EnumItem* FindItem(const EnumDesc& e, int64_t value) {
    for (const auto& it : e.items) {
        if (it.value == value) return &it;
    }
    return nullptr;
}

const EnumItem* FindItem(const EnumDesc& e, std::string_view name) {
    for (const auto& it : e.items) {
        if (name == it.name) return &it;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ShowIf
// ---------------------------------------------------------------------------

namespace {

// One term of a ShowIf ("kind=A|B", "flag", "!flag") against `object` (null: only checked - *problem
// says what is wrong).
bool ShowIfTerm(const TypeDesc& owner, std::string_view term, const void* object, std::string* problem) {
    const bool negate = term.starts_with('!');
    if (negate) term.remove_prefix(1);
    const size_t eq = term.find('=');
    const std::string_view name = term.substr(0, eq);
    const FieldDesc* g = nullptr;
    for (const FieldDesc& x : owner.fields) {
        if (name == x.name) g = &x;
    }
    auto bad = [&](std::string why) {
        if (problem && problem->empty()) *problem = std::move(why);
        return false;
    };
    if (!g) return bad(std::format("ShowIf: {} にフィールド {} がありません", owner.name, name));
    bool on = false;
    if (eq == std::string_view::npos) {
        if (g->value.type != FieldType::Bool) return bad(std::format("ShowIf: {} は bool ではありません (項目なら {}=項目)", name, name));
        on = object && *static_cast<const bool*>(g->Get(object));
    } else {
        if (g->value.type != FieldType::Enum || !g->value.enumType) return bad(std::format("ShowIf: {} は RM_ENUM ではありません", name));
        const EnumDesc& e = *g->value.enumType;
        const int64_t now = object ? e.get(g->Get(object)) : 0;
        std::string_view items = term.substr(eq + 1);
        for (;;) {
            const size_t bar = items.find('|');
            const std::string_view item = items.substr(0, bar);
            const EnumItem* it = FindItem(e, item);
            if (!it) return bad(std::format("ShowIf: {} に項目 {} がありません", e.name, item));
            if (it->value == now) on = true;
            if (bar == std::string_view::npos) break;
            items = items.substr(bar + 1);
        }
    }
    return negate ? !on : on;
}

template <class F>
bool EveryTerm(std::string_view showIf, F&& f) {
    bool all = true;
    for (;;) {
        const size_t amp = showIf.find('&');
        if (!f(showIf.substr(0, amp))) all = false;
        if (amp == std::string_view::npos) return all;
        showIf = showIf.substr(amp + 1);
    }
}

}  // namespace

bool FieldShown(const TypeDesc& owner, const FieldDesc& f, const void* object) {
    if (!f.showIf) return true;
    return EveryTerm(f.showIf, [&](std::string_view t) { return ShowIfTerm(owner, t, object, nullptr); });
}

std::string ShowIfProblem(const TypeDesc& owner, const FieldDesc& f) {
    std::string problem;
    if (f.showIf) EveryTerm(f.showIf, [&](std::string_view t) { return ShowIfTerm(owner, t, nullptr, &problem); });
    return problem;
}

// ---------------------------------------------------------------------------
// Unknowns
// ---------------------------------------------------------------------------

bool Unknowns::Empty() const {
    if (!keys.empty() || !enums.empty()) return false;
    return std::all_of(children.begin(), children.end(), [](const Nested& c) { return c.value.Empty(); });
}

Unknowns* Unknowns::Child(const std::string& key, bool create) {
    for (auto& c : children) {
        if (c.key == key) return &c.value;
    }
    if (!create) return nullptr;
    children.push_back({key, {}});
    return &children.back().value;
}

const Unknowns* Unknowns::Child(const std::string& key) const {
    for (const auto& c : children) {
        if (c.key == key) return &c.value;
    }
    return nullptr;
}

const std::string* Unknowns::Enum(std::string_view field) const {
    for (const auto& [f, name] : enums) {
        if (f == field) return &name;
    }
    return nullptr;
}

void Unknowns::ForgetEnum(std::string_view field) {
    std::erase_if(enums, [&](const auto& e) { return e.first == field; });
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

double SanitizeNumber(const FieldDesc& f, double v, double fallback) {
    if (!std::isfinite(v)) v = fallback;
    if (f.hasMin) v = std::max(v, f.min);
    if (f.hasMax) v = std::min(v, f.max);
    if (f.value.type == FieldType::Int || (f.array && f.array->element.type == FieldType::Int)) {
        v = std::round(v);
        v = std::clamp(v, static_cast<double>(std::numeric_limits<int>::min()),
                       static_cast<double>(std::numeric_limits<int>::max()));
    }
    return v;
}

std::optional<double> ParseNumber(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    double v = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
    if (text.empty() || r.ec != std::errc() || r.ptr != text.data() + text.size()) return std::nullopt;
    return v;
}

std::optional<double> NumberFromInput(const FieldDesc& f, std::string_view text, double current) {
    const auto v = ParseNumber(text);
    if (!v) return std::nullopt;
    return SanitizeNumber(f, *v, current);
}

std::string FormatNumber(const FieldDesc& f, double v) {
    const bool isInt = f.value.type == FieldType::Int || (f.array && f.array->element.type == FieldType::Int);
    if (isInt) return std::to_string(static_cast<long long>(v));
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), static_cast<float>(v));
    return std::string(buf, r.ptr);
}

std::string ReadString(const FieldDesc& f, const void* field) {
    if (!f.stringAccess) return *static_cast<const std::string*>(field);
    char buf[256];
    const size_t n = f.stringAccess->get(f.stringAccess->ctx, field, buf, sizeof(buf));
    if (n < sizeof(buf)) return std::string(buf, n);
    std::string s(n + 1, '\0');
    f.stringAccess->get(f.stringAccess->ctx, field, s.data(), n + 1);
    s.resize(n);
    return s;
}

void WriteString(const FieldDesc& f, void* field, std::string_view text) {
    if (f.stringAccess) {
        f.stringAccess->set(f.stringAccess->ctx, field, text.data(), text.size());
    } else {
        static_cast<std::string*>(field)->assign(text);
    }
}

std::string ReadAssetId(const FieldDesc& f, const void* field) {
    if (!f.stringAccess) return static_cast<const AssetRef*>(field)->guid;
    return ReadString(f, field);
}

void WriteAssetId(const FieldDesc& f, void* field, std::string_view id) {
    if (!f.stringAccess) {
        static_cast<AssetRef*>(field)->guid.assign(id);
        return;
    }
    WriteString(f, field, id);
}

namespace {

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

float& F(void* p, size_t i) { return static_cast<float*>(p)[i]; }
float F(const void* p, size_t i) { return static_cast<const float*>(p)[i]; }

// A float is written as the shortest decimal that reads back as the same float ("0.02", not
// "0.019999999552965164"): the file stays readable and the round trip stays exact.
double FloatForFile(float f) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), f);
    double d = f;
    std::from_chars(buf, r.ptr, d);
    return d;
}

size_t Components(FieldType t) {
    switch (t) {
        case FieldType::Vec2: return 2;
        case FieldType::Vec3: return 3;
        case FieldType::Color: return 4;
        default: return 0;
    }
}

const char* TypeWord(FieldType t) {
    switch (t) {
        case FieldType::Bool: return "真偽";
        case FieldType::Int: return "整数";
        case FieldType::Float: return "数値";
        case FieldType::String: return "文字";
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Color: return "数値の並び";
        case FieldType::Enum: return "名前";
        case FieldType::Struct: return "オブジェクト";
        case FieldType::Array: return "配列";
        case FieldType::AssetRef: return "アセットの ID (文字)";
        case FieldType::EntityRef: return "エンティティの ID (0 以上の整数)";
    }
    return "?";
}

std::string Where(const char* typeName) { return typeName && *typeName ? std::string(typeName) + "." : std::string(); }

void Warn(Warnings* w, std::string msg) {
    if (w) w->push_back(std::move(msg));
}

json::Value SaveValue(const FieldDesc& f, const ValueType& vt, const void* p, const Unknowns* parent, const std::string& key);
void LoadValue(const FieldDesc& f, const ValueType& vt, void* p, const json::Value& v, Unknowns* parent, const std::string& key,
               Warnings* w, const char* typeName);

json::Value SaveValue(const FieldDesc& f, const ValueType& vt, const void* p, const Unknowns* parent, const std::string& key) {
    switch (vt.type) {
        case FieldType::Bool: return json::Value(*static_cast<const bool*>(p));
        case FieldType::Int: return json::Value(static_cast<int64_t>(*static_cast<const int*>(p)));
        case FieldType::Float: return json::Value(FloatForFile(*static_cast<const float*>(p)));
        case FieldType::String: return json::Value(ReadString(f, p));
        case FieldType::AssetRef: return json::Value(ReadAssetId(f, p));
        case FieldType::EntityRef: return json::Value(static_cast<const EntityRef*>(p)->id);
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Color: {
            json::Value a = json::Value::MakeArray();
            for (size_t i = 0; i < Components(vt.type); ++i) a.Push(FloatForFile(F(p, i)));
            return a;
        }
        case FieldType::Enum: {
            if (parent) {
                if (const std::string* unknown = parent->Enum(key)) return json::Value(*unknown);  // written back as it was
            }
            const EnumItem* it = FindItem(*vt.enumType, vt.enumType->get(p));
            return it ? json::Value(it->name) : json::Value(vt.enumType->get(p));
        }
        case FieldType::Struct: return Save(*vt.structType, p, parent ? parent->Child(key) : nullptr);
        case FieldType::Array: {
            json::Value a = json::Value::MakeArray();
            const ArrayOps& ops = *f.array;
            const size_t n = ops.size(p);
            for (size_t i = 0; i < n; ++i) {
                a.Push(SaveValue(f, ops.element, ops.at(const_cast<void*>(p), i), parent, std::format("{}[{}]", key, i)));
            }
            return a;
        }
    }
    return {};
}

void LoadValue(const FieldDesc& f, const ValueType& vt, void* p, const json::Value& v, Unknowns* parent, const std::string& key,
               Warnings* w, const char* typeName) {
    auto wrongType = [&] {
        Warn(w, std::format("{}{}: {} が必要なところに別の種類の値があったので、読み飛ばしました", Where(typeName), key, TypeWord(vt.type)));
    };
    auto number = [&](double current, double& out) -> bool {
        if (!v.IsNumber()) {
            wrongType();
            return false;
        }
        out = SanitizeNumber(f, v.AsDouble(), current);
        if (out != v.AsDouble()) Warn(w, std::format("{}{}: {} は使えない値なので {} にしました", Where(typeName), key, v.AsDouble(), out));
        return true;
    };
    switch (vt.type) {
        case FieldType::Bool:
            if (v.IsBool()) {
                *static_cast<bool*>(p) = v.AsBool();
            } else {
                wrongType();
            }
            return;
        case FieldType::Int: {
            double d = 0;
            if (number(*static_cast<int*>(p), d)) *static_cast<int*>(p) = static_cast<int>(d);
            return;
        }
        case FieldType::Float: {
            double d = 0;
            if (number(*static_cast<float*>(p), d)) *static_cast<float*>(p) = static_cast<float>(d);
            return;
        }
        case FieldType::String:
            if (v.IsString()) {
                WriteString(f, p, v.AsString());
            } else {
                wrongType();
            }
            return;
        case FieldType::AssetRef:
            // Kept as written even when it is not an id this version knows (written back unchanged).
            if (v.IsString()) {
                WriteAssetId(f, p, v.AsString());
            } else if (v.IsNull()) {
                WriteAssetId(f, p, "");
            } else {
                wrongType();
            }
            return;
        case FieldType::EntityRef:
            if (v.IsInt() && v.AsInt() >= 0) {
                static_cast<EntityRef*>(p)->id = static_cast<uint64_t>(v.AsInt());
            } else if (v.IsNull()) {
                static_cast<EntityRef*>(p)->id = 0;
            } else {
                wrongType();
            }
            return;
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Color: {
            const size_t n = Components(vt.type);
            if (!v.IsArray() || v.Size() != n) {
                wrongType();
                return;
            }
            for (size_t i = 0; i < n; ++i) {
                const json::Value& c = v.At(i);
                if (!c.IsNumber()) {
                    wrongType();
                    return;
                }
                F(p, i) = static_cast<float>(SanitizeNumber(f, c.AsDouble(), F(p, i)));
            }
            return;
        }
        case FieldType::Enum: {
            if (!v.IsString()) {
                wrongType();
                return;
            }
            if (const EnumItem* it = FindItem(*vt.enumType, v.AsString())) {
                vt.enumType->set(p, it->value);
                if (parent) parent->ForgetEnum(key);
            } else {
                // Behave with the default value, but keep the name and write it back (design R1 4-f).
                Warn(w, std::format("{}{}: 「{}」はこの版では分からない値なので既定値で動かします (保存すると元のまま残ります)",
                                    Where(typeName), key, v.AsString()));
                if (parent) {
                    parent->ForgetEnum(key);
                    parent->enums.emplace_back(key, v.AsString());
                }
            }
            return;
        }
        case FieldType::Struct:
            if (!v.IsObject()) {
                wrongType();
                return;
            }
            Load(*vt.structType, p, v, parent ? parent->Child(key, true) : nullptr, w);
            return;
        case FieldType::Array: {
            if (!v.IsArray()) {
                wrongType();
                return;
            }
            const ArrayOps& ops = *f.array;
            ops.resize(p, v.Size());
            for (size_t i = 0; i < v.Size(); ++i) {
                LoadValue(f, ops.element, ops.at(p, i), v.At(i), parent, std::format("{}[{}]", key, i), w, typeName);
            }
            return;
        }
    }
}

bool ValueEquals(const FieldDesc& f, const ValueType& vt, const void* a, const void* b) {
    switch (vt.type) {
        case FieldType::Bool: return *static_cast<const bool*>(a) == *static_cast<const bool*>(b);
        case FieldType::Int: return *static_cast<const int*>(a) == *static_cast<const int*>(b);
        case FieldType::Float: return *static_cast<const float*>(a) == *static_cast<const float*>(b);
        case FieldType::String: return ReadString(f, a) == ReadString(f, b);
        case FieldType::AssetRef: return ReadAssetId(f, a) == ReadAssetId(f, b);
        case FieldType::EntityRef: return *static_cast<const EntityRef*>(a) == *static_cast<const EntityRef*>(b);
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Color:
            for (size_t i = 0; i < Components(vt.type); ++i) {
                if (F(a, i) != F(b, i)) return false;
            }
            return true;
        case FieldType::Enum: return vt.enumType->get(a) == vt.enumType->get(b);
        case FieldType::Struct: return Equals(*vt.structType, a, b);
        case FieldType::Array: {
            const ArrayOps& ops = *f.array;
            const size_t n = ops.size(a);
            if (n != ops.size(b)) return false;
            for (size_t i = 0; i < n; ++i) {
                if (!ValueEquals(f, ops.element, ops.at(const_cast<void*>(a), i), ops.at(const_cast<void*>(b), i))) return false;
            }
            return true;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

json::Value SaveField(const FieldDesc& f, const void* object, const Unknowns* unknowns) {
    return SaveValue(f, f.value, f.Get(object), unknowns, f.name);
}

void LoadField(const FieldDesc& f, void* object, const json::Value& v, Unknowns* unknowns, Warnings* w) {
    LoadValue(f, f.value, f.Get(object), v, unknowns, f.name, w, "");
}

json::Value Save(const TypeDesc& type, const void* object, const Unknowns* unknowns) {
    json::Value o = json::Value::MakeObject();
    for (const FieldDesc& f : type.fields) {
        if (!f.transient) o.Set(f.name, SaveField(f, object, unknowns));
    }
    if (unknowns) {
        for (const auto& [k, v] : unknowns->keys) {
            if (!o.Find(k)) o.Set(k, v);
        }
    }
    return o;
}

void Load(const TypeDesc& type, void* object, const json::Value& v, Unknowns* unknowns, Warnings* w) {
    if (!v.IsObject()) {
        Warn(w, std::format("{}: オブジェクトではないので読めませんでした", type.name));
        return;
    }
    if (unknowns) unknowns->keys.clear();
    for (const auto& [key, value] : v.Members()) {
        const FieldDesc* field = nullptr;
        for (const FieldDesc& f : type.fields) {
            if (!f.transient && key == f.name) field = &f;
        }
        if (field) {
            LoadValue(*field, field->value, field->Get(object), value, unknowns, field->name, w, type.name);
        } else if (unknowns) {
            unknowns->keys.emplace_back(key, value);  // a field of a newer version, or of a removed one
        }
    }
}

namespace {

void MapValueRefs(const ValueType& vt, const ArrayOps* array, void* p, const std::function<uint64_t(uint64_t)>& map) {
    switch (vt.type) {
        case FieldType::EntityRef: {
            uint64_t& id = static_cast<EntityRef*>(p)->id;
            if (id != 0) id = map(id);
            return;
        }
        case FieldType::Struct: MapEntityRefs(*vt.structType, p, map); return;
        case FieldType::Array: {
            const size_t n = array->size(p);
            for (size_t i = 0; i < n; ++i) MapValueRefs(array->element, nullptr, array->at(p, i), map);
            return;
        }
        default: return;
    }
}

}  // namespace

void MapEntityRefs(const TypeDesc& type, void* object, const std::function<uint64_t(uint64_t)>& map) {
    for (const FieldDesc& f : type.fields) {
        if (!f.transient) MapValueRefs(f.value, f.array, f.Get(object), map);
    }
}

bool Equals(const TypeDesc& type, const void* a, const void* b) {
    for (const FieldDesc& f : type.fields) {
        if (!f.transient && !ValueEquals(f, f.value, f.Get(a), f.Get(b))) return false;
    }
    return true;
}

uint64_t Diff(const TypeDesc& type, const void* a, const void* b) {
    uint64_t bits = 0;
    for (size_t i = 0; i < type.fields.size() && i < 64; ++i) {
        const FieldDesc& f = type.fields[i];
        if (!f.transient && !ValueEquals(f, f.value, f.Get(a), f.Get(b))) bits |= uint64_t{1} << i;
    }
    return bits;
}

PathTarget Resolve(const TypeDesc& type, void* object, Unknowns* unknowns, const FieldPath& path) {
    PathTarget t;
    const TypeDesc* cur = &type;
    void* owner = object;
    Unknowns* u = unknowns;
    for (size_t depth = 0; depth < path.size(); ++depth) {
        if (path[depth] >= cur->fields.size()) return {};
        const FieldDesc& f = cur->fields[path[depth]];
        if (depth + 1 == path.size()) {
            t.field = &f;
            t.owner = owner;
            t.unknowns = u;
            return t;
        }
        if (f.value.type != FieldType::Struct) return {};
        owner = f.Get(owner);
        u = u ? u->Child(f.name, true) : nullptr;
        cur = f.value.structType;
    }
    return {};
}

std::string PathText(const TypeDesc& type, const FieldPath& path) {
    std::string out;
    const TypeDesc* cur = &type;
    for (const size_t i : path) {
        if (!cur || i >= cur->fields.size()) break;
        if (!out.empty()) out += '.';
        out += cur->fields[i].name;
        cur = cur->fields[i].value.structType;
    }
    return out;
}

}  // namespace rm::reflect
