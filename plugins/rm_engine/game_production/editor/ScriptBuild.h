#pragma once
// Building a game's scripts into a DLL with Visual Studio 2022's MSBuild (found with vswhere), and the
// Visual Studio project that lets the user write them with IntelliSense.
//
//   <project>/Assets/Scripts/**.h / **.cpp            the user's scripts (plain C++20)
//   <project>/Scripts.sln                             open this in Visual Studio (「Visual Studio で開く」)
//   <project>/Library/Scripts/Scripts.vcxproj         written by the editor (when a project is opened)
//   <project>/Library/Scripts/Generated/rm_scripts.cpp   rmgen --scripts writes it before every build
//   <project>/Library/Scripts/Scripts_0007.dll        what the editor built (a new name every time:
//                                                     the loaded one cannot be written over)
//   <engine>/rmgen.exe, <engine>/Sdk/RM.h ...         the generator and the headers scripts include
//
// The DLL is built with the engine's toolset and C runtime (/MD - /MDd for a Debug engine), so the
// same DLL runs in the editor and in an exported game. Building runs a process and waits for it: call
// it on a worker thread (editor/app/Worker), never while drawing.

#include <filesystem>
#include <string>
#include <vector>

namespace rm::game_production {

struct ScriptBuildRequest {
    std::filesystem::path project;          // the project's folder
    std::filesystem::path engine;           // where rmgen.exe is
    std::vector<std::filesystem::path> sdk;  // folders of RM.h & co. (the engine's Sdk/; the tests: the sources)
    bool debug = false;                     // the engine is a Debug build (/MDd)
};

struct ScriptBuildResult {
    bool ok = false;
    bool noVisualStudio = false;     // MSBuild was not found (told in one line, nothing else)
    bool nothingToBuild = false;     // no .cpp / .h in Assets/Scripts
    std::filesystem::path dll;       // ok: the new DLL
    std::vector<std::string> errors; // the compiler's / rmgen's lines with "error" (UTF-8)
    std::string summary;             // one line for the console (Japanese)
};

// <project>/Assets/Scripts
std::filesystem::path ScriptsFolder(const std::filesystem::path& project);
// <project>/Library/Scripts
std::filesystem::path ScriptBuildFolder(const std::filesystem::path& project);
// <project>/Scripts.sln
std::filesystem::path ScriptSolution(const std::filesystem::path& project);
// The newest time a script file (.h / .cpp under Assets/Scripts) was written; 0 when there is none.
long long ScriptSourcesTime(const std::filesystem::path& project);

// Writes Library/Scripts/Scripts.vcxproj and Scripts.sln (only when their text changes). False with
// the reason when they cannot be written.
bool WriteScriptProject(const ScriptBuildRequest& r, std::string* why);
// The text of the two (pure: tested).
std::string ScriptVcxproj(const ScriptBuildRequest& r);
std::string ScriptSln();

// Visual Studio's MSBuild.exe (vswhere: the newest installation with MSBuild); empty when there is none.
std::filesystem::path FindMSBuild();
// Builds the scripts into Library/Scripts/Scripts_<next number>.dll. Blocks until MSBuild is done.
ScriptBuildResult BuildScripts(const ScriptBuildRequest& r);
// The lines of MSBuild's output that are errors (", error " / ": error "), without the project path at the end.
std::vector<std::string> ErrorLines(const std::string& output);

}  // namespace rm::game_production
