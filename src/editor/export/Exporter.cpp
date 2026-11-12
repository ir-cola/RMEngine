#include "editor/export/Exporter.h"

#include "base/Json.h"
#include "base/Log.h"
#include "base/Utf.h"
#include "editor/export/ExportPlan.h"
#include "editor/lang/Lang.h"
#include "platform/Crypto.h"
#include "platform/Process.h"
#include "platform/Shell.h"

#include <algorithm>
#include <format>
#include <fstream>

namespace fs = std::filesystem;

namespace rm::editor::exporter {

namespace {

std::wstring Quote(const fs::path& p) { return L"\"" + p.native() + L"\""; }

std::string Tail(const std::string& text, size_t lines = 30) {
    size_t at = text.size(), n = 0;
    while (at > 0 && n <= lines) {
        at = text.rfind('\n', at - 1);
        if (at == std::string::npos) return text;
        ++n;
    }
    return text.substr(at + 1);
}

bool WriteFile(const fs::path& file, std::string_view bytes, std::string* error) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        if (error) *error = TrU8F("ファイルを書けませんでした: {}", PathToUtf8(file));
        return false;
    }
    return true;
}

std::optional<std::string> ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Visual Studio: where it is (vswhere), and the environment its vcvars64.bat makes.
struct VisualStudio {
    fs::path root;
    std::map<std::wstring, std::wstring> env;
    fs::path tools;  // ...\VC\Tools\MSVC\<ver>\bin\Hostx64\x64\ (cl, link)
    fs::path rc;
};

std::optional<VisualStudio> FindVisualStudio(std::string* error) {
    wchar_t pf[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH);
    const fs::path vswhere = fs::path(pf) / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
    std::error_code ec;
    if (!fs::exists(vswhere, ec)) {
        if (error) *error = TrU8("Visual Studio が見つかりません。ゲームの書き出しには Visual Studio 2022 (「C++ によるデスクトップ開発」) が要ります。");
        return std::nullopt;
    }
    std::string err;
    const auto found = platform::RunProcess(Quote(vswhere) + L" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -utf8",
                                            {}, {}, &err);
    std::string path = found.output;
    while (!path.empty() && (path.back() == '\n' || path.back() == '\r' || path.back() == ' ')) path.pop_back();
    if (!found.started || found.exitCode != 0 || path.empty()) {
        if (error) *error = TrU8("C++ のツールの入った Visual Studio が見つかりません。Visual Studio Installer で「C++ によるデスクトップ開発」を入れてください。");
        return std::nullopt;
    }
    VisualStudio vs;
    vs.root = PathFromUtf8(path);
    const fs::path vcvars = vs.root / L"VC" / L"Auxiliary" / L"Build" / L"vcvars64.bat";
    const auto set = platform::RunProcess(L"cmd.exe /d /s /c \"" + Quote(vcvars) + L" >nul && set\"", {}, {}, &err);
    if (!set.started || set.exitCode != 0) {
        if (error) *error = TrU8F("Visual Studio の準備 (vcvars64.bat) が失敗しました:\n{}", Tail(platform::ConsoleTextToUtf8(set.output)));
        return std::nullopt;
    }
    const std::wstring text = Utf8ToWide(platform::ConsoleTextToUtf8(set.output));
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (const size_t eq = line.find(L'='); eq != std::wstring::npos && eq > 0) vs.env[line.substr(0, eq)] = line.substr(eq + 1);
        start = end + 1;
    }
    auto envOf = [&](const wchar_t* name) -> std::wstring {
        for (const auto& [k, v] : vs.env) {
            if (_wcsicmp(k.c_str(), name) == 0) return v;
        }
        return {};
    };
    // The tools' messages in English (plain ASCII: never garbled by a code page) - shown as they are.
    vs.env[L"VSLANG"] = L"1033";
    vs.tools = fs::path(envOf(L"VCToolsInstallDir")) / L"bin" / L"Hostx64" / L"x64";
    vs.rc = fs::path(envOf(L"WindowsSdkVerBinPath")) / L"x64" / L"rc.exe";
    if (!fs::exists(vs.tools / L"link.exe", ec) || !fs::exists(vs.rc, ec)) {
        if (error) *error = TrU8("Visual Studio のリンカ (link.exe) か Windows SDK のリソースコンパイラ (rc.exe) が見つかりません。Visual Studio Installer で「C++ によるデスクトップ開発」を入れてください。");
        return std::nullopt;
    }
    return vs;
}

