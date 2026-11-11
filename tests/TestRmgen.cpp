// Tests for the generator (tools/rmgen/Scanner): errors with codes and the user's line numbers
// (design R1 T4 / T6), and what it reads from a header.

#include "Test.h"

#include "rmgen/Scanner.h"

#include <string>
#include <vector>

using namespace rmgen;

namespace {

struct Result {
    std::vector<Header> headers;
    std::vector<Diagnostic> diags;
};

Result Run(std::string_view text) {
    Result r;
    r.headers.push_back(Scan(text, "T.h", "T.h", r.diags));
    Validate(r.headers, r.diags);
    return r;
}

bool Has(const Result& r, const char* code, int line) {
    for (const auto& d : r.diags) {
        if (d.code == code && d.line == line) return true;
    }
    return false;
}

}  // namespace

RM_TEST(Rmgen_ReadsAHeader) {
    const Result r = Run(R"(#pragma once
#include "core/reflect/Reflect.h"
// RM_PROPERTY in a comment is ignored
namespace game::data {
RM_ENUM() enum class Mode : int { RM_ITEM(Label="速い") Fast, Slow = 4 };
RM_STRUCT(Label="速度")
struct Speed final {
    RM_PROPERTY(Edit, Label="値", Min=-1.5, Max=2e3, Step=0.25f) float value = 1.0f;
    RM_PROPERTY(Edit) std::vector<Mode> modes;
    RM_PROPERTY() std::string name{"x"};
    RM_TRANSIENT() int* scratch = nullptr;
    void Update(float dt) { value += dt; }
    static constexpr int kMax = 3;
    Speed() = default;
};
}
)");
    CHECK(r.diags.empty());
    const Header& h = r.headers[0];
    CHECK(h.enums.size() == 1 && h.enums[0].Qualified() == "game::data::Mode");
    CHECK(h.enums[0].items.size() == 2 && h.enums[0].items[0].label == "速い");
    CHECK(h.structs.size() == 1);
    const Struct& s = h.structs[0];
    CHECK(s.Qualified() == "game::data::Speed" && s.label == "速度");
    CHECK(s.fields.size() == 4);
    CHECK(s.fields[0].name == "value" && s.fields[0].line == 8);
    CHECK(s.fields[1].type.kind == TypeRef::Kind::Vector && s.fields[1].type.element[0].resolved == "game::data::Mode");
    CHECK(s.fields[1].type.element[0].isEnum);
    CHECK(s.fields[3].transient);
    const std::string out = EmitHeader(r.headers, h);
    CHECK(out.find("FieldCount<::game::data::Speed>() == 4") != std::string::npos);
    CHECK(out.find(".min = -1.5") != std::string::npos && out.find(".step = 0.25") != std::string::npos);
    CHECK(OutputName("a/b/C.h") == "a_b_C.reflect.cpp");
}

RM_TEST(Rmgen_T4_UnannotatedFieldIsAnError) {
    const Result r = Run("RM_STRUCT()\nstruct A {\n    RM_PROPERTY() int a = 0;\n    float forgot = 1;\n};\n");
    CHECK(Has(r, "RM0001", 4));
    CHECK(r.diags.size() == 1);
    CHECK(r.diags[0].Text().starts_with("T.h(4,11): error RM0001: "));
}

RM_TEST(Rmgen_T6_ErrorsPointAtTheUsersLine) {
    const Result r = Run(R"(RM_STRUCT()
struct A {
    RM_PROPERTY(Mim=0) float a = 0;
    RM_PROPERTY(Label=Name) int b = 0;
    RM_PROPERTY(Min=kMax) int c = 0;
    RM_PROPERTY() double d = 0;
    RM_PROPERTY() int* e = nullptr;
    RM_PROPERTY() Unknown f;
    RM_PROPERTY() std::vector<std::vector<int>> g;
    RM_PROPERTY(Min=5, Max=1) int h = 0;
    RM_PROPERTY(Min=0) std::string i;
    RM_PROPERTY(Edit=1) bool j = false;
private:
    RM_PROPERTY() int k = 0;
};
RM_PROPERTY() int stray = 0;
)");
    CHECK(Has(r, "RM0002", 3));   // unknown key (typo)
    CHECK(Has(r, "RM0003", 4));   // Label needs a string
    CHECK(Has(r, "RM0003", 5));   // Min must be a literal, not a constant's name
    CHECK(Has(r, "RM0004", 6));   // double
    CHECK(Has(r, "RM0004", 7));   // pointer
    CHECK(Has(r, "RM0006", 8));   // not a reflected type
    CHECK(Has(r, "RM0008", 9));   // array of arrays
    CHECK(Has(r, "RM0003", 10));  // Min > Max
    CHECK(Has(r, "RM0003", 11));  // Min on a string
    CHECK(Has(r, "RM0003", 12));  // a flag with a value
    CHECK(Has(r, "RM0011", 13));  // private
    CHECK(Has(r, "RM0005", 16));  // annotation outside a reflected struct
}

