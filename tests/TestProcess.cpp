// Tests for running other programs (platform/Process: the script build runs MSBuild, the export cl /
// rc / link) and for where a folder dialog opens (platform/Shell ExistingFolderAtOrAbove).

#include "Test.h"

#include "platform/Process.h"
#include "platform/Shell.h"

#include <chrono>
#include <filesystem>
#include <thread>

using namespace rm;
namespace fs = std::filesystem;

// What a program leaves running (MSBuild's worker nodes; here a command started in the background)
// ends with it: the call does not wait for it, and it never gets to do anything after.
RM_TEST(Process_WhatItLeftRunningEndsWithIt) {
    const fs::path dir = fs::temp_directory_path() / L"rm_process_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path marker = dir / L"left.txt";
    // The outer cmd ends at once; the one it started waits ~2 s (ping), then would write the marker.
    const std::wstring cmd = L"cmd.exe /d /c start \"\" /b cmd.exe /d /c \"ping -n 3 127.0.0.1 >nul & echo left> \"" + marker.native() + L"\"\"";
    std::string err;
    const auto start = std::chrono::steady_clock::now();
    const platform::ProcessResult r = platform::RunProcess(cmd, dir, {}, &err);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(r.started && r.exitCode == 0);
    CHECK(took < 1.5);  // not kept waiting by the one left running (it holds the output pipe)
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    CHECK(!fs::exists(marker, ec));  // it was ended with the program
    // The output is still read whole, and the exit code given.
    const platform::ProcessResult e = platform::RunProcess(L"cmd.exe /d /c \"echo hello& exit 3\"", dir, {}, &err);
    CHECK(e.started && e.exitCode == 3 && e.output.find("hello") != std::string::npos);
    fs::remove_all(dir, ec);
}

// A folder that is not there yet (an export's output) opens the nearest one above it.
RM_TEST(Shell_ExistingFolderAtOrAbove) {
    const fs::path dir = fs::temp_directory_path() / L"rm_folder_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    CHECK(platform::ExistingFolderAtOrAbove(dir) == dir.lexically_normal());
    CHECK(platform::ExistingFolderAtOrAbove(dir / L"Build" / L"My Game") == dir.lexically_normal());
    CHECK(platform::ExistingFolderAtOrAbove(fs::path()).empty());
    fs::remove_all(dir, ec);
}
