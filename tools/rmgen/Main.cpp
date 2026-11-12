// rmgen --out <dir> --root <dir> [--root <dir> ...] [--skip-dir <name>] [--scripts]
//
// --skip-dir: folders of that name are not read (the engine skips plugins' templates/: the scripts in
//             a template are a game's, not the engine's).
// --scripts:  the roots are a game's scripts (Assets/Scripts): writes <out>/rm_scripts.cpp, the
//             registration of the script DLL (RM.h), instead of the engine's tables.
//
// Reads every .h under the roots, and writes <out>/<header>.reflect.cpp for each header with
// reflected types plus <out>/rm_all_types.cpp. A file is written only when its text changes
// (so MSBuild recompiles only what changed); generated files of headers that are gone are removed.
// Exit code 0 = ok, 1 = errors in the headers (printed as "file(line,col): error RM####: ..."),
// 2 = bad arguments / I/O.

#include <windows.h>  // WIN32_LEAN_AND_MEAN / NOMINMAX come from RM.Common.props

#include "Scanner.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string U8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Another program (Visual Studio saving a file, an antivirus scan, a compiler reading the output)
// can hold a file for a moment. Every file operation is tried again a few times before giving up.
constexpr int kTries = 20;
constexpr DWORD kWaitMs = 100;

std::string ErrorText(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                   code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring w = n && buf ? std::wstring(buf, n) : std::wstring();
    if (buf) LocalFree(buf);
    while (!w.empty() && (w.back() == L'\r' || w.back() == L'\n' || w.back() == L' ' || w.back() == L'.')) w.pop_back();
    const std::u8string u = fs::path(w).u8string();
    return std::string(u.begin(), u.end()) + " (" + std::to_string(code) + ")";
}

bool ReadOnce(const fs::path& p, std::string& out, DWORD& error) {
    // Share everything: a file being written or renamed by someone else can still be read.
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return false;
    }
    out.clear();
    char buf[64 * 1024];
    DWORD got = 0;
    bool ok = true;
    while ((ok = ReadFile(h, buf, sizeof(buf), &got, nullptr) != 0) && got > 0) out.append(buf, got);
    if (!ok) error = GetLastError();
    CloseHandle(h);
    return ok;
}

// missing = the file is not there (not an error for an output file that was never written).
bool ReadAll(const fs::path& p, std::string& out, std::string* why = nullptr, bool* missing = nullptr) {
    DWORD error = 0;
    for (int i = 0; i < kTries; ++i) {
        if (ReadOnce(p, out, error)) return true;
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            if (missing) *missing = true;
            break;
        }
        Sleep(kWaitMs);
    }
    if (why) *why = ErrorText(error);
    return false;
}

// Writes only when the text differs. Returns false (with the reason) on an I/O error.
bool WriteIfChanged(const fs::path& p, const std::string& text, bool& wrote, std::string& why) {
    std::string old;
    wrote = false;
    bool missing = false;
    if (ReadAll(p, old, nullptr, &missing) && old == text) return true;
    fs::path tmp = p;
    tmp += L".tmp";
    DWORD error = 0;
    for (int i = 0; i < kTries; ++i) {
        if (i > 0) Sleep(kWaitMs);
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            continue;
        }
        DWORD put = 0;
        const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr) && put == text.size();
        if (!ok) error = GetLastError();
        CloseHandle(h);
        if (!ok) continue;
        if (MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            wrote = true;
            return true;
        }
        error = GetLastError();
    }
    DeleteFileW(tmp.c_str());
    why = ErrorText(error);
    return false;
}

