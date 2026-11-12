#include "rm_engine/game_production/editor/ScriptBuild.h"

#include "base/Utf.h"
#include "platform/Win.h"
#include "rm_engine/game_production/runtime/Scripts.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace rm::game_production {

namespace fs = std::filesystem;

namespace {

std::string Xml(std::string s) {
    std::string o;
    for (const char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

// A folder for MSBuild: absolute, with a backslash at the end.
std::string Dir(const fs::path& p) {
    std::string s = PathToUtf8(fs::absolute(p).lexically_normal());
    if (!s.empty() && s.back() != '\\') s += '\\';
    return s;
}

bool IsScriptFile(const fs::path& p) {
    const std::wstring e = p.extension().native();
    return e == L".h" || e == L".cpp" || e == L".hpp" || e == L".inl";
}

bool WriteIfChanged(const fs::path& p, const std::string& text, std::string* why) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    {
        std::ifstream in(p, std::ios::binary);
        if (in) {
            std::stringstream old;
            old << in.rdbuf();
            if (old.str() == text) return true;
        }
    }
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
    if (!out) {
        if (why) *why = std::format("「{}」を書けませんでした", PathToUtf8(p));
        return false;
    }
    return true;
}

// Runs a command line and waits; its exit code (-1: it could not start). `output`: what it printed
// (null: thrown away - MSBuild's is read from its log file instead: its console text is in the
// console's code page, and its helper processes could hold a pipe open after it ends).
int Run(const std::wstring& commandLine, const fs::path& folder, std::string* output) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (output) {
        if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return -1;
        SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    } else {
        writeEnd = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        if (writeEnd == INVALID_HANDLE_VALUE) return -1;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;
    const BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        folder.empty() ? nullptr : folder.c_str(), &si, &pi);
    CloseHandle(writeEnd);
    if (!started) {
        if (readEnd) CloseHandle(readEnd);
        return -1;
    }
    std::string bytes;
    if (readEnd) {
        char buf[4096];
        DWORD got = 0;
        while (ReadFile(readEnd, buf, sizeof(buf), &got, nullptr) && got > 0) bytes.append(buf, got);
        CloseHandle(readEnd);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (output) *output = std::move(bytes);
    return static_cast<int>(code);
}

std::wstring Quote(const fs::path& p) { return L"\"" + p.native() + L"\""; }

std::string ReadText(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    std::string t = s.str();
    if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF && static_cast<unsigned char>(t[1]) == 0xBB &&
        static_cast<unsigned char>(t[2]) == 0xBF) {
        t.erase(0, 3);  // the UTF-8 mark MSBuild's file logger writes
    }
    return t;
}

}  // namespace

fs::path ScriptsFolder(const fs::path& project) { return project / L"Assets" / L"Scripts"; }
fs::path ScriptBuildFolder(const fs::path& project) { return project / L"Library" / L"Scripts"; }
fs::path ScriptSolution(const fs::path& project) { return project / L"Scripts.sln"; }

long long ScriptSourcesTime(const fs::path& project) {
    long long newest = 0, count = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(ScriptsFolder(project), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || !IsScriptFile(it->path())) continue;
        ++count;
        newest = std::max<long long>(newest, it->last_write_time(e2).time_since_epoch().count());
    }
    return count == 0 ? 0 : newest + count;  // (a file removed changes it too)
}

