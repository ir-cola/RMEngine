#include "editor/app/Paths.h"

#include "platform/Shell.h"

namespace rm::editor::paths {

namespace fs = std::filesystem;

namespace {
fs::path LocalRoot() { return platform::GetKnownFolder(platform::KnownFolder::LocalAppData) / L"RM_Engine"; }
}  // namespace

fs::path DefaultProjects() { return platform::EngineDirectory() / L"Project"; }
fs::path EnginePlugins() { return platform::ExeDirectory() / L"Plugins"; }
fs::path InstalledPlugins() { return LocalRoot() / L"Plugins"; }
fs::path LogFolder() { return LocalRoot() / L"Logs"; }
fs::path LogFile() { return LogFolder() / L"editor.log"; }
fs::path HubSettingsFile() { return platform::GetKnownFolder(platform::KnownFolder::RoamingAppData) / L"RM_Engine" / L"hub.json"; }

}  // namespace rm::editor::paths
