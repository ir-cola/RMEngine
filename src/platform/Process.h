#pragma once
// Running another program and reading what it prints (the export runs Visual Studio's compiler,
// resource compiler and linker; vswhere finds Visual Studio). Waits until it ends.

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace rm::platform {

struct ProcessResult {
    bool started = false;   // false: the program could not be started (*error says why)
    unsigned long exitCode = 0;
    std::string output;     // stdout and stderr together, as the program wrote them (bytes)
};

// `commandLine`: the program (quoted when it has spaces) and its arguments. `environment`: the whole
// environment it gets (empty: this process's). No window is shown.
ProcessResult RunProcess(const std::wstring& commandLine, const std::filesystem::path& workingDir,
                         const std::map<std::wstring, std::wstring>& environment, std::string* error);

// This process's environment (names as Windows keeps them).
std::map<std::wstring, std::wstring> CurrentEnvironment();

// Program output in the console's code page (cl / link print in it) -> UTF-8.
std::string ConsoleTextToUtf8(const std::string& bytes);

}  // namespace rm::platform