std::string ScriptVcxproj(const ScriptBuildRequest& r) {
    std::string sdk;
    for (const fs::path& p : r.sdk) sdk += Xml(Dir(p)) + ";";
    const std::string engine = Xml(Dir(r.engine));
    std::string o;
    o += R"(<?xml version="1.0" encoding="utf-8"?>
<!-- RM Engine が作りました (プロジェクトを開くたびに書き直します)。スクリプトは Assets/Scripts に書きます。
     Written by RM Engine (again every time the project is opened). Write scripts in Assets/Scripts. -->
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{6D2B1E3A-5C1F-4E7A-9B0D-2A4C8E7F1D35}</ProjectGuid>
    <RootNamespace>Scripts</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
)";
    o += "    <RmEngineDir Condition=\"'$(RmEngineDir)' == ''\">" + engine + "</RmEngineDir>\n";
    o += "    <RmSdkDirs>" + sdk + "</RmSdkDirs>\n";
    o += R"(    <RmScriptsDir>$(MSBuildThisFileDirectory)..\..\Assets\Scripts\</RmScriptsDir>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup>
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <PlatformToolset>v143</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
    <UseDebugLibraries Condition="'$(Configuration)' == 'Debug'">true</UseDebugLibraries>
    <UseDebugLibraries Condition="'$(Configuration)' == 'Release'">false</UseDebugLibraries>
    <WholeProgramOptimization>false</WholeProgramOptimization>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir Condition="'$(OutDir)' == '' Or '$(OutDir)' == '$(SolutionDir)$(Platform)\$(Configuration)\'">$(MSBuildThisFileDirectory)Build\</OutDir>
    <IntDir>$(MSBuildThisFileDirectory)Obj\$(Configuration)\</IntDir>
    <IncludePath>$(RmScriptsDir);$(RmSdkDirs);$(IncludePath)</IncludePath>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalOptions>/utf-8 /Zc:__cplusplus %(AdditionalOptions)</AdditionalOptions>
      <WarningLevel>Level3</WarningLevel>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <PreprocessorDefinitions>NOMINMAX;WIN32_LEAN_AND_MEAN;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <RuntimeLibrary Condition="'$(Configuration)' == 'Debug'">MultiThreadedDebugDLL</RuntimeLibrary>
      <RuntimeLibrary Condition="'$(Configuration)' == 'Release'">MultiThreadedDLL</RuntimeLibrary>
      <Optimization Condition="'$(Configuration)' == 'Release'">MaxSpeed</Optimization>
    </ClCompile>
    <ClCompile Condition="'$(RmForExport)' == 'true'">
      <DebugInformationFormat>None</DebugInformationFormat>
    </ClCompile>
    <Link>
      <GenerateDebugInformation Condition="'$(RmForExport)' != 'true'">true</GenerateDebugInformation>
      <GenerateDebugInformation Condition="'$(RmForExport)' == 'true'">false</GenerateDebugInformation>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="$(RmScriptsDir)**\*.cpp" />
    <ClCompile Include="$(MSBuildThisFileDirectory)Generated\rm_scripts.cpp" />
    <ClInclude Include="$(RmScriptsDir)**\*.h" />
  </ItemGroup>
  <!-- rmgen reads the scripts' headers (RM_SCRIPT / RM_PROPERTY) and writes their registration. -->
  <Target Name="RmGenScripts" BeforeTargets="ClCompile">
    <Exec Command="&quot;$(RmEngineDir)rmgen.exe&quot; --scripts --out &quot;$(MSBuildThisFileDirectory)Generated&quot; --root &quot;$(RmScriptsDir).&quot;" StdOutEncoding="UTF-8" />
  </Target>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
)";
    return o;
}

std::string ScriptSln() {
    return "\xEF\xBB\xBF\r\n"
           "Microsoft Visual Studio Solution File, Format Version 12.00\r\n"
           "# Visual Studio Version 17\r\n"
           "VisualStudioVersion = 17.0.31903.59\r\n"
           "MinimumVisualStudioVersion = 10.0.40219.1\r\n"
           "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"Scripts\", \"Library\\Scripts\\Scripts.vcxproj\", "
           "\"{6D2B1E3A-5C1F-4E7A-9B0D-2A4C8E7F1D35}\"\r\n"
           "EndProject\r\n"
           "Global\r\n"
           "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\r\n"
           "\t\tDebug|x64 = Debug|x64\r\n"
           "\t\tRelease|x64 = Release|x64\r\n"
           "\tEndGlobalSection\r\n"
           "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n"
           "\t\t{6D2B1E3A-5C1F-4E7A-9B0D-2A4C8E7F1D35}.Debug|x64.ActiveCfg = Debug|x64\r\n"
           "\t\t{6D2B1E3A-5C1F-4E7A-9B0D-2A4C8E7F1D35}.Debug|x64.Build.0 = Debug|x64\r\n"
           "\t\t{6D2B1E3A-5C1F-4E7A-9B0D-2A4C8E7F1D35}.Release|x64.ActiveCfg = Release|x64\r\n"
           "\t\t{6D2B1E3A-5C1F-4E7A-9B0D-2A4C8E7F1D35}.Release|x64.Build.0 = Release|x64\r\n"
           "\tEndGlobalSection\r\n"
           "EndGlobal\r\n";
}

bool WriteScriptProject(const ScriptBuildRequest& r, std::string* why) {
    std::error_code ec;
    fs::create_directories(ScriptsFolder(r.project), ec);
    return WriteIfChanged(ScriptBuildFolder(r.project) / L"Scripts.vcxproj", ScriptVcxproj(r), why) &&
           WriteIfChanged(ScriptSolution(r.project), ScriptSln(), why);
}

