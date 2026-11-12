#pragma once
// What every reflected type gets from its description: saving, loading (with range clamping and
// "keep what you do not know"), comparison, diff and the input rule of numbers. One function each,
// walking the TypeDesc - there is no per-type copy of any of these (rules 2 and 3 of CLAUDE.md).
//
// Files are JSON objects keyed by field name. Enums are saved by NAME (inserting or reordering
// enumerators never changes what old files mean). Vec2 / Vec3 / Color are arrays of numbers.

#include "base/Json.h"
#include "core/reflect/Reflect.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::reflect {

// What a file had that this version does not understand (design R1 4-f / BluePrint 10-b).
// It is kept next to the object and written back unchanged on save, so opening and saving a file
// from a newer version (or with a plugin missing) never loses data.
struct Unknowns {
    struct Nested;
    std::vector<json::Value::Member> keys;                   // unknown keys, in file order
    std::vector<std::pair<std::string, std::string>> enums;  // field -> enum name not in this version
    std::vector<Nested> children;                            // "field" (struct) / "field[3]" (array element)

    bool Empty() const;
    Unknowns* Child(const std::string& key, bool create);
    const Unknowns* Child(const std::string& key) const;
    const std::string* Enum(std::string_view field) const;
    void ForgetEnum(std::string_view field);
};

struct Unknowns::Nested {
    std::string key;
    Unknowns value;
};

// Messages for the console (Japanese, UTF-8), e.g. a value out of range or an unknown enum name.
using Warnings = std::vector<std::string>;

// A String field's text, and setting it - through FieldDesc::stringAccess when the string is another
// module's (every string field goes through these two).
std::string ReadString(const FieldDesc& f, const void* field);
void WriteString(const FieldDesc& f, void* field, std::string_view text);

json::Value Save(const TypeDesc& type, const void* object, const Unknowns* unknowns = nullptr);
// Fields missing from the file keep their current value (so: default-construct, then Load).
void Load(const TypeDesc& type, void* object, const json::Value& v, Unknowns* unknowns = nullptr,
          Warnings* warnings = nullptr);

// One field (the value at `field.Get(object)`); `unknowns` is the object's (enum names live there).
json::Value SaveField(const FieldDesc& field, const void* object, const Unknowns* unknowns);
void LoadField(const FieldDesc& field, void* object, const json::Value& v, Unknowns* unknowns, Warnings* warnings);

// Every EntityRef inside `object` (in nested structs and arrays too) that is set: id = map(id)
// (0 clears it). Copies of entities use it to point at the copies (pasting, prefabs).
void MapEntityRefs(const TypeDesc& type, void* object, const std::function<uint64_t(uint64_t)>& map);

bool Equals(const TypeDesc& type, const void* a, const void* b);
// Bit i set = field i differs (transient fields never). At most 64 fields (rmgen checks).
uint64_t Diff(const TypeDesc& type, const void* a, const void* b);

// THE rule for a number entering a field, used by loading and by the inspector alike:
// not finite -> fallback (the current value); then clamped to Min / Max; Int fields are rounded.
double SanitizeNumber(const FieldDesc& field, double v, double fallback);
// What the user typed into a number field: nullopt when it is not a number at all.
std::optional<double> ParseNumber(std::string_view text);
// The typed text after SanitizeNumber - the inspector's path, tested against the loader's (T3).
std::optional<double> NumberFromInput(const FieldDesc& field, std::string_view text, double current);
std::string FormatNumber(const FieldDesc& field, double v);

// A field inside nested structs: indices into fields, outermost first.
using FieldPath = std::vector<size_t>;
struct PathTarget {
    const FieldDesc* field = nullptr;
    void* owner = nullptr;          // the struct that holds the field
    Unknowns* unknowns = nullptr;   // that struct's unknowns (null when none are kept)
};
PathTarget Resolve(const TypeDesc& type, void* object, Unknowns* unknowns, const FieldPath& path);
std::string PathText(const TypeDesc& type, const FieldPath& path);  // "window.width"

const EnumItem* FindItem(const EnumDesc& e, int64_t value);
const EnumItem* FindItem(const EnumDesc& e, std::string_view name);

template <class T>
json::Value Save(const T& v, const Unknowns* u = nullptr) {
    return Save(TypeOf<T>(), &v, u);
}
template <class T>
void Load(T& v, const json::Value& j, Unknowns* u = nullptr, Warnings* w = nullptr) {
    Load(TypeOf<T>(), &v, j, u, w);
}

}  // namespace rm::reflect
