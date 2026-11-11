#pragma once
// Where the engine keeps its files - written here once (the Hub's オプション page shows them).
//
//   <engine>/Project/                       new projects (default)   engine = platform::EngineDirectory()
//   <exe>/Plugins/<author>/<name>/          plugins this engine has
//   %LOCALAPPDATA%\RM_Engine\Engines\<key>\Plugins\   installed plugins of THIS engine (EngineKey.h:
//                                           every build / copy of the engine has its own)
//   %LOCALAPPDATA%\RM_Engine\Plugins\       where every engine kept them before (read once: what
//                                           was installed there is what a new engine installs first)
//   %LOCALAPPDATA%\RM_Engine\Logs\          editor.log
//   %APPDATA%\RM_Engine\hub.json            the Hub's settings (HubSettings)
//   %APPDATA%\RM_Engine\Layouts.json        panel layouts shared by every project (per software)

#include <filesystem>

namespace rm::editor::paths {

std::filesystem::path DefaultProjects();
std::filesystem::path EnginePlugins();
std::filesystem::path InstalledPlugins();
std::filesystem::path LegacyInstalledPlugins();
std::filesystem::path LogFolder();
std::filesystem::path LogFile();
std::filesystem::path HubSettingsFile();
std::filesystem::path SharedLayoutsFile();
std::filesystem::path LangFolder();      // <exe>/Lang (the core's translations)
std::filesystem::path FileList();        // <exe>/RM_Engine.files.json (for the integrity check)

}  // namespace rm::editor::paths
