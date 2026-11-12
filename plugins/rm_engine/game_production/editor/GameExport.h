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
    std::filesystem::path iconFile;         // the icon's file (empty: none) - found on the UI thread
};

// The asset ids written in `v` (scenes, prefabs): every string that is an asset id, each once.
std::vector<std::string> AssetIdsIn(const json::Value& v);

// --- Baking (design RME_PLATFORM 5-a: ゲーム用の形に変える) ---------------------------------------
// An asset the game uses, found in the project (on the UI thread: the ids are the editor's).
struct ExportAsset {
    std::string id;
    std::filesystem::path file;
    std::string rel;  // from the project folder ("Assets/Models/house.gltf")
    std::filesystem::path projectRoot;
};
enum class BakeResult { NotMine, Done, Failed };
struct ExportBaked {
    std::vector<pak::Entry> files;  // into the .rmpak (paths of the game's own: "Baked/…")
    std::string path;               // the one the game reads the asset by (game.json "assets": id -> path)
    std::string note;               // a line for the export's log
};
// RuntimeRegistry point: items are ExportBaker (static), offered by the editor part of a plugin that
// knows a kind of asset (ゲーム制作 × 3D: 3D models and their pictures, images as DDS). Asked in order
// for every asset; the first that does not say NotMine bakes it. An asset nobody bakes goes in as its
// file. Runs on the export's worker thread (only files are read).
inline constexpr char kExportBakePoint[] = "rm_engine.game_production.export_bake";
struct ExportBaker {
    BakeResult (*bake)(const ExportAsset& asset, ExportBaked& out, std::string* error) = nullptr;
};

// Every asset the scenes use (the prefabs they place, followed, bring theirs), with its file now -
// on the UI thread. Ids of no asset (an entity's name that looks like one, a file that is gone) and
// files outside the project are left out.
std::vector<ExportAsset> CollectAssets(const GameExportInput& in);

// The exporter's request: game.json, the scenes, the assets (baked by the first baker that takes
// each, else as their files), the icon (`iconFile`, read as it is). On the worker thread. False and
// *error (Japanese) when something cannot be read or baked, or a path of this PC would be written into
// the game. `note` gets a line per asset baked.
bool MakeRequest(const GameExportInput& in, const std::vector<ExportAsset>& assets, const std::vector<const ExportBaker*>& bakers,
                 editor::exporter::Request& out, const std::function<void(const std::string&)>& note, std::string* error);

}  // namespace rm::game_production
