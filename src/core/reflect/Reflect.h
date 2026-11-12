#pragma once
// Reflection (design R1, TestEngine docs/REMAKE_R1_REFLECT.md): a data type is described ONCE,
// in its own header, and saving, loading, the inspector, comparison, copy, diff, undo and range
// clamping all come from that description. Nothing about a field is written a second time.
//
//   RM_STRUCT(Label="ゲームの設定")
//   struct GameSettings {
//       RM_PROPERTY(Edit, Label="目標 FPS", Min=1, Max=240) int targetFps = 60;
//       RM_PROPERTY(Edit, Label="名前")                     std::string title = "My Game";
//       RM_TRANSIENT()                                       float timer = 0;   // not saved, not shown
//   };
//
//   RM_ENUM() enum class Quality { RM_ITEM(Label="低") Low, Medium, High };
//
// The macros expand to nothing. tools/rmgen reads the headers before every build and writes
// build/generated/<project>/*.reflect.cpp (which include the header - the header includes
// nothing generated) and rm_all_types.cpp (the table of every type; no self-registration, which
// a static library would silently drop). The generated code also checks at compile time that it
// still matches the header (field count, field names and types): edit a header without
// regenerating and the build stops with "rmgen を走らせ直してください".
//
// Every field of a reflected struct must be RM_PROPERTY or RM_TRANSIENT (error RM0001).
// RM_PROPERTY attributes: Edit, ReadOnly, Label="", Tooltip="", Min=, Max=, Step= (numbers only),
// AssetKind="model" / "image,model" (AssetRef only: the kinds of file it takes - core/asset).
//
// Types: bool, int, float, std::string, Vec2, Vec3, Color, AssetRef, EntityRef (base/Ref.h: saved
// as the id), RM_ENUM enums (saved by NAME), RM_STRUCT structs (nested), std::vector<one of those>.

#include "base/Math.h"
#include "base/Ref.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#define RM_STRUCT(...)
#define RM_COMPONENT(...)
#define RM_SCRIPT(...)
#define RM_ENUM(...)
#define RM_ITEM(...)
#define RM_PROPERTY(...)
#define RM_TRANSIENT(...)

namespace rm::reflect {

enum class FieldType : uint8_t { Bool, Int, Float, String, Vec2, Vec3, Color, Enum, Struct, Array, AssetRef, EntityRef };

struct TypeDesc;

struct EnumItem {
    const char* name;   // saved in files
    const char* label;  // shown (UTF-8); the name when there is no RM_ITEM(Label=)
    int64_t value;
};

struct EnumDesc {
    const char* name;       // C++ name (qualified)
    std::span<const EnumItem> items;
    int64_t (*get)(const void* p);
    void (*set)(void* p, int64_t v);
    const std::type_info& (*cppType)();
};

// The type of a value: a field, or an element of an array field.
struct ValueType {
    FieldType type = FieldType::Int;
    const TypeDesc* structType = nullptr;  // Struct
    const EnumDesc* enumType = nullptr;    // Enum
};

struct ArrayOps {
    ValueType element;
    size_t (*size)(const void* vec);
    void (*resize)(void* vec, size_t n);
    void* (*at)(void* vec, size_t i);
};

// A std::string that lives in another module (a script DLL, built with its own C runtime): read and
// written only through that module's functions - this program never assigns to it or frees it (its
// memory is the other runtime's). On an AssetRef field: the id kept in another form (a script's
// char guid[...]) - read and written as text the same way.
struct StringAccess {
    const void* ctx;
    size_t (*get)(const void* ctx, const void* field, char* buf, size_t cap);  // the full length; copies what fits (0-terminated)
    void (*set)(const void* ctx, void* field, const char* utf8, size_t len);
};

struct FieldDesc {
    const char* name;  // C++ name = key in files
    ValueType value;
    void* (*access)(void* object);  // the field inside an object
    const ArrayOps* array = nullptr;  // Array
    bool transient = false;           // RM_TRANSIENT: not saved, not shown, not compared
    bool edit = false;                // shown in the inspector
    bool readOnly = false;
    const char* label = nullptr;      // UTF-8
    const char* tooltip = nullptr;
    bool hasMin = false, hasMax = false, hasStep = false;
    double min = 0, max = 0, step = 0;
    int line = 0;  // in the header (for messages)
    const char* assetKinds = nullptr;  // AssetRef: AssetKind="..." (null: any file)
    const StringAccess* stringAccess = nullptr;  // String of another module (scripts): ReadString / WriteString

    const void* Get(const void* object) const { return access(const_cast<void*>(object)); }
    void* Get(void* object) const { return access(object); }
    const char* Label() const { return label ? label : name; }
};

enum class TypeKind : uint8_t { Struct, Component, Script };

struct TypeDesc {
    const char* name;   // C++ name (qualified)
    const char* label;  // UTF-8 (RM_STRUCT(Label=)), the plain name when there is none
    TypeKind kind;
    size_t size;
    std::span<const FieldDesc> fields;
    void (*construct)(void* p);  // placement new with the default member values
    void (*destruct)(void* p);
    void (*copy)(void* dst, const void* src);
    const std::type_info& (*cppType)();
    const char* header;  // the header it is declared in (for messages)
};

// Every reflected type of this program (written by rmgen into rm_all_types.cpp).
std::span<const TypeDesc* const> AllTypes();
std::span<const EnumDesc* const> AllEnums();

const TypeDesc* FindType(const std::type_info& t);
const EnumDesc* FindEnum(const std::type_info& t);

template <class T>
const TypeDesc& TypeOf() {
    static const TypeDesc* d = FindType(typeid(T));
    return *d;  // a type without RM_STRUCT / RM_COMPONENT / RM_SCRIPT has no description
}

// --- Compile-time checks used by the generated code ----------------------------------------

namespace detail {

// Converts to any field type (as an lvalue reference, so only this conversion is considered).
struct AnyField {
    template <class U>
    constexpr operator U&() const noexcept;
};

template <class T, size_t... I>
constexpr bool InitializableWith(std::index_sequence<I...>) {
    return requires { T{((void)I, AnyField{})...}; };
}

template <class T, size_t N>
constexpr size_t CountFields() {
    if constexpr (N > 128) {
        return N;
    } else if constexpr (InitializableWith<T>(std::make_index_sequence<N + 1>{})) {
        return CountFields<T, N + 1>();
    } else {
        return N;
    }
}

}  // namespace detail

// Number of members of an aggregate struct (every reflected struct is one).
template <class T>
constexpr size_t FieldCount() {
    return detail::CountFields<T, 0>();
}

}  // namespace rm::reflect
