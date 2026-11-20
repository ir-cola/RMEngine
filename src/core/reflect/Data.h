#pragma once
// リフレクションの型は、その記述から何でももらえる: 保存、読み込み (範囲の丸めと「知らないものを
// 残す」付き)、比較、差分、数の入力の決まり。それぞれ関数 1 つが TypeDesc をたどる - どれも
// 型ごとのコピーはない (CLAUDE.md の憲法 2 と 3)。
//
// ファイルはフィールド名をキーにした JSON のオブジェクト。enum は名前で保存する (列挙子を挟んだり
// 並べ替えたりしても、古いファイルの意味は変わらない)。Vec2 / Vec3 / Color は数の配列。

#include "base/Json.h"
#include "core/reflect/Reflect.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rm::reflect {

// ファイルにあって、このバージョンが分からないもの (設計 R1 4-f / BluePrint 10-b)。
// オブジェクトの隣に保ち、保存でそのまま書き戻すので、新しいバージョンのファイル (またはプラグインが
// ないファイル) を開いて保存しても、データは失われない。
struct Unknowns {
    struct Nested;
    std::vector<json::Value::Member> keys;                   // 知らないキー、ファイルの順
    std::vector<std::pair<std::string, std::string>> enums;  // フィールド -> このバージョンにない enum の名前
    // フィールド -> このバージョンのフィールドと別の種類の値 (新しいバージョンが変えた: 数が
    // リストになった、など): フィールドは既定値で動き、値はフィールドが設定されるまで、
    // そのまま書き戻される (設計 R1 4-f: 読めないものは残す)。
    std::vector<json::Value::Member> kept;
    // 配列のフィールド -> 読んだときの要素数。その要素の unknowns ("field[3]") はその場所のもの -
    // その後に数が変わった (ツールが足した、消した) ときは、他の要素には書かない。
    std::vector<std::pair<std::string, size_t>> lengths;
    std::vector<Nested> children;                            // "field" (構造体) / "field[3]" (配列の要素)

    bool Empty() const;
    Unknowns* Child(const std::string& key, bool create);
    const Unknowns* Child(const std::string& key) const;
    const std::string* Enum(std::string_view field) const;
    void ForgetEnum(std::string_view field);
    const json::Value* Kept(std::string_view field) const;
    void ForgetKept(std::string_view field);
};

struct Unknowns::Nested {
    std::string key;
    Unknowns value;
};

// コンソール用のメッセージ (日本語、UTF-8)。たとえば範囲外の値や、知らない enum の名前。
using Warnings = std::vector<std::string>;

// String フィールドのテキストと、それを設定する - 文字列が別のモジュールのものなら
// FieldDesc::stringAccess を通る (どの文字列フィールドもこの 2 つを通る)。
std::string ReadString(const FieldDesc& f, const void* field);
void WriteString(const FieldDesc& f, void* field, std::string_view text);
// AssetRef フィールドの id ("" = なし) と、それを設定する - 別の形 (スクリプトのもの) で
// 持っているなら FieldDesc::stringAccess を通る。
std::string ReadAssetId(const FieldDesc& f, const void* field);
void WriteAssetId(const FieldDesc& f, void* field, std::string_view id);

json::Value Save(const TypeDesc& type, const void* object, const Unknowns* unknowns = nullptr);
// ファイルにないフィールドは今の値を保つ (だから: 既定のまま作ってから Load する)。
void Load(const TypeDesc& type, void* object, const json::Value& v, Unknowns* unknowns = nullptr,
          Warnings* warnings = nullptr);

// 1 つのフィールド (`field.Get(object)` にある値)。`unknowns` はオブジェクトのもの (enum の名前はそこにある)。
json::Value SaveField(const FieldDesc& field, const void* object, const Unknowns* unknowns);
void LoadField(const FieldDesc& field, void* object, const json::Value& v, Unknowns* unknowns, Warnings* warnings);

// `object` の中 (入れ子の構造体や配列も) の、設定されている EntityRef すべて: id = map(id)
// (0 は空にする)。エンティティのコピーが、コピーのほうを指すために使う (貼り付け、Prefab)。
void MapEntityRefs(const TypeDesc& type, void* object, const std::function<uint64_t(uint64_t)>& map);

bool Equals(const TypeDesc& type, const void* a, const void* b);
// ビット i が立つ = フィールド i が違う (transient は決して)。フィールドは最大 64 個 (rmgen が調べる)。
uint64_t Diff(const TypeDesc& type, const void* a, const void* b);

// 数がフィールドに入るときの決まり。読み込みもインスペクターも同じものを使う:
// 有限でない -> fallback (今の値)。それから Min / Max に収める。Int フィールドは丸める。
double SanitizeNumber(const FieldDesc& field, double v, double fallback);
// ユーザーが数のフィールドに打ち込んだもの: 数でなければ nullopt。
std::optional<double> ParseNumber(std::string_view text);
// SanitizeNumber を通した後の、打ち込んだテキスト - インスペクターの経路で、ローダーのものと
// 突き合わせてテストする (T3)。
std::optional<double> NumberFromInput(const FieldDesc& field, std::string_view text, double current);
std::string FormatNumber(const FieldDesc& field, double v);

// 入れ子の構造体の中のフィールド: fields への添字、外側から順に。
using FieldPath = std::vector<size_t>;
struct PathTarget {
    const FieldDesc* field = nullptr;
    void* owner = nullptr;          // フィールドを持つ構造体
    Unknowns* unknowns = nullptr;   // その構造体の unknowns (保っていなければ null)
};
PathTarget Resolve(const TypeDesc& type, void* object, Unknowns* unknowns, const FieldPath& path);
std::string PathText(const TypeDesc& type, const FieldPath& path);  // "window.width"

const EnumItem* FindItem(const EnumDesc& e, int64_t value);
// インスペクターが `object` (`owner`) の `f` を表示するか: その ShowIf が成り立つか (ShowIf なし: true)。
// フィールドを指さない項や、その enum にない項目は false (何が悪いかは ShowIfProblem が言う)。
bool FieldShown(const TypeDesc& owner, const FieldDesc& f, const void* object);
// ShowIf の何がおかしいか (空: 何も) - テストが全部の表を調べる。
std::string ShowIfProblem(const TypeDesc& owner, const FieldDesc& f);
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
