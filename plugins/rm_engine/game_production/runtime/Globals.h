#pragma once
// ゲームのグローバル変数と、その保存 (設計 TestEngine NewEngineBluePrint 6: 名前空間のアクセサー -
// Global::score - #define は決して使わない。RME_PLATFORM 4-b: 入力 / グローバル変数 + セーブ)。
// ランタイム側: エディターの Play と書き出したゲームは、ScriptWorld の中に (ゲーム全体で 1 つ:
// シーンを切り替えても残る)、同じやり方で持つ。
//
//   GlobalStore::Reset(settings.globals)   Play と Stop のとき (エディター) / ゲームが始まるとき:
//                                          すべての値が 初めの値 に戻る
//   スクリプトが名前で読み書きする         (部品 "rm_engine.game_production.globals"。スクリプトでは
//                                          Global::score。GlobalsHeader が書くヘッダーから)
//   WriteSave / ReadSave (スロット)        <folder>/save_<slot>.json。WriteFileAtomic で書く:
//       { "kind": "rm_engine.save", "format": 1, "globals": { "score": 3, "name": "A", "pos": [1, 2] } }
//     書くときは、そこにあるファイル (なければ最後に読んだもの) から始める: このバージョンが知らない
//     キー - 一覧からなくなった変数や、別のメンバー - は、そのまま書き戻す。
//   世界のセーブ (rm::Save::WriteWorld。WorldSave.h、docs/SAVE.md) は同じファイルのキー "world" に
//   入る。WriteSave は `world` を渡されたときだけそれを書き換え (前の "world" の知らないキーは残す)、
//   渡されなければ "world" はそのまま残る。
//
// 場所: エディターの Play は <project>/Library/Saves/ (Assets の中には決して置かない)、書き出した
// ゲームは %LOCALAPPDATA%\<exe name>\Saves\。

#include "base/Json.h"
#include "rm_engine/game_production/runtime/GameSettings.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rm::game_production {

struct GlobalValue {
    GlobalType type = GlobalType::Int;
    double v[3]{};  // Bool (0 / 1)、Int (整数)、Float、Vec2 (2)、Vec3 (3)
    std::string s;  // 文字列
};

// 設定に書かれた 初めの値 ("true"、"3"、"1.5"、"text"、"1, 2, 3")。"" はゼロ (空の文字列)。
// 型に合わないとき (int に "abc") は nullopt。
std::optional<GlobalValue> ParseGlobalValue(GlobalType type, std::string_view text);
// 値を文字にしたもの (ParseGlobalValue が読み戻せる形): "true"、"3"、"1.5"、"1, 2, 3"。
std::string FormatGlobalValue(const GlobalValue& v);
json::Value GlobalToJson(const GlobalValue& v);
// セーブから読んだ値を、変数の型にしたもの (float に数: よい。数に文字: nullopt)。
std::optional<GlobalValue> GlobalFromJson(GlobalType type, const json::Value& j);
// スクリプトが Global::<name> として使える名前: ASCII の英字・数字・_ で、数字で始まらず、
// C++ のキーワードでもない。
bool IsGlobalName(std::string_view name);

class GlobalStore {
public:
    // 一覧のすべての変数を 初めの値 で (合わないもの: ゼロにして、警告を出す)。同じ名前が
    // 2 つあるとき: 最初のもの。
    void Reset(std::span<const GlobalVariable> list, std::vector<std::string>* warnings = nullptr);
    GlobalValue* Find(std::string_view name);
    const GlobalValue* Find(std::string_view name) const;
    size_t Size() const { return values_.size(); }
    // すべての変数 (名前、値)、一覧の順。
    const std::vector<std::pair<std::string, GlobalValue>>& Values() const { return values_; }
    // セーブの文書: `base` (ファイルのもの、または最後に読んだもの) に、"kind"、"format"、すべての変数を設定したもの。
    json::Value ToSave(json::Value base) const;
    // セーブの文書から変数を取り込む (他はそのまま)。セーブでないときは false。
    bool FromSave(const json::Value& doc, std::vector<std::string>* warnings);

    json::Value lastRead;  // 最後に読んだ文書 (知らないキーは、新しいスロットに一緒に行く)

private:
    std::vector<std::pair<std::string, GlobalValue>> values_;
};

inline constexpr char kSaveKind[] = "rm_engine.save";
inline constexpr char kWorldSaveKey[] = "world";  // 世界のセーブ (WorldSave.h) が入るキー
inline constexpr int64_t kSaveFormat = 1;

// スロット: "0"、"auto"、"slot-2" (ASCII の英字・数字・_ と -、1 から 64 文字): パスには決してしない。
bool IsSaveSlot(std::string_view slot);
std::filesystem::path SaveFile(const std::filesystem::path& folder, std::string_view slot);
// `world`: 世界のセーブ (CaptureWorld) も "world" に書く。null: グローバル変数だけ。
bool WriteSave(GlobalStore& store, const std::filesystem::path& folder, std::string_view slot, std::string* why,
               const json::Value* world = nullptr);
bool ReadSave(GlobalStore& store, const std::filesystem::path& folder, std::string_view slot, std::vector<std::string>* warnings,
              std::string* why);
bool SaveExists(const std::filesystem::path& folder, std::string_view slot);
// セーブのファイルの中身そのもの (rm_engine.save であること)。読めない / 無い / スロットが使えない: nullopt と *why。
std::optional<json::Value> ReadSaveDoc(const std::filesystem::path& folder, std::string_view slot, std::string* why);
bool DeleteSave(const std::filesystem::path& folder, std::string_view slot, std::string* why);

// <project>/Library/Saves (エディターの Play)
std::filesystem::path ProjectSaveFolder(const std::filesystem::path& project);
// %LOCALAPPDATA%\<exe name>\Saves (書き出したゲーム。exe の名前から .exe を除いたもの)
std::filesystem::path GameSaveFolder(std::wstring_view exeName);

}  // namespace rm::game_production
