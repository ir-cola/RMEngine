#pragma once
// エンジンがファイルを置く場所 - ここに 1 度だけ書く (Hub のオプションのページが見せる)。
//
//   <engine>/Project/                       新しいプロジェクト (既定)   engine = platform::EngineDirectory()
//   <exe>/Plugins/<author>/<name>/          このエンジンが持つプラグイン
//   %LOCALAPPDATA%\RM_Engine\Engines\<key>\Plugins\   このエンジン専用の、入っているプラグイン (EngineKey.h:
//                                           エンジンのビルド / コピーごとに別)
//   %LOCALAPPDATA%\RM_Engine\Plugins\       前はどのエンジンもここに置いた (1 度だけ読む:
//                                           そこに入っていた物を、新しいエンジンが最初に入れる)
//   %LOCALAPPDATA%\RM_Engine\Logs\          editor.log
//   %APPDATA%\RM_Engine\hub.json            Hub の設定 (HubSettings)
//   %APPDATA%\RM_Engine\Layouts.json        全プロジェクトで共通のパネル配置 (ソフトごと)

#include <filesystem>

namespace rm::editor::paths {

std::filesystem::path DefaultProjects();
std::filesystem::path EnginePlugins();
std::filesystem::path InstalledPlugins();
std::filesystem::path LegacyInstalledPlugins();
std::filesystem::path LogFolder();
std::filesystem::path LogFile();
std::filesystem::path HubSettingsFile();
std::filesystem::path SharedLayoutsFile();
std::filesystem::path LangFolder();      // <exe>/Lang (核の翻訳)
std::filesystem::path HelpFolder();      // <exe>/Help (核のヘルプ。リポジトリの resources/help)
std::filesystem::path FileList();        // <exe>/RM_Engine.files.json (整合性の確認用)

}  // namespace rm::editor::paths
