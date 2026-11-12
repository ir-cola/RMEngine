#pragma once
// Making the exported game's folder (design NewEngineBluePrint 11-b / 11-b-2, RME_PLATFORM 5-a
// "書き出しを作り直す" 1-7). Knows no software: the software that exports (a game) gives what goes in -
// its plugins, its files for the .rmpak, its name and icon, its scripts' DLL. Runs on a worker thread.
//
//   1  the game kit next to the editor (GameKit/kit.json: the core's and each plugin's runtime .lib)
//   2  the generated main (ExportPlan: GameMainSource) and the resource (name, icon): compiled with
//      Visual Studio's cl / rc (found by vswhere; its environment from vcvars64.bat)
//   3  link: the main, the resource, core.lib and only the given plugins' libs -> <exe name>.exe, made
//      with its name (no copying and renaming), no .pdb
//   4  Game.rmpak (core/pak), Scripts.dll, Visual Studio's C++ runtime DLLs (an app-local copy: a PC
//      without the redistributable package plays)
//   5  the exe is searched for the paths of this PC it must not hold (the project's, the engine's)

#include "core/PluginId.h"
#include "core/pak/Pak.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor::exporter {

struct Request {
    std::filesystem::path outDir;     // made when missing
    std::string exeName;              // without ".exe" (ExeNameProblem says nothing)
    std::string titleUtf8;            // the exe's description and product name
    std::vector<PluginId> plugins;    // ExportPlugins' order (requirements first)
    std::vector<pak::Entry> files;    // into Game.rmpak
    std::optional<std::string> iconPng;
    std::filesystem::path scriptsDll;  // empty: the game has no scripts
    std::vector<std::string> privatePaths;  // UTF-8: folders of this PC the game must not name
};

// progress 0..1 and a line for the log (Japanese).
using Report = std::function<void(float progress, const std::string& line)>;

// Makes the folder. False and *error (Japanese, with the tools' own words) when it could not.
bool Run(const Request& request, const Report& report, std::string* error);

// The game kit's folder (next to the editor's exe).
std::filesystem::path KitFolder();

}  // namespace rm::editor::exporter
