#include "editor/project/Project.h"

#include "editor/lang/Lang.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/project/HubSettings.h"
#include "editor/project/ProjectRename.h"
#include "editor/project/Templates.h"
#include "platform/Shell.h"

#include <format>

namespace fs = std::filesystem;

namespace rm::editor {

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
    const std::string text = json::Write(project.doc);
    constexpr uint64_t kLimitMb = platform::kMaxFileBytes >> 20;
    if (text.size() > platform::kMaxFileBytes) {
        // Not saved (the file on disk stays as it was): it could not be opened again.
        if (error) {
            *error = TrU8F("プロジェクトファイルが大きすぎるので保存しませんでした ({} MB / 上限 {} MB)。前に保存したものはそのままです。\n大きなデータ (絵・音・モデルなど) は Assets フォルダのファイルにしてください。",
                           text.size() >> 20, kLimitMb);
        }
        return false;
    }
    if (text.size() > platform::kMaxFileBytes / 4 * 3) {
        log::Warn("プロジェクトファイルが上限に近づいています ({} MB / 上限 {} MB)。大きなデータは Assets のファイルにしてください: {}",
                  text.size() >> 20, kLimitMb, PathToUtf8(project.file));
    }
    std::string err;
    if (!platform::WriteFileAtomic(project.file, text, &err)) {
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
    // Plugins this project uses. Recorded automatically (never by hand): here, the plugins of the
    // template's first data; afterwards, whenever a software saves its data.
    p.doc.Set("plugins", json::Value::MakeArray());
    ApplyTemplate(p.doc, tmpl);

    // The first layout (which software shows first ...) before the project file: a project is never
    // found without it.
    if (const json::Value layout = TemplateLayout(tmpl); !layout.IsNull()) {
        std::string err;
        fs::create_directories(p.LayoutFile().parent_path(), ec);
        if (ec || !platform::WriteFileAtomic(p.LayoutFile(), json::Write(layout), &err)) {
            if (error) *error = TrU8F("最初の配置を書けませんでした: {}\n{}", PathToUtf8(p.LayoutFile()), ec ? ec.message() : err);
            return std::nullopt;
        }
    }
    if (!SaveProject(p, error)) return std::nullopt;
    log::Info("プロジェクトを作りました: {} (テンプレート: {})", PathToUtf8(p.file), tmpl.id);
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

    // The folder follows only when it is the project's own (named after it); a folder of the user's
    // (a .rmpf opened from D:\Work) keeps its name - only the .rmpf and the name inside change.
    const ProjectRenamePlan plan = PlanProjectRename(file, oldName, newNameUtf8);
    const fs::path oldFolder = file.parent_path();
    const fs::path newFolder = plan.folder;
    const fs::path newFile = plan.file;
    const fs::path newFileName = newFile.filename();
    if (newFile.native().size() > 240) return fail(TrU8("新しい名前だとパスが長すぎます。短い名前にしてください。"));
    // Only the letter case may differ (Windows names ignore case): the same folder, renamed.
    const bool sameFolder = SamePath(oldFolder, newFolder);
    std::error_code ec;
    if (!sameFolder && fs::exists(newFolder, ec)) return fail(TrU8F("同じ名前のフォルダが既にあります: {}", PathToUtf8(newFolder)));

    // 1. The folder (only when it is the project's).
    bool folderMoved = false;
    if (plan.renameFolder) {
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
