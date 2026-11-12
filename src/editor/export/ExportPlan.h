#pragma once
// The pure parts of exporting a game (design NewEngineBluePrint 11-b / 11-b-2, RME_PLATFORM 5-a
// "書き出しを作り直す"): which plugins go in, the exe's name, the small generated main that links them,
// and the check that no path of the PC it was made on is left. Tested in tests/TestExport.cpp; the
// files and the linker are Exporter.cpp's.
//
//   1  the .rmpf's record of used plugins  ->  + their requirements and connectors (ExportPlugins)
//   2  the game kit's .lib of the core and of each of those plugins' runtime/ (built with the engine)
//   3  GameMainSource: a .cpp that only lists them (their registration and their reflected types) -
//      so nothing a static library holds is dropped by the linker for not being named
//   4  a resource with the icon and the game's name, then  link  ->  <exe name>.exe, made with its name

#include "base/Json.h"
#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rm::editor::exporter {

// The default exe name for a game name: the characters a file name cannot have taken out, spaces and
// dots taken off its ends, at most 64 characters; "Game" when nothing (or a name Windows keeps: CON ...)
// is left. Never changed silently: the export screen shows it in a field of its own.
std::string ExeNameFrom(std::string_view gameName);
// Why `name` cannot be an exe's name (Japanese), empty when it can.
std::string ExeNameProblem(std::string_view name);

// The plugins an exported game links: the recorded ones with their requirements and the connectors
// they need (as installing them would bring: requirements first, each once). Recorded plugins this
// engine does not have go to *missing.
std::vector<PluginId> ExportPlugins(const std::vector<PluginId>& recorded, const std::vector<PluginManifest>& available,
                                    std::vector<PluginId>* missing);

// One library linked into the game: the core's (no id) or a plugin's runtime part.
struct GameLib {
    std::optional<PluginId> plugin;
    bool registers = false;                // it has a RegisterRuntime (RM_RUNTIME_ENTRY)
    std::vector<std::string> types;        // its reflected types' symbols (rmgen: T_...)
    std::vector<std::string> enums;        // ... and enums' (E_...)
};
// The C name of a plugin's runtime registration (RM_RUNTIME_ENTRY in core/RuntimeRegistry.h).
std::string RuntimeEntryName(const PluginId& id);
// The generated main: the plugins in this order (requirements first), every reflected type of the
// libraries (reflect::AllTypes / AllEnums), the game's title and the export's build number.
std::string GameMainSource(const std::vector<GameLib>& libs, std::string_view titleUtf8, uint64_t build);

// The reflected symbols of a library, read from rmgen's files of its headers (lines
// "extern const ::rm::reflect::TypeDesc T_x = {" / "... EnumDesc E_x = {").
void ReadSymbols(std::string_view generatedCpp, std::vector<std::string>& types, std::vector<std::string>& enums);

// The resource script (rc /c65001: UTF-8) of the game's exe: its icon (game.ico next to it, when
// `icon`) and its name in the file's details (説明 / 製品名 / 元のファイル名).
std::string GameResourceSource(std::string_view titleUtf8, std::string_view exeName, bool icon);
// An .ico holding one PNG picture (Windows Vista and later read PNG inside an icon). Nullopt when
// `png` is not a PNG.
std::optional<std::string> IcoFromPng(std::string_view png);
// The .ico of an icon picture: an .ico as it is (its pictures of every size), a PNG put inside one
// (IcoFromPng). Nullopt for anything else.
std::optional<std::string> IconFile(std::string_view bytes);

// The export's number (the exe and its .rmpak carry it): from what goes into the .rmpak, so the same
// game exported again gets the same number (and so the same files). Never 0.
uint64_t BuildNumber(const std::vector<std::pair<std::string, std::string>>& entries);

// A path of the exported game: relative to its folder, "/" or "\" between the parts, never leaving it.
bool PortablePath(std::string_view path);
// Strings in `v` that look like a path of the PC it was made on (a drive "C:\", "\\server", or a
// path starting at the root): what the export checks before it writes the game's data.
std::vector<std::string> AbsolutePathsIn(const json::Value& v);

}  // namespace rm::editor::exporter
