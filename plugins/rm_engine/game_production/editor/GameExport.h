#pragma once
// Exporting a game (the 書き出し panel of the ゲーム software, and the check's command line): what goes
// into the exported folder - the game's data and scenes, only the assets they use, the plugins they use
// - handed to the core's exporter (editor/export/Exporter.h), which links the exe.
//
//   .rmpf  game data  "export": { "exeName": "…" }            (ExportSettings: project data, undone)
//   UserSettings/Layout.json  the game's view  "export": { "output": "…" }   (where to: this PC's, never project data)

#include "base/Json.h"
#include "base/Ref.h"
#include "core/PluginId.h"
#include "core/reflect/Reflect.h"
#include "editor/export/Exporter.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rm::game_production {

RM_STRUCT()
struct ExportSettings {
    RM_PROPERTY() std::string exeName;  // empty: made from the game's name (shown in the field)
};

// What the 書き出し panel shows and edits: the game's name (= the game settings' ゲームの名前), the exe's
// name, where to. Filled from the data every time it is drawn; an edit goes where the value lives.
RM_STRUCT(Label="書き出し")
struct ExportView {
    RM_PROPERTY(Edit, Label="ゲームの名前", Tooltip="ウィンドウのタイトルと exe の説明に使います (ゲームの設定の「ゲームの名前」と同じもの)") std::string title;
    RM_PROPERTY(Edit, Label="exe の名前", Tooltip="書き出す exe のファイルの名前 (.exe は付けない)。空にするとゲームの名前から作ります") std::string exeName;
    RM_PROPERTY(Edit, Label="出力先", Tooltip="書き出すフォルダ。この PC の設定で、プロジェクトのデータには入りません") std::string output;
};

// What one export is made from (the open project as it is now - not only as saved).
struct GameExportInput {
    std::string title;
    std::string exeName;
    std::filesystem::path outDir;
    json::Value settings;  // GameSettings as saved (its unknown keys too)
    // The scenes: path from the project folder ("Assets/Scenes/Main.rmscene") and the file's text.
    std::vector<std::pair<std::string, std::string>> scenes;
    size_t open = 0;
    AssetRef icon;
    std::vector<PluginId> plugins;          // ExportPlugins' order
    std::filesystem::path projectRoot;
    std::function<std::filesystem::path(const AssetRef&)> assetFile;  // an asset's file now (empty: not there)
    std::filesystem::path scriptsDll;       // the newest built (empty: none)
};

// The asset ids written in `v` (scenes, prefabs): every string that is an asset id, each once.
std::vector<std::string> AssetIdsIn(const json::Value& v);

// The exporter's request: game.json, the scenes, every asset they use (a prefab's own too, through
// the prefabs they place), the icon. False and *error (Japanese) when something cannot be read or a
// path of this PC would be written into the game.
bool MakeRequest(const GameExportInput& in, editor::exporter::Request& out, std::string* error);

}  // namespace rm::game_production