// Every .h under `base`. An entry that vanishes while the folder is listed (a temporary file of
// another program) is skipped; a folder that cannot be listed is tried again before giving up.
bool ListHeaders(const fs::path& base, std::vector<fs::path>& out, std::string& why, const std::vector<std::wstring>& skip) {
    std::error_code exists;
    if (!fs::is_directory(base, exists)) {
        why = ErrorText(ERROR_PATH_NOT_FOUND);
        return false;
    }
    for (int i = 0; i < kTries; ++i) {
        if (i > 0) Sleep(kWaitMs);
        out.clear();
        std::error_code ec;
        for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code entryEc;
            if (it->is_directory(entryEc) && std::find(skip.begin(), skip.end(), it->path().filename().native()) != skip.end()) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->path().extension() == L".h" && it->is_regular_file(entryEc)) out.push_back(it->path());
        }
        if (!ec) return true;
        why = ErrorText(static_cast<DWORD>(ec.value()));
    }
    return false;
}
bool Mentions(const std::string& text) {
    for (const char* m : {"RM_STRUCT", "RM_COMPONENT", "RM_SCRIPT", "RM_ENUM", "RM_PROPERTY", "RM_TRANSIENT", "RM_ITEM"}) {
        if (text.find(m) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    fs::path out;
    std::vector<fs::path> roots;
    std::vector<std::wstring> skip;
    bool scripts = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (a == L"--root" && i + 1 < argc) {
            roots.emplace_back(argv[++i]);
        } else if (a == L"--skip-dir" && i + 1 < argc) {
            skip.emplace_back(argv[++i]);
        } else if (a == L"--scripts") {
            scripts = true;
        } else {
            std::printf("rmgen: 使い方: rmgen --out <フォルダ> --root <フォルダ> [--root <フォルダ> ...]\n");
            return 2;
        }
    }
    if (out.empty() || roots.empty()) {
        std::printf("rmgen: --out と --root が必要です\n");
        return 2;
    }

    std::vector<rmgen::Diagnostic> diags;
    std::vector<rmgen::Header> headers;
    for (const fs::path& root : roots) {
        std::error_code ec;
        const fs::path base = fs::absolute(root, ec).lexically_normal();
        std::vector<fs::path> files;
        std::string why;
        if (!ListHeaders(base, files, why, skip)) {
            std::printf("rmgen: error RM0007: フォルダの中を読めませんでした: %s (%s)\n", U8(base).c_str(), why.c_str());
            return 2;
        }
        for (const fs::path& file : files) {
            std::string text;
            bool missing = false;
            if (!ReadAll(file, text, &why, &missing)) {
                if (missing) continue;  // removed while we were reading the folder
                std::printf("%s: error RM0007: 読めませんでした (%s)。ほかのアプリがこのファイルを開いていないか確かめてください\n",
                            U8(file).c_str(), why.c_str());
                return 2;
            }
            if (!Mentions(text)) continue;
            std::string include = U8(file.lexically_relative(base));
            for (char& c : include) {
                if (c == '\\') c = '/';
            }
            rmgen::Header h = rmgen::Scan(text, U8(file), include, diags);
            if (!h.structs.empty() || !h.enums.empty()) headers.push_back(std::move(h));
        }
    }    rmgen::Validate(headers, diags);
    if (scripts) rmgen::ValidateScripts(headers, diags);
    if (!diags.empty()) {
        for (const auto& d : diags) std::printf("%s\n", d.Text().c_str());
        std::printf("rmgen: %zu 個のエラー。反映するコードは作り直していません\n", diags.size());
        return 1;
    }

    std::error_code ec;
    fs::create_directories(out, ec);
    std::set<fs::path> produced;
    int written = 0;
    auto emit = [&](const std::string& name, const std::string& text) {
        const fs::path p = out / fs::path(std::u8string(name.begin(), name.end()));
        produced.insert(p.filename());
        bool wrote = false;
        std::string why;
        if (!WriteIfChanged(p, text, wrote, why)) {
            std::printf("%s: error RM0007: 書き込めませんでした (%s)。ほかのアプリ (ウイルス対策など) がこのファイルを開いていないか確かめてください\n",
                        U8(p).c_str(), why.c_str());
            return false;
        }
        written += wrote ? 1 : 0;
        return true;
    };
    if (scripts) {
        if (!emit("rm_scripts.cpp", rmgen::EmitScripts(headers))) return 2;
    } else {
        for (const auto& h : headers) {
            if (!emit(rmgen::OutputName(h.includePath), rmgen::EmitHeader(headers, h))) return 2;
        }
        if (!emit("rm_all_types.cpp", rmgen::EmitAll(headers))) return 2;
    }
    for (fs::directory_iterator it(out, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == L".cpp" && !produced.count(it->path().filename())) fs::remove(it->path(), ec);
    }
    size_t types = 0;
    for (const auto& h : headers) types += h.structs.size() + h.enums.size();
    std::printf("rmgen: 型 %zu 個 (ヘッダー %zu 枚)、書き直したファイル %d 個\n", types, headers.size(), written);
    return 0;
}