// Visual Studio's C++ runtime DLLs to put next to the game (VC\Redist\MSVC\<newest>\x64\Microsoft.VC*.CRT\).
std::vector<fs::path> RuntimeDlls(const fs::path& vsRoot) {
    std::vector<fs::path> out;
    std::error_code ec;
    std::vector<fs::path> versions;
    for (fs::directory_iterator it(vsRoot / L"VC" / L"Redist" / L"MSVC", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory() && fs::exists(it->path() / L"x64", ec)) versions.push_back(it->path());
    }
    std::sort(versions.begin(), versions.end());
    for (auto v = versions.rbegin(); v != versions.rend() && out.empty(); ++v) {
        for (fs::directory_iterator it(*v / L"x64", ec), end; !ec && it != end; it.increment(ec)) {
            const std::wstring name = it->path().filename().native();
            if (!it->is_directory() || name.rfind(L"Microsoft.VC", 0) != 0 || name.find(L".CRT") == std::wstring::npos) continue;
            for (fs::directory_iterator f(it->path(), ec), fe; !ec && f != fe; f.increment(ec)) {
                if (f->path().extension() == L".dll") out.push_back(f->path());
            }
        }
    }
    return out;
}

bool Contains(const std::string& haystack, std::string_view needle) {
    if (needle.empty()) return false;
    auto lower = [](unsigned char c) { return static_cast<char>(c < 0x80 ? std::tolower(c) : c); };
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b)); }) != haystack.end();
}

}  // namespace

fs::path KitFolder() { return platform::ExeDirectory() / L"GameKit"; }

