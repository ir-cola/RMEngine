#pragma once
// Hub: プロジェクトを選ぶ・作る・開く、そしてプラグインを管理する (インストール / 入れ直し / 削除)。
// プラグインを変えられるのはここと、エディターのプラグインメニュー (インストールだけ) だけ -
// プロジェクトを開いている間に削除することは絶対に起こらない (設計 RME_PLATFORM 5-a)。

#include "core/PluginId.h"
#include "editor/app/ScreenWindow.h"
#include "editor/app/Worker.h"
#include "editor/help/HelpWindow.h"
#include "editor/app/Integrity.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/widgets/Widgets.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rm::editor {

class HubScreen final : public ScreenWindow {
public:
    explicit HubScreen(App& app) : ScreenWindow(app) {}
    bool Open();
    void OnLookChanged() override;  // 前に作った文字 (一覧) を、新しい言語で作り直す
    void ShowError(std::wstring message, std::filesystem::path removableFile = {}, std::wstring title = Tr(L"開けませんでした"));
    // プロジェクトを開く前に App が聞く。
    //   blocking: プロジェクトが使えないプラグインを使っている -> "起動できません。前提プラグインをインストールしますか？"
    //   limited:  インストール済みのプラグインに前提が足りない -> 1 回だけ知らせる、[インストール] [このまま開く]
    void AskBlockingPlugins(const std::filesystem::path& project, const std::vector<PluginId>& blocking);
    void AskLimitedPlugins(const std::filesystem::path& project, const std::vector<PluginId>& limited);

protected:
    void OnCreated() override;
    void OnDeviceChanged() override;  // ロゴを作り直す
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;

private:
    enum class Page { Projects, Plugins, Updates, Options };
    enum class Modal { None, NewProject, Rename, Error, Plugin, Integrity, Theme };
    // 作成: 新しいプロジェクトのテンプレートに、インストールしていないプラグインが必要 (Blocking と同じ聞き方)。
    enum class PromptKind { Blocking, Limited, Install, Remove, Create };

    // プラグインの質問は 1 度に 1 つ (設計: ポップアップは 1 つずつ)。
    struct Prompt {
        PromptKind kind = PromptKind::Install;
        std::wstring title;
        std::wstring body;
        std::filesystem::path project;    // Blocking / Limited: 後で開く
        PluginId target;                  // Remove
        std::vector<PluginId> toInstall;  // インストールボタンがコピーするもの
        std::vector<PluginId> limited;    // Limited: 「今後表示しない」が覚えておく組
        bool hideAgain = false;
    };

    struct Row {
        std::filesystem::path file;
        std::wstring name;
        std::wstring folder;
        int64_t modifiedSort = 0;
        bool missing = false;
        bool loading = true;              // まだ読んでいない (Worker がファイルを読む: 遅いドライブでも描画は止まらない)
        std::vector<PluginId> plugins;    // プロジェクトが使っていると記録しているもの (「N 個がこのプラグインを使っています」用)
    };
    // Worker が行のために読むもの (そのスレッドで: ファイルだけ)。
    struct RowData {
        std::filesystem::path file;
        std::wstring name;
        int64_t modifiedSort = 0;
        bool missing = false;
        std::vector<PluginId> plugins;
    };
    static RowData ReadRow(const std::filesystem::path& file);
    void ApplyRow(const RowData& d);

    // 最近の一覧をディスクから読み直す (起動、F5、名前の変更 / 一覧から外した後)。
    // 行はすぐ表示し ("読み込み中…")、Worker が読むにつれて埋まっていく。
    void Refresh();
    void SortRows();
    std::vector<size_t> FilteredRows() const;

    void DrawSidebar(ui::Context& c, ui::Rect r);
    void DrawProjects(ui::Context& c, ui::Rect r);
    void DrawPlugins(ui::Context& c, ui::Rect r);
    // プラグインの下の、同じスクロールする行の中 (`area` はその下へ動く)。
    void DrawConnectors(ui::Context& c, ui::Rect& area, const ui::Rect& view);
    // ダウンロードできるプラグイン (App::Catalog): 入っていない物・新しい版がある物と、一覧を読めたか。
    void DrawDownloads(ui::Context& c, ui::Rect& area, const ui::Rect& view);
    void DrawOptions(ui::Context& c, ui::Rect r);
    void DrawUpdates(ui::Context& c, ui::Rect r);
    void DrawIntegrity(ui::Context& c);
    void DrawThemePicker(ui::Context& c);
    void RunIntegrityCheck();
    void DrawNewProject(ui::Context& c);
    void DrawRename(ui::Context& c);
    void DrawError(ui::Context& c);
    void DrawRowMenu(ui::Context& c);
    void DrawPluginPrompt(ui::Context& c);

