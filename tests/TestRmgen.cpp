// ジェネレーター (tools/rmgen/Scanner) のテスト: コード付きのエラーとユーザーの行番号
// (設計 R1 T4 / T6)、そしてヘッダーから読むもの。

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
    CHECK(OutputName("a/b/C.h").starts_with("a_b_C.") && OutputName("a/b/C.h").ends_with(".reflect.cpp"));
}

RM_TEST(Rmgen_OutputNamesNeverMeet) {
    // '/' を '_' にすると同じ読みになるパスでも、別のファイルになる (パス全体のハッシュ)。
    CHECK(OutputName("rm_engine/game_x_3d/A.h") != OutputName("rm_engine/game_x/3d/A.h"));
    CHECK(OutputName("core/x/A.h") != OutputName("core_x/A.h"));
    CHECK(OutputName("a/b/C.h") == OutputName("a\\b\\C.h"));  // どちらの書き方でも同じパス
    CHECK(OutputName("a/b/C.h") == OutputName("a/b/C.h"));    // ビルドごとにも同じ
    CHECK(OutputName("a/b/C.h").size() == std::string("a_b_C.12345678.reflect.cpp").size());

    // 同じ include パスのヘッダーが 2 つ (src/core の隣に "core" という作者のプラグイン。または
    // 大文字小文字だけ違う - Windows では 1 つのファイル): rmgen は片方をもう片方に上書きせず、止まる (RM0012)。
    const char* text = "RM_STRUCT()\nstruct A {\n    RM_PROPERTY() int a = 0;\n};\n";
    const char* other = "RM_STRUCT()\nstruct B {\n    RM_PROPERTY() int b = 0;\n};\n";
    std::vector<Diagnostic> diags;
    std::vector<Header> headers;
    headers.push_back(Scan(text, "src/core/x/A.h", "core/x/A.h", diags));
    headers.push_back(Scan(other, "plugins/core/x/A.h", "core/x/A.h", diags));
    Validate(headers, diags);
    bool found = false;
    for (const auto& d : diags) found = found || (d.code == "RM0012" && d.file == "plugins/core/x/A.h");
    CHECK(found);
    diags.clear();
    headers.clear();
    headers.push_back(Scan(text, "1/A.h", "game/A.h", diags));
    headers.push_back(Scan(other, "2/A.h", "Game/A.h", diags));
    Validate(headers, diags);
    found = false;
    for (const auto& d : diags) found = found || d.code == "RM0012";
    CHECK(found);
    // 別のパス: エラーなし。
    diags.clear();
    headers.clear();
    headers.push_back(Scan(text, "1/A.h", "rm_engine/game_x_3d/A.h", diags));
    headers.push_back(Scan(other, "2/A.h", "rm_engine/game_x/3d/A.h", diags));
    Validate(headers, diags);
    CHECK(diags.empty());
}

