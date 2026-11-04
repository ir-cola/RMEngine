// rmgen --out <dir> --root <dir> [--root <dir> ...]
//
// Reads every .h under the roots, and writes <out>/<header>.reflect.cpp for each header with
// reflected types plus <out>/rm_all_types.cpp. A file is written only when its text changes
// (so MSBuild recompiles only what changed); generated files of headers that are gone are removed.
// Exit code 0 = ok, 1 = errors in the headers (printed as "file(line,col): error RM####: ..."),
// 2 = bad arguments / I/O.

#include <windows.h>  // WIN32_LEAN_AND_MEAN / NOMINMAX come from RM.Common.props

#include "Scanner.h"

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

bool ReadAll(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Writes only when the text differs. Returns false on an I/O error.
bool WriteIfChanged(const fs::path& p, const std::string& text, bool& wrote) {
    std::string old;
    wrote = false;
    if (ReadAll(p, old) && old == text) return true;
    fs::path tmp = p;
    tmp += L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) return false;
    wrote = true;
    return true;
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
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--out" && i + 1 < argc) {
            out = argv[++i];
        } else if (a == L"--root" && i + 1 < argc) {
            roots.emplace_back(argv[++i]);
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
        for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != L".h") continue;
            std::string text;
            if (!ReadAll(it->path(), text)) {
                std::printf("%s: error RM0007: 読めませんでした\n", U8(it->path()).c_str());
                return 2;
            }
            if (!Mentions(text)) continue;
            std::string include = U8(it->path().lexically_relative(base));
            for (char& c : include) {
                if (c == '\\') c = '/';
            }
            rmgen::Header h = rmgen::Scan(text, U8(it->path()), include, diags);
            if (!h.structs.empty() || !h.enums.empty()) headers.push_back(std::move(h));
        }
        if (ec) {
            std::printf("rmgen: %s を読めませんでした\n", U8(base).c_str());
            return 2;
        }
    }
    rmgen::Validate(headers, diags);
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
        if (!WriteIfChanged(p, text, wrote)) {
            std::printf("%s: error RM0007: 書き込めませんでした\n", U8(p).c_str());
            return false;
        }
        written += wrote ? 1 : 0;
        return true;
    };
    for (const auto& h : headers) {
        if (!emit(rmgen::OutputName(h.includePath), rmgen::EmitHeader(headers, h))) return 2;
    }
    if (!emit("rm_all_types.cpp", rmgen::EmitAll(headers))) return 2;
    for (fs::directory_iterator it(out, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == L".cpp" && !produced.count(it->path().filename())) fs::remove(it->path(), ec);
    }
    size_t types = 0;
    for (const auto& h : headers) types += h.structs.size() + h.enums.size();
    std::printf("rmgen: 型 %zu 個 (ヘッダー %zu 枚)、書き直したファイル %d 個\n", types, headers.size(), written);
    return 0;
}