fs::path FindMSBuild() {
    wchar_t pf[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", pf, MAX_PATH)) return {};
    const fs::path vswhere = fs::path(pf) / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
    std::error_code ec;
    if (!fs::exists(vswhere, ec)) return {};
    std::string out;
    const std::wstring cmd = Quote(vswhere) +
                             L" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe -utf8 -nologo";
    if (Run(cmd, {}, &out) != 0) return {};
    const std::string first = out.substr(0, out.find_first_of("\r\n"));
    if (first.empty()) return {};
    const fs::path p = PathFromUtf8(first);
    return fs::exists(p, ec) ? p : fs::path();
}

std::vector<std::string> ErrorLines(const std::string& output) {
    std::vector<std::string> lines;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find(": error ") == std::string::npos && line.find(": fatal error ") == std::string::npos) continue;
        // MSBuild ends a line with " [the project]": not needed here.
        if (const size_t b = line.rfind(" ["); b != std::string::npos && line.back() == ']') line.erase(b);
        while (!line.empty() && line.front() == ' ') line.erase(0, 1);
        if (std::find(lines.begin(), lines.end(), line) == lines.end()) lines.push_back(line);
    }
    return lines;
}

ScriptBuildResult BuildScripts(const ScriptBuildRequest& r) {
    ScriptBuildResult res;
    if (ScriptSourcesTime(r.project) == 0) {
        res.nothingToBuild = true;
        res.summary = "スクリプトはありません (Assets/Scripts に .h / .cpp がありません)";
        return res;
    }
    const fs::path msbuild = FindMSBuild();
    if (msbuild.empty()) {
        res.noVisualStudio = true;
        res.summary = "Visual Studio 2022 (「C++ によるデスクトップ開発」) が見つからないので、スクリプトをビルドできません";
        return res;
    }
    std::string why;
    if (!WriteScriptProject(r, &why)) {
        res.summary = why;
        return res;
    }
    const fs::path folder = ScriptBuildFolder(r.project);
    // A new name every time (Scripts_0007.dll): the one the editor has loaded cannot be written over.
    // For an export: always Scripts.dll in Export/, built apart (its own objects).
    const int number = r.forExport ? 0 : std::max(0, ScriptDllNumber(LatestScriptDll(folder).filename().native())) + 1;
    const std::wstring name = r.forExport ? std::wstring(L"Scripts") : Utf8ToWide(std::format("Scripts_{:04}", number));
    const fs::path outFolder = r.forExport ? folder / L"Export" : folder;
    const fs::path log = folder / (r.forExport ? L"build_export.log" : L"build.log");
    // (A folder ends with a backslash, which would escape the closing quote: doubled inside quotes.)
    auto folderArg = [](const char* key, const fs::path& p) { return L" \"" + Utf8ToWide(key) + Utf8ToWide(Dir(p)) + L"\\\""; };
    std::wstring cmd = Quote(msbuild) + L" " + Quote(folder / L"Scripts.vcxproj") + L" -nologo -v:quiet -m -nodeReuse:false -p:Configuration=" +
                       (r.debug && !r.forExport ? L"Debug" : L"Release") + L" -p:Platform=x64 -p:TargetName=" + name + folderArg("-p:OutDir=", outFolder) +
                       folderArg("-p:RmEngineDir=", r.engine) + L" \"-flp:LogFile=" + log.native() + L";Encoding=UTF-8;Verbosity=minimal\"";
    if (r.forExport) {
        std::error_code e0;
        fs::remove(outFolder / L"Scripts.dll", e0);  // (never the one of an earlier export, if this one fails)
        cmd += L" -p:RmForExport=true" + folderArg("-p:IntDir=", folder / L"ObjExport");
    }
    const int code = Run(cmd, folder, nullptr);
    const std::string text = ReadText(log);
    res.errors = ErrorLines(text);
    const fs::path dll = outFolder / (name + L".dll");
    std::error_code ec;
    if (code != 0 || !fs::exists(dll, ec)) {
        res.summary = std::format("スクリプトのビルドに失敗しました (エラー {} 個)。前のスクリプトのまま動きます", res.errors.size());
        if (res.errors.empty() && code == -1) res.errors.push_back("MSBuild を始められませんでした");
        return res;
    }
    res.ok = true;
    res.dll = dll;
    res.summary = std::format("スクリプトをビルドしました ({})", WideToUtf8(name + L".dll"));
    if (r.forExport) return res;
    // The older DLLs (two back and before) are not needed: removed when they can be (one in use stays).
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        const fs::path stem = p.stem();
        const int n = ScriptDllNumber(stem.native() + L".dll");
        const std::wstring ext = p.extension().native();
        if (n >= 0 && n < number - 1 && (ext == L".dll" || ext == L".pdb" || ext == L".exp" || ext == L".lib" || ext == L".ilk")) {
            std::error_code e2;
            fs::remove(p, e2);
        }
    }
    return res;
}

}  // namespace rm::game_production
