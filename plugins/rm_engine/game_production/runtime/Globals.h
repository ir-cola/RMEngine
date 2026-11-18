#pragma once
// Global variables of a game and saving them (design TestEngine NewEngineBluePrint 6: accessors in a
// namespace - Global::score - never #define; RME_PLATFORM 4-b: input / global variables + save). The
// RUNTIME part: the editor's Play and the exported game keep them the same way, in the ScriptWorld (one
// for the whole game: switching the scene keeps them).
//
//   GlobalStore::Reset(settings.globals)   at Play and at Stop (editor) / when the game starts: every
//                                          value back to its 初めの値
//   the scripts read / write them by name  (the part "rm_engine.game_production.globals"; in a script
//                                          Global::score, from the header GlobalsHeader writes)
//   WriteSave / ReadSave (a slot)          <folder>/save_<slot>.json, written with WriteFileAtomic:
//       { "kind": "rm_engine.save", "format": 1, "globals": { "score": 3, "name": "A", "pos": [1, 2] } }
//     A write starts from the file that is there (else the last one read): keys this version does not
//     know - a variable no longer in the list, another member - are written back as they were.
//
// Where: the editor's Play <project>/Library/Saves/ (never in Assets), an exported game
// %LOCALAPPDATA%\<exe name>\Saves\.

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
    double v[3]{};  // Bool (0 / 1), Int (whole), Float, Vec2 (2), Vec3 (3)
    std::string s;  // String
};

// A 初めの値 as written in the settings ("true", "3", "1.5", "text", "1, 2, 3"); "" is zero (an empty
// text). nullopt when it does not fit the type ("abc" for an int).
std::optional<GlobalValue> ParseGlobalValue(GlobalType type, std::string_view text);
// The value as text (what ParseGlobalValue reads back): "true", "3", "1.5", "1, 2, 3".
std::string FormatGlobalValue(const GlobalValue& v);
json::Value GlobalToJson(const GlobalValue& v);
// A value read from a save, made the variable's type (a number for a float: fine; a text for a number:
// nullopt).
std::optional<GlobalValue> GlobalFromJson(GlobalType type, const json::Value& j);
// A name a script can use as Global::<name>: ASCII letters, digits and _, not starting with a digit,
// not a C++ keyword.
bool IsGlobalName(std::string_view name);

class GlobalStore {
public:
    // Every variable of the list at its 初めの値 (one that does not fit: zero, and a warning). A name
    // twice: the first.
    void Reset(std::span<const GlobalVariable> list, std::vector<std::string>* warnings = nullptr);
    GlobalValue* Find(std::string_view name);
    const GlobalValue* Find(std::string_view name) const;
    size_t Size() const { return values_.size(); }
    // Every variable (name, value), in the list's order.
    const std::vector<std::pair<std::string, GlobalValue>>& Values() const { return values_; }
    // A save document: `base` (the file's, or the last read) with "kind", "format" and every variable set.
    json::Value ToSave(json::Value base) const;
    // Takes the variables of a save document (the others stay as they are). False when it is not a save.
    bool FromSave(const json::Value& doc, std::vector<std::string>* warnings);

    json::Value lastRead;  // the last document read (its unknown keys go along to a new slot)

private:
    std::vector<std::pair<std::string, GlobalValue>> values_;
};

inline constexpr char kSaveKind[] = "rm_engine.save";
inline constexpr int64_t kSaveFormat = 1;

// A slot: "0", "auto", "slot-2" (ASCII letters, digits, _ and -; 1 to 64): never a path.
bool IsSaveSlot(std::string_view slot);
std::filesystem::path SaveFile(const std::filesystem::path& folder, std::string_view slot);
bool WriteSave(GlobalStore& store, const std::filesystem::path& folder, std::string_view slot, std::string* why);
bool ReadSave(GlobalStore& store, const std::filesystem::path& folder, std::string_view slot, std::vector<std::string>* warnings,
              std::string* why);
bool SaveExists(const std::filesystem::path& folder, std::string_view slot);
bool DeleteSave(const std::filesystem::path& folder, std::string_view slot, std::string* why);

// <project>/Library/Saves (the editor's Play)
std::filesystem::path ProjectSaveFolder(const std::filesystem::path& project);
// %LOCALAPPDATA%\<exe name>\Saves (an exported game; the exe's name without .exe)
std::filesystem::path GameSaveFolder(std::wstring_view exeName);

}  // namespace rm::game_production
