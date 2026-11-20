#pragma once
// Visual Studio 2022 の MSBuild (vswhere で見つける) でゲームのスクリプトを DLL にビルドする。
// そして、ユーザーが IntelliSense つきでスクリプトを書けるようにする Visual Studio のプロジェクト。
//
//   <project>/Assets/Scripts/**.h / **.cpp            ユーザーのスクリプト (ふつうの C++20)
//   <project>/Scripts.sln                             これを Visual Studio で開く (「Visual Studio で開く」)
//   <project>/Library/Scripts/Scripts.vcxproj         エディターが書く (プロジェクトを開いたとき)
//   <project>/Library/Scripts/Generated/rm_scripts.cpp   rmgen --scripts が、ビルドのたびに書く
//   <project>/Library/Scripts/Scripts_0007.dll        エディターがビルドしたもの (毎回新しい名前:
//                                                     読み込んだものには上書きできない)
//   <engine>/rmgen.exe, <engine>/Sdk/RM.h ...         ジェネレーターと、スクリプトが include するヘッダー
//
// DLL は、エンジンのツールセットと C ランタイム (/MD - Debug のエンジンは /MDd) でビルドするので、
// 同じ DLL がエディターでも書き出したゲームでも動く。ビルドはプロセスを起動して待つ: ワーカー
// スレッド (editor/app/Worker) から呼ぶこと。描画中には呼ばない。

#include "rm_engine/game_production/runtime/GameSettings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace rm::game_production {

struct ScriptBuildRequest {
    std::filesystem::path project;          // プロジェクトのフォルダ
    std::filesystem::path engine;           // rmgen.exe がある場所
    std::vector<std::filesystem::path> sdk;  // RM.h などのフォルダ (エンジンの Sdk/。テストでは: ソース)
    bool debug = false;                     // エンジンが Debug ビルドである (/MDd)
    // 書き出すゲーム用: いつも Release (/MD - 書き出しがゲームの隣に置くランタイム)。デバッグ情報なし
    // (.pdb なし、DLL にこの PC のパスを入れない)。Library/Scripts/Export/Scripts.dll に作る
    // (エディターの DLL やオブジェクトとは別)。このとき `debug` は使わない。
    bool forExport = false;
    // ゲーム設定の グローバル変数: Library/Scripts/Globals/RmGlobals.h (Global::<name>) として書く。
    std::vector<GlobalVariable> globals;
    // 変わったものだけでなく、すべてのオブジェクトを作り直す (MSBuild の Rebuild ターゲット。ビルドし直す)。
    bool rebuild = false;
};

struct ScriptBuildResult {
    bool ok = false;
    bool noVisualStudio = false;     // MSBuild が見つからなかった (1 行で伝える。他には何もしない)
    bool nothingToBuild = false;     // Assets/Scripts に .cpp / .h がない
    std::filesystem::path dll;       // 成功: 新しい DLL
    std::vector<std::string> errors; // コンパイラーや rmgen の "error" を含む行 (UTF-8)
    std::string summary;             // コンソール用の 1 行 (日本語)
};

// エディターの最後のビルドがしたこと (デバッグウィンドウの スクリプト に出る)。
struct ScriptBuildOutcome {
    enum class State { None, Ok, Failed, NoVisualStudio, NothingToBuild } state = State::None;
    std::string summary;              // コンソールと同じ (日本語)
    std::vector<std::string> errors;  // コンパイラーの行
    double at = 0;                    // 終わった時刻 (platform::NowSeconds)
};

// <project>/Assets/Scripts
std::filesystem::path ScriptsFolder(const std::filesystem::path& project);
// <project>/Library/Scripts
std::filesystem::path ScriptBuildFolder(const std::filesystem::path& project);
// <project>/Scripts.sln
std::filesystem::path ScriptSolution(const std::filesystem::path& project);
// DLL をビルドする場所: Library/Scripts (Release) / Library/Scripts/Debug (Debug でビルドしたエンジン)。
// DLL は同じ種類のエンジンだけが使う (RmModuleFlag_Debug)。書き出しは Release のものを使う。
std::filesystem::path ScriptDllFolder(const std::filesystem::path& project, bool debug);
// スクリプトファイル (Assets/Scripts の下の .h / .cpp) が最後に書かれた一番新しい時刻。1 つもなければ 0。
long long ScriptSourcesTime(const std::filesystem::path& project);

// Library/Scripts/Scripts.vcxproj、Scripts.sln、Globals/RmGlobals.h を書く (中身が変わるときだけ)。
// 書けないときは、理由つきで false。
bool WriteScriptProject(const ScriptBuildRequest& r, std::string* why);
// その 2 つの中身 (純粋: テストする)。
std::string ScriptVcxproj(const ScriptBuildRequest& r);
std::string ScriptSln();
// Library/Scripts/Globals/RmGlobals.h: グローバル変数を Global::<name> として並べたもの (RM.h が include する)。
std::filesystem::path GlobalsHeaderFile(const std::filesystem::path& project);
// その中身 (純粋: テストする)。スクリプトが使えない名前 (C++ の名前でない、重複) は省いて
// `skipped` に入れて知らせる (値は名前でなら使える: rm::GlobalVar<int>("名前"))。
std::string GlobalsHeader(const std::vector<GlobalVariable>& globals, std::vector<std::string>* skipped = nullptr);

// Visual Studio の MSBuild.exe (vswhere: MSBuild のある一番新しいインストール)。なければ空。
std::filesystem::path FindMSBuild();
// スクリプトを ScriptDllFolder/Scripts_<次の番号>.dll にビルドする。MSBuild が終わるまで止まる。
// この PC では 1 プロジェクトにつき一度に 1 つのビルドだけ (次のものはそれを待つ: 名前つきミューテックス)。
ScriptBuildResult BuildScripts(const ScriptBuildRequest& r);
// MSBuild の出力のうちエラーの行 (", error " / ": error ")。末尾のプロジェクトのパスは除く。
std::vector<std::string> ErrorLines(const std::string& output);

}  // namespace rm::game_production
