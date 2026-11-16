#pragma once
// rmgen: reads headers with RM_STRUCT / RM_ENUM ... (core/reflect/Reflect.h) and writes the
// reflection tables. Pure (text in, text out) so the tests can feed it headers directly.
//
// Errors are printed as   <file>(<line>,<col>): error RM####: <日本語>   (clickable in Visual Studio,
// and MSBuild counts them as build errors). Nothing is skipped silently: anything inside a
// reflected type that rmgen cannot handle is an error, never "ignored".
//
//   RM0001  a field without RM_PROPERTY / RM_TRANSIENT
//   RM0002  unknown attribute key
//   RM0003  attribute value of the wrong form (numbers must be literals), or Min > Max, or Min on a non-number,
//           or AssetKind on something that is not an AssetRef / not a list of lower-case words,
//           or ShowIf naming no other field of the struct
//   RM0004  a type that cannot be reflected (pointer, double, const, C array ...)
//   RM0005  an annotation in a place where it does nothing
//   RM0006  a type name that is not RM_STRUCT / RM_ENUM
//   RM0007  syntax rmgen does not understand
//   RM0008  array of arrays
//   RM0009  the same type name twice
//   RM0010  more than 64 fields
//   RM0011  private / protected / base classes (reflected types are plain public structs)
//   RM0012  two headers that would make the same generated file (or have the same include path)
//   RM0013  (scripts) something a script DLL cannot have yet: RM_STRUCT / RM_ENUM, a field of a list,
//           an enum, a struct or an AssetRef

#include <string>
#include <string_view>
#include <vector>

namespace rmgen {

struct Diagnostic {
    std::string file;
    int line = 0, col = 0;
    std::string code;     // "RM0001"
    std::string message;  // Japanese, UTF-8
    std::string Text() const;
};

struct Attr {
    std::string key;
    std::string value;  // string literal: the text between the quotes, exactly as written
    enum class Kind { Flag, String, Number, Ident } kind = Kind::Flag;
    int line = 0, col = 0;
};

struct TypeRef {
    enum class Kind { Bool, Int, Float, String, Vec2, Vec3, Color, AssetRef, EntityRef, Named, Vector } kind = Kind::Int;
    std::string name;               // Named: as written ("Quality", "game::Quality")
    std::vector<TypeRef> element;   // Vector: exactly one
    std::string resolved;           // Named, after Validate: qualified name ("rm::game::Quality")
    bool isEnum = false;            // Named, after Validate
    std::string assetType;          // AssetRef of a script: "PrefabRef" / "ImageRef" / "ModelRef" (RM.h's)
};

struct Field {
    std::string name;
    std::string cppType;  // as written, for the compile-time type check
    TypeRef type;
    bool transient = false;
    std::vector<Attr> attrs;
    int line = 0, col = 0;
};

struct EnumItem {
    std::string name;
    std::string label;  // raw literal text, empty = none
    int line = 0;
};

struct Enum {
    std::string name, ns;  // ns: "rm::game" (empty = global)
    std::string label;
    int line = 0;
    std::vector<EnumItem> items;
    std::string Qualified() const { return ns.empty() ? name : ns + "::" + name; }
};

struct Struct {
    std::string name, ns;
    std::string macro;  // RM_STRUCT / RM_COMPONENT / RM_SCRIPT
    std::string label;
    int line = 0;
    std::vector<Field> fields;  // every data member, in order (transient ones included)
    std::string Qualified() const { return ns.empty() ? name : ns + "::" + name; }
};

struct Header {
    std::string file;         // for messages (as given)
    std::string includePath;  // "rm_engine/game_production/runtime/GameSettings.h"
    std::vector<Struct> structs;
    std::vector<Enum> enums;
};

// Reads one header. Appends problems to diags.
Header Scan(std::string_view text, const std::string& file, const std::string& includePath, std::vector<Diagnostic>& diags);
// Checks the whole program: type names, attributes, limits. Fills TypeRef::resolved.
void Validate(std::vector<Header>& headers, std::vector<Diagnostic>& diags);
// The generated .cpp of one header, and the table of every type.
std::string EmitHeader(const std::vector<Header>& all, const Header& h);
std::string EmitAll(const std::vector<Header>& all);
// Scripts (rmgen --scripts: the headers of a game's Assets/Scripts): only RM_SCRIPT, with the field
// types RM.h has. The one generated .cpp of the script DLL: the scripts' tables (RmAbi.h) and
// RmModuleInit.
void ValidateScripts(const std::vector<Header>& headers, std::vector<Diagnostic>& diags);
std::string EmitScripts(const std::vector<Header>& all);
// File name of the generated .cpp for an include path: readable, plus a hash of the whole path so two
// paths never meet ("a/b_c/D.h" and "a/b/c/D.h" both read a_b_c_D): "a/b/C.h" -> "a_b_C.<8 hex>.reflect.cpp".
// Validate still checks every pair (RM0012), ignoring case as Windows does.
std::string OutputName(const std::string& includePath);

}  // namespace rmgen