RM_TEST(Rmgen_DuplicateNamesAndBases) {
    std::vector<Diagnostic> diags;
    std::vector<Header> hs;
    hs.push_back(Scan("namespace n { RM_STRUCT() struct A { RM_PROPERTY() int x = 0; }; }", "A.h", "A.h", diags));
    hs.push_back(Scan("namespace n { RM_STRUCT() struct A : Base { RM_PROPERTY() int y = 0; }; }", "B.h", "B.h", diags));
    Validate(hs, diags);
    bool dup = false, base = false;
    for (const auto& d : diags) {
        dup |= d.code == "RM0009" && d.file == "B.h";
        base |= d.code == "RM0011" && d.file == "B.h";
    }
    CHECK(dup && base);
}

RM_TEST(Rmgen_NamesResolveLikeCpp) {
    std::vector<Diagnostic> diags;
    std::vector<Header> hs;
    hs.push_back(Scan("namespace a { RM_ENUM() enum class E { X }; namespace b { RM_STRUCT() struct S { RM_PROPERTY() E e; RM_PROPERTY() a::E f; }; } }",
                      "A.h", "A.h", diags));
    Validate(hs, diags);
    CHECK(diags.empty());
    CHECK(hs[0].structs[0].fields[0].type.resolved == "a::E");
    CHECK(hs[0].structs[0].fields[1].type.resolved == "a::E");
    const std::string all = EmitAll(hs);
    CHECK(all.find("&rmgen_generated::T_a__b__S") != std::string::npos);
    CHECK(all.find("&rmgen_generated::E_a__E") != std::string::npos);
}

RM_TEST(Rmgen_References) {
    const Result r = Run(R"(RM_STRUCT()
struct Links {
    RM_PROPERTY(Edit, AssetKind="model") rm::AssetRef model;
    RM_PROPERTY(Edit) EntityRef target;
    RM_PROPERTY(Edit, AssetKind="image,model") std::vector<AssetRef> textures;
};
)");
    CHECK(r.diags.empty());
    const Struct& s = r.headers[0].structs[0];
    CHECK(s.fields[0].type.kind == TypeRef::Kind::AssetRef && s.fields[1].type.kind == TypeRef::Kind::EntityRef);
    CHECK(s.fields[2].type.element[0].kind == TypeRef::Kind::AssetRef);
    const std::string out = EmitHeader(r.headers, r.headers[0]);
    CHECK(out.find("FieldType::AssetRef") != std::string::npos && out.find("FieldType::EntityRef") != std::string::npos);
    CHECK(out.find(".assetKinds = \"model\"") != std::string::npos && out.find(".assetKinds = \"image,model\"") != std::string::npos);
    CHECK(out.find("decltype(::Links::model), ::rm::AssetRef>") != std::string::npos);

    // AssetKind only on an AssetRef, and only lower-case names with commas.
    const Result bad = Run(R"(RM_STRUCT()
struct A {
    RM_PROPERTY(AssetKind="model") int a = 0;
    RM_PROPERTY(AssetKind="Model") AssetRef b;
    RM_PROPERTY(AssetKind="image,,model") AssetRef c;
    RM_PROPERTY(AssetKind=model) AssetRef d;
    RM_PROPERTY(Min=0) EntityRef e;
};
)");
    CHECK(Has(bad, "RM0003", 3));
    CHECK(Has(bad, "RM0003", 4));
    CHECK(Has(bad, "RM0003", 5));
    CHECK(Has(bad, "RM0003", 6));
    CHECK(Has(bad, "RM0003", 7));
}
