#pragma once
// What renaming a project changes on disk (pure - tested in RMTests).
//
//   <parent>/<name>/<name>.rmpf     a folder this engine made (named after the project): the folder,
//                                   the .rmpf and the name inside all follow the new name
//   <anything>/<other>.rmpf         a .rmpf the user put in a folder of their own ("開く…" of
//                                   D:\Work\game.rmpf): only the .rmpf and the name inside change.
//                                   The folder is the user's - renaming it would rename D:\Work.
//
// The folder counts as the project's when its name is the project's name (letter case ignored, as
// Windows does).

#include <filesystem>
#include <string_view>

namespace rm::editor {

struct ProjectRenamePlan {
    bool renameFolder = false;     // the folder is the project's and its name changes
    std::filesystem::path folder;  // the project's folder afterwards
    std::filesystem::path file;    // the .rmpf afterwards
};

ProjectRenamePlan PlanProjectRename(const std::filesystem::path& file, std::string_view oldNameUtf8, std::string_view newNameUtf8);

// Two file names that Windows treats as one (letter case ignored).
bool SameFileName(const std::filesystem::path& a, const std::filesystem::path& b);

}  // namespace rm::editor