bool Run(const Request& r, const Report& report, std::string* error) {
    auto fail = [&](std::string msg) {
        log::Error("書き出し: {}", msg);
        if (error) *error = std::move(msg);
        return false;
    };
    // 1. The game kit.
    report(0.02f, TrU8("ゲームの部品 (GameKit) を確かめています"));
    const fs::path kit = KitFolder();
    const auto kitText = ReadFile(kit / L"kit.json");
    const auto kitDoc = kitText ? json::Parse(*kitText) : std::nullopt;
    if (!kitDoc || kitDoc->Get("kind").AsString() != "rm_engine.gamekit") {
        return fail(TrU8F("ゲームの部品 (GameKit) がありません: {}\nエンジンをビルドし直してください (RMGameKit が作ります)。", PathToUtf8(kit)));
    }
    std::vector<GameLib> libs;
    std::vector<fs::path> libFiles{kit / PathFromUtf8(kitDoc->Get("core").AsString())};
    auto readReflect = [&](const fs::path& dir, GameLib& lib) {
        std::error_code ec;
        for (fs::directory_iterator it(dir / L"reflect", ec), end; !ec && it != end; it.increment(ec)) {
            if (const auto text = ReadFile(it->path())) ReadSymbols(*text, lib.types, lib.enums);
        }
    };
    {
        GameLib core;
        readReflect(kit / L"lib" / L"core", core);
        libs.push_back(std::move(core));
    }
    for (const PluginId& id : r.plugins) {
        const json::Value* entry = nullptr;
        for (const auto& p : kitDoc->Get("plugins").Items()) {
            if (p.Get("author").AsString() == id.author && p.Get("name").AsString() == id.name) entry = &p;
        }
        GameLib lib;
        lib.plugin = id;
        if (entry) {
            lib.registers = entry->Get("entry").AsBool();
            libFiles.push_back(kit / PathFromUtf8(entry->Get("lib").AsString()));
            readReflect(kit / L"lib" / PathFromUtf8(id.author) / PathFromUtf8(id.name), lib);
            report(0.05f, TrU8F("プラグイン: {}", id.Text()));
        } else {
            report(0.05f, TrU8F("プラグイン: {} (ゲームに入る部分がありません)", id.Text()));
        }
        libs.push_back(std::move(lib));
    }
    std::error_code ec;
    for (const fs::path& f : libFiles) {
        if (!fs::exists(f, ec)) return fail(TrU8F("ゲームの部品が欠けています: {}\nエンジンをビルドし直してください。", PathToUtf8(f)));
    }

    // 2. Visual Studio.
    report(0.10f, TrU8("Visual Studio を探しています"));
    std::string err;
    const auto vs = FindVisualStudio(&err);
    if (!vs) return fail(err);
    report(0.20f, TrU8F("Visual Studio: {}", PathToUtf8(vs->root)));

    // The build number (the exe and the .rmpak) and the work folder.
    std::vector<std::pair<std::string, std::string>> forNumber;
    for (const pak::Entry& e : r.files) forNumber.push_back({e.path, e.bytes});
    forNumber.push_back({"exe", r.exeName});
    forNumber.push_back({"title", r.titleUtf8});
    const uint64_t build = BuildNumber(forNumber);
    const fs::path work = fs::temp_directory_path(ec) / PathFromUtf8(std::format("rm_export_{:016x}", build));
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    fs::create_directories(r.outDir, ec);
    if (ec) return fail(TrU8F("書き出し先のフォルダを作れませんでした: {}\n{}", PathToUtf8(r.outDir), ec.message()));

    const std::optional<std::string> ico = r.iconPng ? IconFile(*r.iconPng) : std::nullopt;
    if (r.iconPng && !ico) report(0.22f, TrU8("アイコンの画像が PNG でも ICO でもないので、アイコンなしで書き出します"));
    if (!WriteFile(work / L"main.cpp", GameMainSource(libs, r.titleUtf8, build), &err) ||
        !WriteFile(work / L"game.rc", GameResourceSource(r.titleUtf8, r.exeName, ico.has_value()), &err) ||
        (ico && !WriteFile(work / L"game.ico", *ico, &err))) {
        return fail(err);
    }
    auto run = [&](const std::wstring& cmd, const std::string& what) {
        const auto res = platform::RunProcess(cmd, work, vs->env, &err);
        if (!res.started) return fail(TrU8F("{} を起動できませんでした: {}", what, err));
        if (res.exitCode != 0) return fail(TrU8F("{} が失敗しました (終了コード {}):\n{}", what, res.exitCode, Tail(platform::ConsoleTextToUtf8(res.output))));
        return true;
    };
    const std::string crt = kitDoc->Get("crt").AsString();
    const std::wstring crtFlag = crt == "MultiThreadedDLL" ? L"/MD" : crt == "MultiThreadedDebug" ? L"/MTd" : crt == "MultiThreadedDebugDLL" ? L"/MDd" : L"/MT";
    report(0.30f, TrU8("初期化のファイル (main.cpp) をコンパイルしています"));
    if (!run(Quote(vs->tools / L"cl.exe") + L" /nologo /c /std:c++20 /utf-8 /EHsc /O2 " + crtFlag + L" /Fomain.obj main.cpp", "cl")) return false;
    report(0.40f, TrU8("アイコンと名前のリソースを作っています"));
    if (!run(Quote(vs->rc) + L" /nologo /c65001 /fogame.res game.rc", "rc")) return false;

    // 3. Link: made with its name.
    const fs::path exe = r.outDir / PathFromUtf8(r.exeName + ".exe");
    report(0.50f, TrU8F("リンクしています: {}", PathToUtf8(exe.filename())));
    std::wstring rsp;
    rsp += L"/nologo /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /INCREMENTAL:NO /OPT:REF /OPT:ICF /MANIFEST:EMBED /MANIFESTUAC:NO\n";
    rsp += L"/OUT:" + Quote(exe) + L"\nmain.obj\ngame.res\n";
    for (const fs::path& f : libFiles) rsp += Quote(f) + L"\n";
    for (const auto& s : kitDoc->Get("systemLibs").Items()) rsp += Utf8ToWide(s.AsString()) + L"\n";
    if (!WriteFile(work / L"link.rsp", "\xEF\xBB\xBF" + WideToUtf8(rsp), &err)) return fail(err);  // (UTF-8 with its mark: link reads it so)
    if (!run(Quote(vs->tools / L"link.exe") + L" @link.rsp", "link")) return false;

    // 4. The rest of the folder.
    report(0.75f, TrU8F("アセットを詰めています ({} 個)", r.files.size()));
    const std::string pakBytes = pak::Write(r.files, build, platform::PakCipher(build));
    if (pakBytes.empty()) return fail(TrU8("アセットを暗号化できませんでした (Windows の暗号の機能 (CNG) を使えません)"));
    if (!WriteFile(r.outDir / L"Game.rmpak", pakBytes, &err)) return fail(err);
    if (!r.scriptsDll.empty()) {
        fs::copy_file(r.scriptsDll, r.outDir / L"Scripts.dll", fs::copy_options::overwrite_existing, ec);
        if (ec) return fail(TrU8F("スクリプトの DLL を写せませんでした: {}\n{}", PathToUtf8(r.scriptsDll), ec.message()));
        report(0.80f, TrU8("スクリプト: Scripts.dll"));
    } else {
        fs::remove(r.outDir / L"Scripts.dll", ec);  // (one of an earlier export)
    }
    const auto dlls = RuntimeDlls(vs->root);
    for (const fs::path& d : dlls) {
        fs::copy_file(d, r.outDir / d.filename(), fs::copy_options::overwrite_existing, ec);
        if (ec) return fail(TrU8F("C++ のランタイムを写せませんでした: {}\n{}", PathToUtf8(d), ec.message()));
    }
    report(0.85f, dlls.empty() ? TrU8("★ Visual Studio の C++ ランタイム (Redist) が見つからないので、横に置いていません")
                               : TrU8F("C++ のランタイム: {} 個", dlls.size()));

    // 5. No path of this PC in anything written: the exe, the scripts' DLL, every file of the .rmpak
    // (before it was encrypted) - the project's folder, the engine's, the user's.
    std::vector<std::string> privatePaths = r.privatePaths;
    privatePaths.push_back(PathToUtf8(platform::ExeDirectory()));
    wchar_t home[MAX_PATH] = {};
    if (const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH); n > 0 && n < MAX_PATH) privatePaths.push_back(WideToUtf8(home));
    auto check = [&](const std::string& bytes, const std::string& what) {
        for (const std::string& p : privatePaths) {
            const std::wstring wide = Utf8ToWide(p);
            const std::string utf16(reinterpret_cast<const char*>(wide.data()), wide.size() * sizeof(wchar_t));
            if (Contains(bytes, p) || Contains(bytes, utf16)) report(0.9f, TrU8F("★ {} の中にこの PC のパスが残っています: {}", what, p));
        }
    };
    if (const auto exeBytes = ReadFile(exe)) check(*exeBytes, PathToUtf8(exe.filename()));
    if (!r.scriptsDll.empty()) {
        if (const auto dllBytes = ReadFile(r.outDir / L"Scripts.dll")) check(*dllBytes, "Scripts.dll");
    }
    for (const pak::Entry& e : r.files) check(e.bytes, e.path);
    fs::remove_all(work, ec);
    report(1.f, TrU8F("書き出しました: {}", PathToUtf8(exe)));
    return true;
}

}  // namespace rm::editor::exporter
