#pragma once
// Which engine this is, as a folder name (pure - tested in RMTests).
//
// Every build of the engine (Debug, Release, each git worktree, an installed copy) keeps its own
// installed plugins: %LOCALAPPDATA%\RM_Engine\Engines\<key>\Plugins\. Sharing one folder let an old
// build copy its old plugin files over a newer build's, or remove a folder another running engine
// was reading. The key is the exe's folder: "<its name>-<16 hex of its full path>", e.g.
// "Release-3f2a...". The same folder (letter case and a trailing separator ignored) is the same key.

#include <filesystem>
#include <string>

namespace rm::editor {

std::string EngineKey(const std::filesystem::path& exeDirectory);

}  // namespace rm::editor
