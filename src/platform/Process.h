#pragma once
// 別のプログラムを動かして、その出力を読む (書き出しは Visual Studio のコンパイラ、
// リソースコンパイラ、リンカーを動かす。vswhere が Visual Studio を探す)。終わるまで待つ。

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace rm::platform {

struct ProcessResult {
    bool started = false;   // false: プログラムを起動できなかった (*error に理由)
    unsigned long exitCode = 0;
    std::string output;     // stdout と stderr を、プログラムが書いたとおりにまとめたもの (バイト列)
};

// `commandLine`: プログラム (空白があれば引用符で囲む) とその引数。`environment`: 渡す環境全体
// (空: このプロセスのもの)。ウィンドウは出さない。
// それが起こしたものは全部、一緒に終わる (ジョブオブジェクト): 終わったとき、残っているもの
// - MSBuild のワーカーノード、バックグラウンドで起こしたプログラム - も終わるので、プロジェクトの
// フォルダを開いたままにするもの (消せなくなる) や、この呼び出しを待たせ続けるものは残らない。
// このプロセスが先に終わったときも、それらは終わる。
ProcessResult RunProcess(const std::wstring& commandLine, const std::filesystem::path& workingDir,
                         const std::map<std::wstring, std::wstring>& environment, std::string* error);

// このプロセスの環境 (名前は Windows が持つとおり)。
std::map<std::wstring, std::wstring> CurrentEnvironment();

// コンソールのコードページ (cl / link はそれで出す) のプログラム出力 -> UTF-8。
std::string ConsoleTextToUtf8(const std::string& bytes);

}  // namespace rm::platform
