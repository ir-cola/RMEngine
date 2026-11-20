#pragma once
// rmgen: RM_STRUCT / RM_ENUM ... (core/reflect/Reflect.h) のあるヘッダーを読んで、反射の表を
// 書く。純粋 (文字を入れて文字を出す) なので、テストがヘッダーを直接渡せる。
//
// エラーは   <file>(<line>,<col>): error RM####: <日本語>   の形で出す (Visual Studio でクリックでき、
// MSBuild もビルドエラーとして数える)。黙って飛ばすものはない: 反射する型の中で rmgen が扱えない
// ものは、「無視」ではなくすべてエラー。
//
//   RM0001  RM_PROPERTY / RM_TRANSIENT のないフィールド
//   RM0002  知らない属性のキー
//   RM0003  属性の値の形が違う (数は literal でなければならない)、Min > Max、数でないものに Min、
//           AssetRef でない・小文字の単語の一覧でないものに AssetKind、
//           構造体のほかのフィールドを指していない ShowIf
//   RM0004  反射できない型 (ポインター、double、const、C の配列 ...)
//   RM0005  何の働きもない場所の注釈
//   RM0006  RM_STRUCT / RM_ENUM でない型の名前
//   RM0007  rmgen が分からない構文
//   RM0008  配列の配列
//   RM0009  同じ型の名前が 2 回
//   RM0010  フィールドが 64 個より多い
//   RM0011  private / protected / 基底クラス (反射する型は素の public の構造体)
//   RM0012  同じ生成ファイルを作る (または同じ include のパスを持つ) 2 つのヘッダー
//   RM0013  (スクリプト) スクリプトの DLL がまだ持てないもの: RM_STRUCT / RM_ENUM、一覧のフィールド、
//           enum、構造体、AssetRef
//   RM0014  反射する構造体の中の #if / #ifdef の中のフィールド (ビルドでフィールドが変わってしまう。
//           #if 0 ... #endif は飛ばすので問題ない)

#include <string>
#include <string_view>
#include <vector>

namespace rmgen {

struct Diagnostic {
    std::string file;
    int line = 0, col = 0;
    std::string code;     // "RM0001"
    std::string message;  // 日本語、UTF-8
    std::string Text() const;
};

struct Attr {
    std::string key;
    std::string value;  // 文字列リテラル: 引用符の間の文字を、書いたまま
    enum class Kind { Flag, String, Number, Ident } kind = Kind::Flag;
    int line = 0, col = 0;
};

struct TypeRef {
    enum class Kind { Bool, Int, Float, String, Vec2, Vec3, Color, AssetRef, EntityRef, Named, Vector } kind = Kind::Int;
    std::string name;               // Named: 書いたまま ("Quality"、"game::Quality")
    std::vector<TypeRef> element;   // Vector: ちょうど 1 つ
    std::string resolved;           // Named、Validate の後: 修飾名 ("rm::game::Quality")
    bool isEnum = false;            // Named、Validate の後
    std::string assetType;          // スクリプトの AssetRef: "PrefabRef" / "ImageRef" / "ModelRef" (RM.h の)
};

struct Field {
    std::string name;
    std::string cppType;  // 書いたまま。コンパイル時の型の検査用
    TypeRef type;
    bool transient = false;
    std::vector<Attr> attrs;
    int line = 0, col = 0;
};

struct EnumItem {
    std::string name;
    std::string label;  // リテラルの文字そのまま。空 = なし
    int line = 0;
};

struct Enum {
    std::string name, ns;  // ns: "rm::game" (空 = グローバル)
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
    std::vector<Field> fields;  // すべてのデータメンバーを順に (transient なものも含む)
    std::string Qualified() const { return ns.empty() ? name : ns + "::" + name; }
};

struct Header {
    std::string file;         // メッセージ用 (渡されたまま)
    std::string includePath;  // "rm_engine/game_production/runtime/GameSettings.h"
    std::vector<Struct> structs;
    std::vector<Enum> enums;
};

// ヘッダーを 1 つ読む。問題は diags に足す。
Header Scan(std::string_view text, const std::string& file, const std::string& includePath, std::vector<Diagnostic>& diags);
// プログラム全体を調べる: 型の名前、属性、上限。TypeRef::resolved を埋める。
void Validate(std::vector<Header>& headers, std::vector<Diagnostic>& diags);
// ヘッダー 1 つの生成 .cpp と、すべての型の表。
std::string EmitHeader(const std::vector<Header>& all, const Header& h);
std::string EmitAll(const std::vector<Header>& all);
// スクリプト (rmgen --scripts: ゲームの Assets/Scripts のヘッダー): RM_SCRIPT だけ、RM.h が持つ
// フィールドの型だけ。スクリプトの DLL の生成 .cpp 1 つ: スクリプトの表 (RmAbi.h) と
// RmModuleInit。
void ValidateScripts(const std::vector<Header>& headers, std::vector<Diagnostic>& diags);
std::string EmitScripts(const std::vector<Header>& all);
// include のパスから作る生成 .cpp のファイル名: 読みやすく、パス全体のハッシュもつけて 2 つの
// パスがぶつからないようにする ("a/b_c/D.h" と "a/b/c/D.h" がどちらも a_b_c_D になる): "a/b/C.h" -> "a_b_C.<8 hex>.reflect.cpp"。
// Validate は、Windows と同じく大文字小文字を無視して、すべての組をなお調べる (RM0012)。
std::string OutputName(const std::string& includePath);

}  // namespace rmgen
