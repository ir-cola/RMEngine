#pragma once
// プロジェクトの名前を変えるとディスク上で何が変わるか (純粋。RMTests でテストする)。
//
//   <parent>/<name>/<name>.rmpf     このエンジンが作ったフォルダー (プロジェクトの名前が付いている):
//                                   フォルダーも .rmpf も中の名前も、新しい名前に変わる
//   <anything>/<other>.rmpf         ユーザーが自分のフォルダーに置いた .rmpf ("開く…" で
//                                   D:\Work\game.rmpf): .rmpf と中の名前だけが変わる。
//                                   フォルダーはユーザーのもの - 変えると D:\Work の名前が変わってしまう。
//
// フォルダーの名前がプロジェクトの名前と同じなら、プロジェクトのフォルダーとみなす
// (Windows と同じく、大文字小文字は無視する)。

#include <filesystem>
#include <string_view>

namespace rm::editor {

struct ProjectRenamePlan {
    bool renameFolder = false;     // フォルダーはプロジェクトのもので、名前が変わる
    std::filesystem::path folder;  // 変更後のプロジェクトのフォルダー
    std::filesystem::path file;    // 変更後の .rmpf
};

ProjectRenamePlan PlanProjectRename(const std::filesystem::path& file, std::string_view oldNameUtf8, std::string_view newNameUtf8);

// Windows が同じとみなす 2 つのファイル名 (大文字小文字は無視する)。
bool SameFileName(const std::filesystem::path& a, const std::filesystem::path& b);

}  // namespace rm::editor
