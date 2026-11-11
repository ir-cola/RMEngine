#include "editor/project/ProjectRename.h"

#include "base/Utf.h"
#include "editor/project/Project.h"

#include <cwctype>
#include <string>

namespace fs = std::filesystem;

namespace rm::editor {

bool SameFileName(const fs::path& a, const fs::path& b) {
    const std::wstring& x = a.native();
    const std::wstring& y = b.native();
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (std::towlower(x[i]) != std::towlower(y[i])) return false;
    }
    return true;
}

ProjectRenamePlan PlanProjectRename(const fs::path& file, std::string_view oldNameUtf8, std::string_view newNameUtf8) {
    ProjectRenamePlan plan;
    const fs::path oldFolder = file.parent_path();
    const fs::path newName = PathFromUtf8(std::string(newNameUtf8));
    const bool folderIsProjects = !oldFolder.filename().empty() && SameFileName(oldFolder.filename(), PathFromUtf8(std::string(oldNameUtf8)));
    plan.folder = folderIsProjects ? oldFolder.parent_path() / newName : oldFolder;
    plan.renameFolder = folderIsProjects && oldFolder.filename().native() != newName.native();
    plan.file = plan.folder / (newName.native() + kProjectExtension);
    return plan;
}

}  // namespace rm::editor
