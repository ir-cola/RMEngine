#pragma once
// リフレクション (設計 R1、TestEngine docs/REMAKE_R1_REFLECT.md): データの型は、自分のヘッダーに 1 回だけ書く。
// 保存・読み込み・インスペクター・比較・コピー・差分・Undo・範囲の丸めは、その記述から全部作る。
// フィールドについて同じことを 2 回書かない。
//
//   RM_STRUCT(Label="ゲームの設定")
//   struct GameSettings {
//       RM_PROPERTY(Edit, Label="目標 FPS", Min=1, Max=240) int targetFps = 60;
//       RM_PROPERTY(Edit, Label="名前")                     std::string title = "My Game";
//       RM_TRANSIENT()                                       float timer = 0;   // 保存も表示もしない
//   };
//
//   RM_ENUM() enum class Quality { RM_ITEM(Label="低") Low, Medium, High };
//
// マクロは何にも展開されない。tools/rmgen がビルドのたびに先にヘッダーを読み、
// build/generated/<project>/*.reflect.cpp (ヘッダーを include する。ヘッダーは生成物を何も include しない) と
// rm_all_types.cpp (全部の型の表。自己登録はしない。静的ライブラリだと黙って捨てられるため) を書く。
// 生成されたコードは、ヘッダーとまだ合っているかをコンパイル時にも確かめる (フィールドの数・名前・型)。
// 再生成せずにヘッダーを直すと、ビルドが「rmgen を走らせ直してください」で止まる。
//
// リフレクトする構造体のフィールドは、全部 RM_PROPERTY か RM_TRANSIENT にする (エラー RM0001)。
// RM_PROPERTY の属性: Edit, ReadOnly, Label="", Tooltip="", Min=, Max=, Step= (数値だけ),
// Decimals=1 (float・ベクトル・色。その桁数までに丸める。残りは切り捨てで、四捨五入ではない。
// 表示もその桁数で: "12.3"),
// AssetKind="model" / "image,model" (AssetRef だけ。受け取るファイルの種類 - core/asset),
// ShowIf="kind=Show|Hide" / "useMin" / "!loop" (両方なら &): 同じ構造体の別のフィールドが、それらの
// 項目のどれかである / オン / オフのときだけ、インスペクターに出す (FieldShown, Data.h)。
// リストの要素なら、その種類のときだけ、その種類のフィールドを出す。隠しても保存と読み込みはされる。
//
// 型: bool, int, float, std::string, Vec2, Vec3, Color, AssetRef, EntityRef (base/Ref.h: id で保存)、
// RM_ENUM の列挙型 (名前で保存)、RM_STRUCT の構造体 (入れ子)、std::vector<これらのどれか>。

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
    const char* name;   // ファイルに保存する名前
    const char* label;  // 表示する文字 (UTF-8)。RM_ITEM(Label=) が無いときは名前
    int64_t value;
};

struct EnumDesc {
    const char* name;       // C++ の名前 (修飾つき)
    std::span<const EnumItem> items;
    int64_t (*get)(const void* p);
    void (*set)(void* p, int64_t v);
    const std::type_info& (*cppType)();
};

// 値の型。フィールド、または配列フィールドの要素。
struct ValueType {
    FieldType type = FieldType::Int;
    const TypeDesc* structType = nullptr;  // 構造体
    const EnumDesc* enumType = nullptr;    // 列挙型
};

struct ArrayOps {
    ValueType element;
    size_t (*size)(const void* vec);
    void (*resize)(void* vec, size_t n);
    void* (*at)(void* vec, size_t i);
};

// 別のモジュール (自分の C ランタイムでビルドされたスクリプト DLL) にある std::string。そのモジュールの
// 関数を通してだけ読み書きし、このプログラムからは代入も解放もしない (メモリは相手のランタイムのもの)。
// AssetRef のフィールドなら、別の形で持つ id (スクリプトの char guid[...])。同じようにテキストとして読み書きする。
struct StringAccess {
    const void* ctx;
    size_t (*get)(const void* ctx, const void* field, char* buf, size_t cap);  // 全体の長さを返す。入る分だけコピーする (末尾は 0)
    void (*set)(const void* ctx, void* field, const char* utf8, size_t len);
};

struct FieldDesc {
    const char* name;  // C++ の名前 = ファイルでのキー
    ValueType value;
    void* (*access)(void* object);  // オブジェクトの中のフィールド
    const ArrayOps* array = nullptr;  // 配列
    bool transient = false;           // RM_TRANSIENT: 保存しない・表示しない・比べない
    bool edit = false;                // インスペクターに表示する
    bool readOnly = false;
    const char* label = nullptr;      // UTF-8
    const char* tooltip = nullptr;
    bool hasMin = false, hasMax = false, hasStep = false;
    double min = 0, max = 0, step = 0;
    int line = 0;  // ヘッダーの中の行 (メッセージ用)
    const char* assetKinds = nullptr;  // AssetRef: AssetKind="..." (null: どのファイルでも)
    const StringAccess* stringAccess = nullptr;  // 別のモジュール (スクリプト) の文字列: ReadString / WriteString
    const char* showIf = nullptr;  // ShowIf="..." (null: いつも表示)
    int decimals = -1;             // Decimals=N: N 桁までにして、残りは切り捨てる (-1: そのまま)

    const void* Get(const void* object) const { return access(const_cast<void*>(object)); }
    void* Get(void* object) const { return access(object); }
    const char* Label() const { return label ? label : name; }
};

enum class TypeKind : uint8_t { Struct, Component, Script };

struct TypeDesc {
    const char* name;   // C++ の名前 (修飾つき)
    const char* label;  // UTF-8 (RM_STRUCT(Label=))。無いときは飾りのない名前
    TypeKind kind;
    size_t size;
    std::span<const FieldDesc> fields;
    void (*construct)(void* p);  // 既定のメンバー値で placement new する
    void (*destruct)(void* p);
    void (*copy)(void* dst, const void* src);
    const std::type_info& (*cppType)();
    const char* header;  // 宣言されているヘッダー (メッセージ用)
};

// このプログラムのリフレクトする型すべて (rmgen が rm_all_types.cpp に書く)。
std::span<const TypeDesc* const> AllTypes();
std::span<const EnumDesc* const> AllEnums();

const TypeDesc* FindType(const std::type_info& t);
const EnumDesc* FindEnum(const std::type_info& t);

template <class T>
const TypeDesc& TypeOf() {
    static const TypeDesc* d = FindType(typeid(T));
    return *d;  // RM_STRUCT / RM_COMPONENT / RM_SCRIPT の無い型には記述が無い
}

// --- 生成コードが使うコンパイル時のチェック ----------------------------------------

namespace detail {

// どのフィールドの型にも変換できる (左辺値参照として。この変換だけが候補になる)。
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

// 集成体の構造体のメンバーの数 (リフレクトする構造体はみな集成体)。
template <class T>
constexpr size_t FieldCount() {
    return detail::CountFields<T, 0>();
}

}  // namespace rm::reflect