// #if 0 は飛ばす。struct の中の別の #if / #ifdef にある欄はエラー (数がビルドで変わるため)。
// マクロの \ の継続は CRLF でも働く (2026-11-19)。
RM_TEST(Rmgen_Preprocessor) {
    const Result off = Run("RM_STRUCT()\nstruct A {\n    RM_PROPERTY() int a = 0;\n#if 0\n    RM_PROPERTY() int b = 0;\n    junk }{ ;\n#endif\n};\n");
    CHECK(off.diags.empty() && off.headers[0].structs.size() == 1 && off.headers[0].structs[0].fields.size() == 1);
    const Result debug = Run("RM_STRUCT()\nstruct A {\n    RM_PROPERTY() int a = 0;\n#ifdef _DEBUG\n    RM_PROPERTY() int b = 0;\n#endif\n};\n");
    CHECK(Has(debug, "RM0014", 5));
    // ガードの中に丸ごと入った struct は問題ない。
    const Result guard = Run("#ifndef A_H\n#define A_H\nRM_STRUCT()\nstruct A {\n    RM_PROPERTY() int a = 0;\n};\n#endif\n");
    CHECK(guard.diags.empty() && guard.headers[0].structs.size() == 1);
    // CRLF: マクロの継続した行はマクロのもので、コードではない。
    const Result crlf = Run("#define TWO(a) \\\r\n    struct a { int x; };\r\nRM_STRUCT()\r\nstruct B {\r\n    RM_PROPERTY() int b = 0;\r\n};\r\n");
    CHECK(crlf.diags.empty() && crlf.headers[0].structs.size() == 1 && crlf.headers[0].structs[0].name == "B");
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
    CHECK(Has(r, "RM0002", 3));   // 知らないキー (打ち間違い)
    CHECK(Has(r, "RM0003", 4));   // Label は文字列が要る
    CHECK(Has(r, "RM0003", 5));   // Min はリテラルで、定数の名前ではない
    CHECK(Has(r, "RM0004", 6));   // double
    CHECK(Has(r, "RM0004", 7));   // ポインター
    CHECK(Has(r, "RM0006", 8));   // 反映された型ではない
    CHECK(Has(r, "RM0008", 9));   // 配列の配列
    CHECK(Has(r, "RM0003", 10));  // Min > Max
    CHECK(Has(r, "RM0003", 11));  // 文字列に Min
    CHECK(Has(r, "RM0003", 12));  // 値の付いたフラグ
    CHECK(Has(r, "RM0011", 13));  // private
    CHECK(Has(r, "RM0005", 16));  // 反映された struct の外の注釈
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

    // AssetKind は AssetRef にだけ、そして小文字の名前をカンマで区切ったものだけ。
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

RM_TEST(Rmgen_Decimals) {
    // Decimals=0 .. 6 は float、ベクトル、色に付けられる。表に書く。
    const Result ok = Run(R"(#pragma once
RM_STRUCT()
struct S {
    RM_PROPERTY(Edit, Decimals=1) float x = 0;
    RM_PROPERTY(Edit) float y = 0;
};
)");
    CHECK(ok.diags.empty());
    const std::string out = EmitHeader(ok.headers, ok.headers[0]);
    CHECK(out.find(".decimals = 1}") != std::string::npos && out.find(".decimals = -1}") != std::string::npos);
    const Result bad = Run(R"(#pragma once
RM_STRUCT()
struct S {
    RM_PROPERTY(Edit, Decimals=1) int i = 0;
    RM_PROPERTY(Edit, Decimals=7) float a = 0;
    RM_PROPERTY(Edit, Decimals=0.5) float b = 0;
    RM_PROPERTY(Edit, Decimals=1) bool c = false;
};
)");
    CHECK(Has(bad, "RM0003", 4) && Has(bad, "RM0003", 5) && Has(bad, "RM0003", 6) && Has(bad, "RM0003", 7));
}

RM_TEST(Rmgen_Icon) {
    // Icon="絵の名前": インスペクターの項目名の前の絵。表に書く。名前は英字と数字だけ。
    const Result ok = Run(R"(#pragma once
RM_STRUCT()
struct S {
    RM_PROPERTY(Edit, Icon="RotationLock") bool lockX = false;
    RM_PROPERTY(Edit) bool other = false;
};
)");
    CHECK(ok.diags.empty());
    const std::string out = EmitHeader(ok.headers, ok.headers[0]);
    CHECK(out.find(".icon = \"RotationLock\"") != std::string::npos && out.find(".icon = nullptr") != std::string::npos);
    const Result bad = Run(R"(#pragma once
RM_STRUCT()
struct S {
    RM_PROPERTY(Edit, Icon="") bool a = false;
    RM_PROPERTY(Edit, Icon="Rotation Lock") bool b = false;
    RM_PROPERTY(Edit, Icon=3) bool c = false;
};
)");
    CHECK(Has(bad, "RM0003", 4) && Has(bad, "RM0003", 5) && !bad.diags.empty());
}

RM_TEST(Rmgen_ShowIf) {
    // ShowIf は同じ struct の他の欄を指す ("kind=A|B", "flag", "!flag", & は両方)。
    const Result ok = Run(R"(#pragma once
RM_ENUM() enum class K { A, B };
RM_STRUCT()
struct S {
    RM_PROPERTY(Edit) K kind = K::A;
    RM_PROPERTY(Edit) bool on = false;
    RM_PROPERTY(Edit, ShowIf="kind=A|B&!on") float x = 0;
};
)");
    CHECK(ok.diags.empty());
    CHECK(EmitHeader(ok.headers, ok.headers[0]).find(".showIf = \"kind=A|B&!on\"") != std::string::npos);
    const Result bad = Run(R"(#pragma once
RM_STRUCT()
struct S {
    RM_PROPERTY(Edit) bool on = false;
    RM_PROPERTY(Edit, ShowIf="nothing") float x = 0;
    RM_PROPERTY(Edit, ShowIf="on=") float y = 0;
    RM_PROPERTY(Edit, ShowIf="on&") float z = 0;
    RM_PROPERTY(Edit, ShowIf="w") float w = 0;
};
)");
    CHECK(Has(bad, "RM0003", 5) && Has(bad, "RM0003", 6) && Has(bad, "RM0003", 7) && Has(bad, "RM0003", 8));  // (w: 自分自身)
}
