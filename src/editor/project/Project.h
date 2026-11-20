#pragma once
// ディスク上のプロジェクト:
//
//   <folder>/
//     <name>.rmpf     プロジェクトファイル (JSON)
//     Assets/
//
// .rmpf のドキュメントは丸ごと持つ: この版が知らないキーも、そのまま
// 書き戻す (捨てない)。プロジェクトのデータの中のパスは常に相対なので、
// プロジェクトフォルダーを別の PC にコピーできる (引き継ぎメモ 4-7)。

#include "base/Json.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor {

inline constexpr const char* kProjectKind = "rm_engine.project";
inline constexpr int kProjectFormatVersion = 1;
inline constexpr const wchar_t* kProjectExtension = L".rmpf";

// <project>/UserSettings/Layout.json (パネルのレイアウトと、各ソフトの見方: ユーザーのもので、
// プロジェクトのデータではない)。エディター (EditorScreen) が書き、新しいプロジェクトでは
// そのテンプレート (Templates.h) から作る。
inline constexpr char kLayoutKind[] = "rm_engine.layout";
inline constexpr int64_t kLayoutFormatVersion = 2;

// <folder>/<name>.rmpf.lock - プロジェクトを開いている間エディターが持つ (platform::FileLock)。
// 2 つ目の RM Engine が同じものを開かないように (2 つのエディターが上書きし合ってしまう)、
// また Hub が名前を変えないように。隠しファイルで、エディターを閉じるかプロセスが終わると消える。
inline std::filesystem::path ProjectLockFile(const std::filesystem::path& rmpf) {
    std::filesystem::path p = rmpf;
    p += L".lock";
    return p;
}

struct ProjectTemplate;

struct Project {
    std::filesystem::path file;  // .rmpf
    json::Value doc;             // ドキュメント全体

    std::filesystem::path Root() const { return file.parent_path(); }
    std::filesystem::path AssetsDir() const { return Root() / L"Assets"; }
    // アセットから、次回を速くするためにエンジンが作るもの (圧縮したテクスチャ ...)。プロジェクトの
    // データではない: 消しても安全 (また作られる)。共有しない (git には入れない)。
    std::filesystem::path CacheDir() const { return Root() / L"Cache"; }
    std::filesystem::path LayoutFile() const { return Root() / L"UserSettings" / L"Layout.json"; }
    std::string Name() const;  // UTF-8
};

// .rmpf を読んで調べる。失敗したら nullopt を返し、*error に日本語のメッセージを入れる。
std::optional<Project> LoadProject(const std::filesystem::path& file, std::string* error);
bool SaveProject(const Project& project, std::string* error);

// テンプレートから、.rmpf と Assets/ のある <parentDir>/<name>/ を作る (最初のデータは
// .rmpf に、最初のレイアウトは UserSettings/Layout.json に)。名前は調べ済みであること。
// テンプレートのプラグインは入っているはず (入っていなくてもプロジェクトは作る - 開くときに求める)。
std::optional<Project> CreateProject(const std::filesystem::path& parentDir, const std::string& nameUtf8,
                                     const ProjectTemplate& tmpl, std::string* error);

// 開いていないプロジェクトの名前を変える: その .rmpf と中の "name"、そしてフォルダーが
// プロジェクト自身のもの (名前が付いている - ProjectRename.h。ユーザーのフォルダーは名前を変えない)
// ならそのフォルダーも、すべて新しい名前に (調べ済み)。フォルダーが最初 (一番失敗しやすい:
// 中のファイルが別のアプリで開かれている)。失敗した段階があれば先の段階を元に戻すので、
// プロジェクトが半端に名前を変えられることはない。新しい .rmpf を返す。
std::optional<std::filesystem::path> RenameProject(const std::filesystem::path& file, const std::string& newNameUtf8,
                                                   std::string* error);

}  // namespace rm::editor
