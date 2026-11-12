// Entry point of RM_Engine.exe (Hub + editor).

#include "base/Log.h"
#include "base/Utf.h"
#include "base/Version.h"
#include "editor/app/App.h"
#include "editor/app/Paths.h"
#include "editor/project/HubSettings.h"
#include "platform/Win.h"

#include <objbase.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace {

rm::editor::LaunchOptions ParseCommandLine() {
    rm::editor::LaunchOptions opt;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return opt;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--export-template" && i + 4 < argc) {
            for (int k = 0; k < 4; ++k) opt.exportTemplate.push_back(argv[++i]);
        } else if (a == L"--new-from-template" && i + 4 < argc) {
            for (int k = 0; k < 4; ++k) opt.newFromTemplate.push_back(argv[++i]);
        } else if (a == L"--export" && i + 2 < argc) {
            for (int k = 0; k < 2; ++k) opt.exportProject.push_back(argv[++i]);
        } else if (a == L"--project" && i + 1 < argc) {
            opt.projectFile = argv[++i];
        } else if (a.size() > 5 && _wcsicmp(a.c_str() + a.size() - 5, L".rmpf") == 0) {
            opt.projectFile = a;  // a .rmpf passed directly (e.g. opened from Explorer)
        }
    }
    LocalFree(argv);
    // A path relative to where the engine was started from becomes a full one: the Hub's list of
    // projects holds full paths, so the same project never shows twice.
    if (!opt.projectFile.empty()) {
        std::error_code ec;
        const auto full = std::filesystem::absolute(opt.projectFile, ec);
        if (!ec) opt.projectFile = full.lexically_normal();
    }
    return opt;
}

void InstallLogSinks() {
    // Debugger output (Visual Studio "Output" window).
    rm::log::AddSink([](const rm::log::Entry& e) {
        const char* tag = e.level == rm::log::Level::Error ? "[error] " : e.level == rm::log::Level::Warn ? "[warn]  " : "[info]  ";
        OutputDebugStringW(rm::Utf8ToWide(std::string(tag) + e.text + "\n").c_str());
    });
    // %LOCALAPPDATA%\RM_Engine\Logs\editor.log (overwritten on every start).
    std::error_code ec;
    std::filesystem::create_directories(rm::editor::paths::LogFolder(), ec);
    static std::ofstream file(rm::editor::paths::LogFile(), std::ios::binary | std::ios::trunc);
    static std::mutex mtx;
    if (!file) return;
    rm::log::AddSink([](const rm::log::Entry& e) {
        std::lock_guard lock(mtx);
        const char* tag = e.level == rm::log::Level::Error ? "ERROR" : e.level == rm::log::Level::Warn ? "WARN " : "INFO ";
        file << std::format("{:9.3f} {} {}\n", e.time, tag, e.text);
        file.flush();
    });
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // STA is required by the shell file dialogs.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    InstallLogSinks();
    rm::log::Info("{} {} を起動しました", rm::kEngineName, rm::kEngineVersion);

    int code = 0;
    {
        rm::editor::App app;
        code = app.Run(ParseCommandLine());
    }
    rm::log::Info("終了しました");
    if (SUCCEEDED(hr)) CoUninitialize();
    return code;
}
