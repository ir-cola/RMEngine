#pragma once
// 書き出したゲームのフォルダを作る (設計 NewEngineBluePrint 11-b / 11-b-2、RME_PLATFORM 5-a
// "書き出しを作り直す" 1-7)。どのソフトも知らない: 書き出す側のソフト (ゲーム) が、入れるもの
// - プラグイン、.rmpak 用のファイル、名前とアイコン、スクリプトの DLL - を渡す。ワーカースレッドで動く。
//
//   1  エディターの隣のゲームキット (GameKit/kit.json: 核と各プラグインの runtime の .lib)
//   2  生成する main (ExportPlan: GameMainSource) とリソース (名前、アイコン): Visual Studio の
//      cl / rc でコンパイルする (vswhere で探し、環境は vcvars64.bat から)
//   3  リンク: main、リソース、core.lib、指定したプラグインの lib だけ -> <exe 名>.exe。
//      その名前のまま作る (コピーして改名はしない)。.pdb なし
//   4  Game.rmpak (core/pak)、Scripts.dll、Visual Studio の C++ ランタイム DLL (アプリ内コピー:
//      再頒布パッケージが無い PC でも遊べる)
//   5  exe を調べて、持ってはいけないこの PC のパス (プロジェクトのもの、エンジンのもの) が無いか見る

#include "core/PluginId.h"
#include "core/pak/Pak.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor::exporter {

struct Request {
    std::filesystem::path outDir;     // 無ければ作る
    std::string exeName;              // ".exe" なし (ExeNameProblem は何も言わない)
    std::string titleUtf8;            // exe の説明と製品名
    std::vector<PluginId> plugins;    // ExportPlugins の順 (前提が先)
    std::vector<pak::Entry> files;    // Game.rmpak に入れる
    std::optional<std::string> iconPng;
    std::filesystem::path scriptsDll;  // 空: ゲームにスクリプトはない
    std::vector<std::string> privatePaths;  // UTF-8: ゲームが名前を出してはいけないこの PC のフォルダ
};

// 進み具合 0..1 と、ログ用の 1 行 (日本語)。
using Report = std::function<void(float progress, const std::string& line)>;

// フォルダを作る。作れなかったら false と *error (日本語、ツール自身の言葉つき)。
bool Run(const Request& request, const Report& report, std::string* error);

// ゲームキットのフォルダ (エディターの exe の隣)。
std::filesystem::path KitFolder();

}  // namespace rm::editor::exporter
