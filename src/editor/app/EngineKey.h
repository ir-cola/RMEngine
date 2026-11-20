#pragma once
// このエンジンがどれか、フォルダー名として (純粋 - RMTests でテストする)。
//
// エンジンのビルドはどれも (Debug、Release、git worktree ごと、インストールしたコピー)、入っている
// プラグインを自分の分として持つ: %LOCALAPPDATA%\RM_Engine\Engines\<key>\Plugins\。1 つの
// フォルダーを共有すると、古いビルドが古いプラグインのファイルを新しいビルドのものに上書き
// コピーしたり、動いている別のエンジンが読んでいるフォルダーを消したりした。key は exe の
// フォルダー: "<その名前>-<フルパスの 16 桁の 16 進>"。例 "Release-3f2a..."。同じフォルダー
// (大文字小文字と末尾の区切りは無視) は同じ key になる。

#include <filesystem>
#include <string>

namespace rm::editor {

std::string EngineKey(const std::filesystem::path& exeDirectory);

}  // namespace rm::editor