    void BeginInstall(const PluginId& id);
    void BeginRemove(const PluginId& id);
    void RunInstall(const std::vector<PluginId>& ids);
    std::wstring PluginLine(const PluginId& id) const;
    std::wstring PluginNames(const std::vector<PluginId>& ids) const;
    // もう読んだ行から (描画中はディスクに触らない)。まだ読んでいる行は数えない。
    int RecentProjectsUsing(const PluginId& id) const;
    void ShowPrompt(Prompt prompt);

    void OpenRow(const Row& row);
    void BrowseOpen();
    void BeginNewProject();
    void BrowseLocation();
    // 作成: 名前と場所を確かめ、テンプレートに足りないプラグインがあれば聞く (Prompt Create)、
    // なければそのままプロジェクトを作る。
    void CreateNewProject();
    bool CheckNewProject();     // 名前と場所 (だめなら newError_)
    void MakeNewProject();      // 選んだテンプレートから作って開く
    size_t NewNode() const;     // テンプレートの木で選んだノード (npos: なし)
    void DrawTemplateDetails(ui::Context& c, const ui::Rect& r, size_t node);
    // プラグインのページの「ソフトで絞る」: そのプラグインは選んだソフト (か 共通) に入っているか。
    bool PluginShown(const PluginId& id) const;
    // 名前の変更 (フォルダ + .rmpf + 中の名前)。Hub からだけ: 表示している間はプロジェクトを開いていない。
    void BeginRename(const Row& row);
    void RunRename();

    Page page_ = Page::Projects;
    Modal modal_ = Modal::None;
    std::vector<Row> rows_;
    int sortColumn_ = 1;  // 0 名前、1 更新日、2 場所
    bool sortAscending_ = false;
    std::filesystem::path selected_;
    std::string search_;

    Microsoft::WRL::ComPtr<ID2D1Bitmap> logo_;

    w::Popup rowMenu_;
    std::filesystem::path rowMenuFile_;

    // 新規プロジェクトのダイアログ
    std::string newName_;
    std::string newLocation_;
    std::vector<std::string> newPath_;  // 選んだテンプレートの木のノード (パスで覚える: インストールすると木は作り直される)
    std::wstring newError_;
    bool focusNewName_ = false;

    // 名前の変更ダイアログ
    std::filesystem::path renameFile_;
    std::wstring renameOld_;
    std::string renameName_;
    std::wstring renameError_;
    bool focusRename_ = false;

    Prompt prompt_;
    int pluginFilter_ = 0;  // 0 すべて, 1..n Plugins::SoftwareGroups()[i - 1], n + 1 共通

    // オプションのページ (オプションはインスペクターがその型から描く)
    inspector::State optionsInspector_;
    // 整合性チェック (エンジンの全ファイルのハッシュを取る: Worker で、進み具合つき)
    struct IntegrityResult {
        bool listRead = false;
        size_t files = 0;
        std::vector<integrity::FileProblem> problems;
        std::vector<PluginId> stale;
    };
    void FinishIntegrityCheck(const IntegrityResult& r);
    std::wstring integrityText_;
    std::vector<PluginId> integrityStale_;  // エンジンのコピーと違うインストール済みプラグイン
    bool integrityRunning_ = false;
    size_t integrityDone_ = 0, integrityTotal_ = 0;

    Worker rowsWorker_;       // 最近のプロジェクトを読む
    Worker integrityWorker_;  // エンジンのファイルのハッシュを取る

    // ヘルプの窓 (サイドバーの「ヘルプ」。1 つだけ: 開いていれば前に出す)。
    std::unique_ptr<HelpWindow> help_;
    void ShowHelp();

    // エラーダイアログ
    std::wstring errorTitle_;
    std::wstring errorText_;
    std::filesystem::path errorRemovable_;
};

}  // namespace rm::editor
