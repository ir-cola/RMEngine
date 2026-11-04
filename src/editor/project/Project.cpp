#include "editor/project/Project.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "platform/Shell.h"

#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

const std::vector<ProjectTemplate>& ProjectTemplates() {
    static const std::vector<ProjectTemplate> kTemplates = {
        {"rm_engine/blank", L"白紙", L"何も入っていないプロジェクト。プラグインを足して育てます。"},
    };
    return kTemplates;
}

std::string Project::Name() const {
    const std::string& n = doc.Get("name").AsString();
    return n.empty() ? PathToUtf8(file.stem()) : n;
}

std::optional<Project> LoadProject(const fs::path& file, std::string* error) {
    std::string readErr;
    auto bytes = platform::ReadFileBytes(file, &readErr);
    if (!bytes) {
        if (error) *error = std::format("プロジェクトファイルを開けません: {}\n{}", PathToUtf8(file), readErr);
        return std::nullopt;
    }
    json::ParseError pe;
    auto doc = json::Parse(*bytes, &pe);
    if (!doc || !doc->IsObject()) {
        if (error) {
            *error = doc ? std::format("プロジェクトファイルの形が正しくありません: {}", PathToUtf8(file))
                         : std::format("プロジェクトファイルが壊れています: {}\n{} 行 {} 文字目: {}", PathToUtf8(file), pe.line,
                                       pe.column, pe.message);
        }
        return std::nullopt;
    }
    if (doc->Get("kind").AsString() != kProjectKind) {
        if (error) *error = std::format("RM Engine のプロジェクトファイルではありません: {}", PathToUtf8(file));
        return std::nullopt;
    }
    const int64_t ver = doc->Get("formatVersion").AsInt(0);
    if (ver > kProjectFormatVersion) {
        if (error) {
            *error = std::format(
                "このプロジェクトは新しいバージョンの RM Engine で作られています (形式 {} / このエンジンは {} まで)。\n"
                "データを壊さないよう、開かずに止めました。",
                ver, kProjectFormatVersion);
        }
        return std::nullopt;
    }
    Project p;
    p.file = file;
    p.doc = std::move(*doc);
    return p;
}

bool SaveProject(const Project& project, std::string* error) {
    std::string err;
    if (!platform::WriteFileAtomic(project.file, json::Write(project.doc), &err)) {
        if (error) *error = std::format("保存できませんでした: {}\n{}", PathToUtf8(project.file), err);
        return false;
    }
    log::Info("保存しました: {}", PathToUtf8(project.file));
    return true;
}

std::optional<Project> CreateProject(const fs::path& parentDir, const std::string& nameUtf8, const ProjectTemplate& tmpl,
                                     std::string* error) {
    const fs::path folder = parentDir / PathFromUtf8(nameUtf8);
    std::error_code ec;
    if (fs::exists(folder, ec) && !fs::is_empty(folder, ec)) {
        if (error) *error = std::format("同じ名前のフォルダが既にあり、空ではありません: {}", PathToUtf8(folder));
        return std::nullopt;
    }
    fs::create_directories(folder / L"Assets", ec);
    if (ec) {
        if (error) *error = std::format("フォルダを作れませんでした: {}\n{}", PathToUtf8(folder), ec.message());
        return std::nullopt;
    }

    Project p;
    p.file = folder / (PathFromUtf8(nameUtf8).native() + kProjectExtension);
    p.doc = json::Value::MakeObject();
    p.doc.Set("kind", kProjectKind);
    p.doc.Set("formatVersion", kProjectFormatVersion);
    p.doc.Set("name", nameUtf8);
    p.doc.Set("engineVersion", kEngineVersion);
    p.doc.Set("template", tmpl.id);
    // Plugins this project uses. Recorded automatically (never by hand) once plugins exist.
    p.doc.Set("plugins", json::Value::MakeArray());

    if (!SaveProject(p, error)) return std::nullopt;
    log::Info("プロジェクトを作りました: {}", PathToUtf8(p.file));
    return p;
}

}  // namespace rm::editor
