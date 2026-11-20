// 他のプログラムを動かすこと (platform/Process: スクリプトのビルドが MSBuild を、書き出しが cl /
// rc / link を動かす) と、フォルダーのダイアログが開く場所 (platform/Shell ExistingFolderAtOrAbove) のテスト。

#include "Test.h"

#include "platform/Process.h"
#include "platform/Shell.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace rm;
namespace fs = std::filesystem;

// プログラムが動かしたままにしたもの (MSBuild のワーカーノード。ここではバックグラウンドで始めたコマンド)
// は、そのプログラムといっしょに終わる: 呼び出しはそれを待たず、その後に何かをすることもない。
RM_TEST(Process_WhatItLeftRunningEndsWithIt) {
    const fs::path dir = fs::temp_directory_path() / L"rm_process_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path marker = dir / L"left.txt";
    // 外側の cmd はすぐ終わる。それが始めたものは 約 2 秒 (ping) 待ち、その後にマーカーを書くはずだった。
    const std::wstring cmd = L"cmd.exe /d /c start \"\" /b cmd.exe /d /c \"ping -n 3 127.0.0.1 >nul & echo left> \"" + marker.native() + L"\"\"";
    std::string err;
    const auto start = std::chrono::steady_clock::now();
    const platform::ProcessResult r = platform::RunProcess(cmd, dir, {}, &err);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(r.started && r.exitCode == 0);
    CHECK(took < 1.5);  // 動かしたままのもの (出力のパイプを持っている) に待たされない
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    CHECK(!fs::exists(marker, ec));  // プログラムといっしょに終わった
    // 出力は最後まで読めて、終了コードも返る。
    const platform::ProcessResult e = platform::RunProcess(L"cmd.exe /d /c \"echo hello& exit 3\"", dir, {}, &err);
    CHECK(e.started && e.exitCode == 3 && e.output.find("hello") != std::string::npos);
    fs::remove_all(dir, ec);
}

// まだ無いフォルダー (書き出しの出力先) は、その上の一番近いフォルダーを開く。
RM_TEST(Shell_ExistingFolderAtOrAbove) {
    const fs::path dir = fs::temp_directory_path() / L"rm_folder_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    CHECK(platform::ExistingFolderAtOrAbove(dir) == dir.lexically_normal());
    CHECK(platform::ExistingFolderAtOrAbove(dir / L"Build" / L"My Game") == dir.lexically_normal());
    CHECK(platform::ExistingFolderAtOrAbove(fs::path()).empty());
    fs::remove_all(dir, ec);
}
