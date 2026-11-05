#pragma once
// Where the engine keeps its files - written here once (the Hub's オプション page shows them).
//
//   <engine>/Project/                       new projects (default)   engine = platform::EngineDirectory()
//   <exe>/Plugins/<author>/<name>/          plugins this engine has
//   %LOCALAPPDATA%\RM_Engine\Plugins\       installed plugins
//   %LOCALAPPDATA%\RM_Engine\Logs\          editor.log
//   %APPDATA%\RM_Engine\hub.json            the Hub's settings (HubSettings)

#include <filesystem>

namespace rm::editor::paths {

std::filesystem::path DefaultProjects();
std::filesystem::path EnginePlugins();
std::filesystem::path InstalledPlugins();
std::filesystem::path LogFolder();
std::filesystem::path LogFile();
std::filesystem::path HubSettingsFile();

}  // namespace rm::editor::paths
