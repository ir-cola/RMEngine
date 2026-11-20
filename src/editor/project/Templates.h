#pragma once
// プロジェクトのテンプレートとその木 (設計 RME_PLATFORM 1-b-2 / 5-a)。純粋。RMTests でテストする。
//
// テンプレートはコードではなくデータだ: プラグインは、自分のフォルダーにファイルを置いて出す。
//
//   plugins/<author>/<name>/templates/<id>/template.json
//   {
//     "kind": "rm_engine.template", "format": 1,
//     "id": "game_3d",                       プラグイン内で一意 (新しい .rmpf に記録する)
//     "path": ["game", "3d"],                木のどこか: 上の枝、そして自分のノード
//     "name": "3D のゲーム", "description": "…",   日本語 (描くときに Tr。英語はプラグインの lang/en.json)
//     "order": 0,                            兄弟の中で (小さい方が先。同じなら名前順)
//     "plugins": [ { "author": …, "name": … } ],   中身が必要とするもの (自分のプラグインは足される)
//     "open": { "author": …, "plugin": …, "software": "game" },   最初に出すソフト
//     "project": { "softwareData": [ … ] },  新しい .rmpf に入れるキー (最初のシーン: カメラ、ライト、床 ...)
//     "userSettings": { "softwares": [ { "author": …, "plugin": …, "software": …, "layout": …, "view": … } ] }
//   }                                        最初のレイアウト / 見え方。UserSettings/Layout.json に入れる
//   plugins/<author>/<name>/templates/<id>/Assets/…   新しいプロジェクトの Assets/ に写すファイル (その
//                                                      .meta も: ゲームの最初のシーン。"project" の中で ID で指す)
//
// 核は、このエンジンが持つプラグインのファイルを - 入っていても入っていなくても - 読むので、
// プラグインが入る前でもテンプレートは木に出る (Hub が入れるか尋ねる)。核はプラグインも
// その データの意味も知らない: "project" と "userSettings" をそのまま写す。
//
// 木はパスから作る。枝は見せ方でしかない (自分のテンプレートが無いノードは、その ID を出す)。
// ノードを選ぶには、そのテンプレートと、その下のすべてのテンプレートのプラグインが全部要る
// (設計 09-29: 「ゲーム」は 3D と 2D の両方を持ってくる) - プロジェクトの始まりは、そのノード自身のテンプレート。

#include "base/Json.h"
#include "core/PluginId.h"
#include "editor/plugin/PluginManifest.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor {

inline constexpr char kTemplateKind[] = "rm_engine.template";
inline constexpr int64_t kTemplateFormat = 1;

struct ProjectTemplate {
    std::optional<PluginId> plugin;  // 出すプラグイン。なし: 核自身のもの (白紙)
    std::string id;
    std::vector<std::string> path;   // 最後の部分が自分のノード
    std::string name;                // UTF-8 の日本語
    std::string description;
    int order = 0;
    std::vector<PluginId> plugins;   // 中身が必要とするもの (最初が `plugin`)
    json::Value open;                // { author, plugin, software } か null
    json::Value project;             // オブジェクトか null
    json::Value userSettings;        // オブジェクトか null
    // その template.json のフォルダー (ローダーが入れる。ファイルからは読まない): そこにある
    // "Assets" フォルダーは新しいプロジェクトに写す (ゲームの最初のシーン: Assets/Scenes/Main.rmscene + .meta)。
    std::filesystem::path folder;
};

// 核自身のテンプレート: 何も入っていないプロジェクト (プラグインは要らない)。
const ProjectTemplate& BlankTemplate();

// `owner` のフォルダーにある 1 つの template.json。知らないキーは許す (新しいエンジンのもの)。
// 新しい format、形が違う、ID やパスが不正なものは、理由 (日本語) を *error に入れて断る。
std::optional<ProjectTemplate> ParseTemplate(const json::Value& v, const PluginId& owner, std::string* error);

struct TemplateNode {
    std::vector<std::string> path;
    size_t depth = 0;                       // path.size() - 1
    size_t parent = static_cast<size_t>(-1);  // 親のノードの番号。一番上では npos
    int tmpl = -1;                          // TemplateTree::templates の番号。-1: 枝だけ
};

struct TemplateTree {
    std::vector<ProjectTemplate> templates;
    std::vector<TemplateNode> nodes;  // 親が子より先、兄弟は順に (表示の順)
    std::vector<std::string> warnings;  // 外したテンプレート (同じ場所に 2 つ、不正な ID ...)

    const ProjectTemplate* At(size_t node) const {
        return node < nodes.size() && nodes[node].tmpl >= 0 ? &templates[static_cast<size_t>(nodes[node].tmpl)] : nullptr;
    }
    // ノードが出す文字: そのテンプレートの名前、または (枝だけなら) その ID。UTF-8。Tr の前。
    std::string Label(size_t node) const;
    size_t Find(const std::vector<std::string>& path) const;  // そこに無ければ npos
};

// これらのテンプレートの木。同じ場所に 2 つあれば最初のものを残し、もう 1 つは警告にする。
// 1 つのプラグインの同じ ID の 2 つも同じ。
TemplateTree BuildTemplateTree(std::vector<ProjectTemplate> templates);

// `node` からプロジェクトを作るのに必要なもの: そのテンプレートと、その下のすべてのテンプレートの
// プラグイン。それぞれ 1 回で、木の順。
std::vector<PluginId> TemplatePlugins(const TemplateTree& tree, size_t node);

// `needed` を必要とするテンプレートからプロジェクトを作る前に写すもの: それぞれの入れる計画
// (前提とコネクター、InstallPlan) をまとめ、それぞれ 1 回だけ。このエンジンに無い
// プラグインは *unavailable に入る。
std::vector<PluginId> TemplateInstallPlan(const std::vector<PluginId>& needed, const std::vector<PluginManifest>& available,
                                          const std::vector<PluginId>& ready, std::vector<PluginId>* unavailable);

// テンプレートから作る、新しいプロジェクトの .rmpf (kind、name ... は設定済み): テンプレートを
// 記録し ("template": { author, plugin, id })、その "project" のキーを入れる - ただし核自身の
// キー (kind、formatVersion、name、engineVersion、template、plugins) は除く - そして、
// 各 "softwareData" の項目のプラグインを "plugins" に記録する (そのソフトのデータを保存したときと同じ)。
void ApplyTemplate(json::Value& doc, const ProjectTemplate& t);

// テンプレートから作る、新しいプロジェクトの UserSettings/Layout.json ("active" は "open" から、
// "softwares" の項目は "userSettings" から)。どちらについてもテンプレートが何も言わなければ null。
json::Value TemplateLayout(const ProjectTemplate& t);

// --- 「ソフトで絞る」(Hub のプラグイン一覧) ---------------------------------------------------
// ソフト = テンプレートの木の一番上のノード。そのプラグイン: その下のテンプレートが必要とする
// プラグインすべて (入れたときに来る前提とコネクターも)と、plugin.json に "software": [その ID]
// と書いてあるプラグインすべて。plugin.json にあるソフトで、その ID のノードが無いものは、
// ほかの後ろに、その ID の名前で出す。プラグインが要らないノード (白紙) はソフトではない。
// どのソフトにも無いプラグインは 共通。
struct SoftwareGroup {
    std::string id;
    std::string name;  // UTF-8。Tr の前
    std::vector<PluginId> plugins;
};
std::vector<SoftwareGroup> GroupPluginsBySoftware(const TemplateTree& tree, const std::vector<PluginManifest>& available);

}  // namespace rm::editor
