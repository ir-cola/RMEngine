#pragma once
// A project on disk:
//
//   <folder>/
//     <name>.rmpf     project file (JSON)
//     Assets/
//
// The .rmpf document is kept whole: keys this version does not know are written
// back unchanged (never dropped). Paths inside project data are always relative,
// so a project folder can be copied to another PC (handoff memo 4-7).

#include "base/Json.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor {

inline constexpr const char* kProjectKind = "rm_engine.project";
inline constexpr int kProjectFormatVersion = 1;
inline constexpr const wchar_t* kProjectExtension = L".rmpf";

struct ProjectTemplate {
    std::string id;           // "rm_engine/blank"
    std::wstring name;        // shown in the Hub
    std::wstring description;
};

// Templates offered by the Hub. Today only the blank project exists; plugins will add theirs
// (game -> 2D / 3D -> genre, see the template tree in the design docs).
const std::vector<ProjectTemplate>& ProjectTemplates();

struct Project {
    std::filesystem::path file;  // the .rmpf
    json::Value doc;             // whole document

    std::filesystem::path Root() const { return file.parent_path(); }
    std::filesystem::path AssetsDir() const { return Root() / L"Assets"; }
    std::string Name() const;  // UTF-8
};

// Reads and checks a .rmpf. On failure returns nullopt and a Japanese message in *error.
std::optional<Project> LoadProject(const std::filesystem::path& file, std::string* error);
bool SaveProject(const Project& project, std::string* error);

// Creates <parentDir>/<name>/ with the .rmpf and Assets/. The name must already be validated.
std::optional<Project> CreateProject(const std::filesystem::path& parentDir, const std::string& nameUtf8,
                                     const ProjectTemplate& tmpl, std::string* error);

// Renames a project that is not open: its folder, its .rmpf and the "name" inside, all to the new
// name (already validated). The folder goes first (what fails most often: a file in it is open in
// another app); a step that fails puts the earlier ones back, so the project is never half renamed.
// Returns the new .rmpf.
std::optional<std::filesystem::path> RenameProject(const std::filesystem::path& file, const std::string& newNameUtf8,
                                                   std::string* error);

}  // namespace rm::editor
