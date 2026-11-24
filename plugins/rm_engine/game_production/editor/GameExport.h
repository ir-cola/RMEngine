#pragma once
// ゲームの書き出し (ゲームソフトの書き出しパネルと、チェックのコマンドライン): 書き出し先のフォルダに
// 入るもの - ゲームのデータとシーン、それが使うアセットだけ、それが使うプラグイン -
// 核のエクスポーター (editor/export/Exporter.h) に渡し、それが exe をリンクする。
//
//   .rmpf  ゲームのデータ  "export": { "exeName": "…" }            (ExportSettings: プロジェクトのデータ、Undo できる)
//   UserSettings/Layout.json  ゲームのビュー  "export": { "output": "…" }   (出力先: この PC のもの、プロジェクトのデータではない)

#include "base/Json.h"
#include "base/Ref.h"
#include "core/PluginId.h"
#include "core/reflect/Reflect.h"
#include "editor/export/Exporter.h"
#include "rm_engine/game_production/editor/ScriptBuild.h"
#include "rm_engine/game_production/runtime/SceneFile.h"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rm::game_production {

RM_STRUCT()
struct ExportSettings {
    RM_PROPERTY() std::string exeName;  // 空: ゲームの名前から作る (欄に表示される)
};

// 書き出しパネルが見せて編集するもの: ゲームの名前 (= ゲームの設定の ゲームの名前)、exe の名前、
// 出力先。描くたびにデータから埋める。編集は値のある所へ行く。
RM_STRUCT(Label="書き出し")
struct ExportView {
    RM_PROPERTY(Edit, Label="ゲームの名前", Tooltip="ウィンドウのタイトルと exe の説明に使います (ゲームの設定の「ゲームの名前」と同じもの)") std::string title;
    RM_PROPERTY(Edit, Label="exe の名前", Tooltip="書き出す exe のファイルの名前 (.exe は付けない)。空にするとゲームの名前から作ります") std::string exeName;
    RM_PROPERTY(Edit, Label="出力先", Tooltip="書き出すフォルダ。この PC の設定で、プロジェクトのデータには入りません") std::string output;
};

// 1 回の書き出しの材料 (開いているプロジェクトの今の状態 - 保存されたものだけではない)。
struct GameExportInput {
    std::string title;
    std::string exeName;
    std::filesystem::path outDir;
    json::Value settings;  // 保存されたままの GameSettings (知らないキーも)
    // シーン: プロジェクトのフォルダからのパス ("Assets/Scenes/Main.rmscene") とファイルの中身を、
    // 一覧の順に (ScenesForExport)。ゲームは `first` から始まる。
    std::vector<std::pair<std::string, std::string>> scenes;
    size_t first = 0;
    AssetRef icon;
    std::vector<PluginId> plugins;          // ExportPlugins の順
    std::filesystem::path projectRoot;
    std::function<std::filesystem::path(const AssetRef&)> assetFile;  // 今のアセットのファイル (空: ない)
    // ゲームのスクリプト。書き出しのワーカーでそのためにビルドする (Release、デバッグ情報なし:
    // ScriptBuildRequest::forExport)。ソースがなければ Scripts.dll も作らない。ビルドが失敗したら
    // 書き出しは止まる。
    ScriptBuildRequest scripts;
    std::filesystem::path iconFile;         // アイコンのファイル (空: なし) - UI スレッドで見つける
};

// ゲームに入るシーン (GameScenes: 一覧のもの、一覧になければ開いているもの) とそのファイルの
// 中身: 開いているものは今の状態 (`openPath`, `openText`)、ほかは `read` が返すもの
// (パス, 中身。nullopt: 外す、その id は `unread` に入る)。`first`: 一覧の最初のシーンの位置
// (FirstScene) をそれらの中で - 開いているものが外れるときはその位置。
struct ExportScenes {
    std::vector<std::pair<std::string, std::string>> scenes;
    size_t first = 0;
    std::vector<std::string> unread;
};
ExportScenes ScenesForExport(const SceneList& list, const AssetRef& open, const std::string& openPath, const std::string& openText,
                             const std::function<std::optional<std::pair<std::string, std::string>>(const AssetRef&)>& read);

// `v` に書かれたアセットの id (シーン、Prefab): アセットの id である文字列すべて、それぞれ 1 回。
std::vector<std::string> AssetIdsIn(const json::Value& v);

// --- ベイク (設計 RME_PLATFORM 5-a: ゲーム用の形に変える) ---------------------------------------
// ゲームが使うアセットで、プロジェクトの中で見つけたもの (UI スレッドで: id はエディターのもの)。
struct ExportAsset {
    std::string id;
    std::filesystem::path file;
    std::string rel;  // プロジェクトのフォルダから ("Assets/Models/house.gltf")
    std::filesystem::path projectRoot;
    // このゲームに入るアセット全部の id -> ファイル (CollectAssets が作る。ほかのアセットを名指すアセット -
    // マテリアルの絵 - を焼くときに引く。ワーカーのスレッドでも読める写し)。
    std::shared_ptr<const std::map<std::string, std::filesystem::path>> files;
};
enum class BakeResult { NotMine, Done, Failed };
struct ExportBaked {
    std::vector<pak::Entry> files;  // .rmpak に入れる (ゲーム自身のパス: "Baked/…")
    std::string path;               // ゲームがそのアセットを読むときのパス (game.json "assets": id -> パス)
    std::string note;               // 書き出しのログの 1 行
};
// RuntimeRegistry の口: 項目は ExportBaker (static)。ある種類のアセットを知っているプラグインの
// エディター側が出す (ゲーム制作 × 3D: 3D モデルとその絵、画像は DDS)。アセットごとに順に尋ね、
// NotMine と言わない最初のものが焼く。誰も焼かないアセットはファイルのまま入る。書き出しの
// ワーカースレッドで動く (ファイルを読むだけ)。
inline constexpr char kExportBakePoint[] = "rm_engine.game_production.export_bake";
struct ExportBaker {
    BakeResult (*bake)(const ExportAsset& asset, ExportBaked& out, std::string* error) = nullptr;
    // ゲームがこのベイカーのプラグインを必要とするアセットなら true (音: 音声 - どのシーンにもその
    // 部品がなくても (スクリプトの欄)、ゲームはそのプラグインで鳴らす)。そのプラグインがゲームに入る。
    // null: 決して (ゲームがどのみち持つプラグイン: 空間のもの)。UI スレッドで。
    bool (*needsPlugin)(const ExportAsset& asset) = nullptr;
};

// シーンが使うすべてのアセット (置く Prefab、たどったもの、そのアセットも)を、今のファイルと
// ともに - UI スレッドで。アセットでない id (エンティティの名前でそう見えるもの、なくなった
// ファイル) とプロジェクトの外のファイルは外す。
std::vector<ExportAsset> CollectAssets(const GameExportInput& in);

// エクスポーターの依頼: game.json、シーン、アセット (それぞれを受け取る最初のベイカーが焼く。
// なければファイルのまま)、アイコン (`iconFile`、そのまま読む)。ワーカースレッドで。何かが
// 読めない・焼けない、またはこの PC のパスがゲームに書かれてしまうときは false と *error (日本語)。
// `note` には焼いたアセットごとに 1 行入る。
bool MakeRequest(const GameExportInput& in, const std::vector<ExportAsset>& assets, const std::vector<const ExportBaker*>& bakers,
                 editor::exporter::Request& out, const std::function<void(const std::string&)>& note, std::string* error);

}  // namespace rm::game_production
