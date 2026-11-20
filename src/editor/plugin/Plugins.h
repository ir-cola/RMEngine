#pragma once
// このエンジンのプラグイン。Hub と全プロジェクトで共有する
// (設計 RME_PLATFORM 5-a: Hub で入れたプラグインは全プロジェクトで動く)。
//
//   available   <exe folder>/Plugins/<author>/<name>/          このエンジンにあるもの (エンジンに付いてくる)
//   installed   %LOCALAPPDATA%\RM_Engine\Engines\<engine>\Plugins\<author>\<name>\   ユーザーが入れたもの
//
// 入れるときはプラグインのフォルダーを "available" から "installed" に写し、消すときはそれを消し、
// 入れ直すときはもう一度写す ("入っていない" を経由せずに、壊れたファイルを直せる)。
// コード自体は exe に組み込まれる (静的リンク): 入っていない、または前提が入っていない
// プラグインは、初期化されないだけだ。最初の起動で、ある物をすべて入れる。

#include "editor/plugin/PluginManifest.h"
#include "editor/plugin/Software.h"
#include "editor/project/Templates.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rm::editor {

class Plugins {
public:
    struct Entry {
        PluginStatus status;     // ID、表示名、状態、理由
        bool available = false;  // このエンジンにファイルがあるので、(再) インストールできる
        bool installed = false;
        // コネクター (その plugin.json の connectorFor): 手では選ばない - つなぐ物が入ると一緒に来る。
        // 別に表示し (Hub)、入れる候補には出さない (エディターのメニュー)。
        bool connector = false;
        std::vector<PluginId> joins;  // 前提とするもの (つなぐ両側)
    };

    // このエンジンの最初の起動 (installedDir が無い) では、`legacyDir` に入っているもの
    // (前は全エンジンで共有していたフォルダー) を入れる。何も無ければ、ある物すべて。
    void Load(const std::filesystem::path& availableDir, const std::filesystem::path& installedDir,
              const std::filesystem::path& legacyDir = {});
    // 全部を読み直してレジストリーを作り直す。エディターが 1 つも開いていないときだけ
    // (開いているワークスペースはレジストリーを指している)。
    void Reload();

    const std::vector<Entry>& Entries() const { return entries_; }
    // このエンジンが持つ全プラグインの plugin.json (入っていてもいなくても)。
    const std::vector<PluginManifest>& Available() const { return available_; }
    const Entry* Find(const PluginId& id) const;
    bool IsEnabled(const PluginId& id) const;
    std::string DisplayName(const PluginId& id) const;  // 不明なプラグインは ID の文字
    // 入っているプラグインのファイルの場所 (そのテーマファイル: themes/*.json)。
    std::filesystem::path InstalledFolder(const PluginId& id) const;
    const PluginRegistry& Registry() const { return registry_; }

    // プロジェクトのテンプレート (Hub の新規作成): 白紙と、このエンジンが持つプラグインの
    // 全 template.json - 入っていても入っていなくても (<engine plugins>/<author>/<name>/templates/<id>/template.json)。
    const TemplateTree& Templates() const { return templates_; }
    // テンプレートの木のソフトと、そのプラグイン (Hub の「ソフトで絞る」)。
    const std::vector<SoftwareGroup>& SoftwareGroups() const { return softwareGroups_; }
    // テンプレートの木の `node` からプロジェクトを作る前に写すもの (そのプラグインと、その下の
    // もの、前提とコネクターも)。このエンジンに無い物は *unavailable へ。
    std::vector<PluginId> TemplateInstallPlan(size_t node, std::vector<PluginId>* unavailable = nullptr) const;

    // `id` を入れる (または直す) ときに写すもの: それと、その前提のうち、まだ入っていない、または
    // 使えないもの。前提が先。このエンジンに無い前提は *unavailable へ。
    std::vector<PluginId> InstallPlan(const PluginId& id, std::vector<PluginId>* unavailable = nullptr) const;
    // `id` が無いと動かなくなる、入っているプラグイン (直接のものが先、次に間接のもの)。
    std::vector<Dependent> Dependents(const PluginId& id) const;

    // ファイルの操作。再読み込みはしない: まとめて行った後に Reload() を呼ぶ。
    bool Install(const std::vector<PluginId>& ids, std::string* error);  // 入っているものの入れ直しもする
    bool Uninstall(const PluginId& id, std::string* error);

    // 起動のたび、Load の後に: エンジンが更新されていて (前回の起動からファイルの一覧が変わった)、
    // 入っているプラグインのファイルがエンジンの写しと違うなら、もう一度写す - Hub の
    // 再インストールと同じことを、聞かずにする (ユーザーのファイルではなく、エンジンのファイルだから)。
    // ユーザーが消したプラグインは消えたまま。印 (ファイル一覧の SHA-256) は
    // <installed>/engine_files.sha256 に残す。
    //   PlanEngineRefresh   どのスレッドからでも (App が Worker で走らせる: 入っている全ファイルのハッシュを取る) - 読むだけ
    //   ApplyEngineRefresh  UI スレッド、エディターが 1 つも開いていないとき: 写し、印を書き、
    //                       何か写したら再読み込みする。写したものを返す。
    struct EngineRefreshPlan {
        bool looked = false;                  // エンジンが変わった: `refresh` を計算した
        std::optional<std::string> listHash;  // ファイル一覧の (nullopt: 一覧が無い - 印を付けるものは無い)
        std::string listError;                // 一覧が読めなかった (plugin.json だけ比べた)
        std::vector<PluginId> refresh;
    };
    std::vector<PluginId> InstalledAndAvailable() const;
    const std::filesystem::path& AvailableDir() const { return availableDir_; }
    const std::filesystem::path& InstalledDir() const { return installedDir_; }
    static EngineRefreshPlan PlanEngineRefresh(const std::filesystem::path& fileList, const std::filesystem::path& availableDir,
                                               const std::filesystem::path& installedDir, const std::vector<PluginId>& candidates);
    std::vector<PluginId> ApplyEngineRefresh(const EngineRefreshPlan& plan);

private:
    bool CopyIn(const PluginId& id, std::string* error);
    void LoadTemplates();

    std::filesystem::path availableDir_, installedDir_;
    std::vector<PluginManifest> available_;  // エンジンの写しの manifest
    std::vector<PluginManifest> installed_;  // 読めた、入っている写しの manifest
    std::vector<Entry> entries_;             // exe に組み込まれたプラグイン 1 つにつき 1 つ
    TemplateTree templates_;
    std::vector<SoftwareGroup> softwareGroups_;
    PluginRegistry registry_;
};

}  // namespace rm::editor
