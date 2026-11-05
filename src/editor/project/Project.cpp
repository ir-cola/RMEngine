#include "editor/project/Project.h"

#include "editor/lang/Lang.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/project/HubSettings.h"
#include "platform/Shell.h"

#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

const std::vector<ProjectTemplate>& ProjectTemplates() {
    static const std::vector<ProjectTemplate> kTemplates = {
        {"rm_engine/blank", TrKey(L"白紙"), TrKey(L"何も入っていないプロジェクト。プラグインを足して育てます。")},
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
        if (error) *error = TrU8F("プロジェクトファイルを開けません: {}\n{}", PathToUtf8(file), readErr);
        return std::nullopt;
    }
    json::ParseError pe;
    auto doc = json::Parse(*bytes, &pe);
    if (!doc || !doc->IsObject()) {
        if (error) {
            *error = doc ? TrU8F("プロジェクトファイルの形が正しくありません: {}", PathToUtf8(file))
                         : TrU8F("プロジェクトファイルが壊れています: {}\n{} 行 {} 文字目: {}", PathToUtf8(file), pe.line,
                                       pe.column, pe.message);
        }
        return std::nullopt;
    }
    if (doc->Get("kind").AsString() != kProjectKind) {
        if (error) *error = TrU8F("RM Engine のプロジェクトファイルではありません: {}", PathToUtf8(file));
        return std::nullopt;
    }
    const int64_t ver = doc->Get("formatVersion").AsInt(0);
    if (ver > kProjectFormatVersion) {
        if (error) {
            *error = TrU8F("このプロジェクトは新しいバージョンの RM Engine で作られています (形式 {} / このエンジンは {} まで)。\nデータを壊さないよう、開かずに止めました。",
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
        if (error) *error = TrU8F("保存できませんでした: {}\n{}", PathToUtf8(project.file), err);
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
        if (error) *error = TrU8F("同じ名前のフォルダが既にあり、空ではありません: {}", PathToUtf8(folder));
        return std::nullopt;
    }
    fs::create_directories(folder / L"Assets", ec);
    if (ec) {
        if (error) *error = TrU8F("フォルダを作れませんでした: {}\n{}", PathToUtf8(folder), ec.message());
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

std::optional<fs::path> RenameProject(const fs::path& file, const std::string& newNameUtf8, std::string* error) {
    auto fail = [&](std::string msg) -> std::optional<fs::path> {
        log::Error("プロジェクトの名前を変えられませんでした: {}", msg);
        if (error) *error = std::move(msg);
        return std::nullopt;
    };
    std::optional<Project> project = LoadProject(file, error);
    if (!project) return std::nullopt;
    const std::string oldName = project->Name();

    const fs::path oldFolder = file.parent_path();
    const fs::path newFolder = oldFolder.parent_path() / PathFromUtf8(newNameUtf8);
    const fs::path newFileName = PathFromUtf8(newNameUtf8).native() + kProjectExtension;
    const fs::path newFile = newFolder / newFileName;
    if (newFile.native().size() > 240) return fail(TrU8("新しい名前だとパスが長すぎます。短い名前にしてください。"));
    // Only the letter case may differ (Windows names ignore case): the same folder, renamed.
    const bool sameFolder = SamePath(oldFolder, newFolder);
    std::error_code ec;
    if (!sameFolder && fs::exists(newFolder, ec)) return fail(TrU8F("同じ名前のフォルダが既にあります: {}", PathToUtf8(newFolder)));

    // 1. The folder.
    bool folderMoved = false;
    if (oldFolder.filename().native() != newFolder.filename().native()) {
        fs::rename(oldFolder, newFolder, ec);
        if (ec) {
            return fail(TrU8F("フォルダの名前を変えられませんでした。中のファイルをほかのアプリ (エクスプローラーなど) で開いていないか確かめてください。\n{}\n{}",
                                    PathToUtf8(oldFolder), ec.message()));
        }
        folderMoved = true;
    }
    auto undoFolder = [&] {
        if (!folderMoved) return;
        std::error_code e2;
        fs::rename(newFolder, oldFolder, e2);
        if (e2) log::Error("フォルダの名前を元に戻せませんでした: {} → {} ({})", PathToUtf8(newFolder), PathToUtf8(oldFolder), e2.message());
    };

    // 2. The .rmpf (now inside the renamed folder).
    const fs::path fileNow = newFolder / file.filename();
    bool fileMoved = false;
    if (file.filename().native() != newFileName.native()) {
        if (!SamePath(fileNow, newFile) && fs::exists(newFile, ec)) {
            undoFolder();
            return fail(TrU8F("フォルダの中に同じ名前のファイルが既にあります: {}", PathToUtf8(newFileName)));
        }
        fs::rename(fileNow, newFile, ec);
        if (ec) {
            undoFolder();
            return fail(TrU8F("プロジェクトファイルの名前を変えられませんでした: {}\n{}", PathToUtf8(fileNow), ec.message()));
        }
        fileMoved = true;
    }

    // 3. The name inside (every other key is written back as it was).
    project->file = newFile;
    project->doc.Set("name", newNameUtf8);
    std::string saveErr;
    if (!SaveProject(*project, &saveErr)) {
        if (fileMoved) {
            std::error_code e2;
            fs::rename(newFile, fileNow, e2);
        }
        undoFolder();
        return fail(saveErr);
    }
    log::Info("プロジェクトの名前を変えました: {} → {} ({})", oldName, newNameUtf8, PathToUtf8(newFile));
    return newFile;
}

}  // namespace rm::editor
