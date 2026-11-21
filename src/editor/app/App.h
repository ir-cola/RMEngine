#pragma once
// エディターのアプリ本体: GPU のデバイスを持ち、2 つの画面を切り替える。
//
//   Hub  --(プロジェクトを開く / 作る)-->  エディター
//   Hub  <--(エディターのウィンドウを閉じる)----  エディター
//
// Hub とエディターは同じ exe の中にある (出来上がったゲームは別の exe で、どちらも含まない)。
// 画面の切り替えは要求として受け、あとでメッセージループが行う (ウィンドウプロシージャの中では
// 決してしない)。そうすれば、ウィンドウ自身のハンドラーがまだ動いている間にそのウィンドウが
// 壊れることはない。

#include "editor/app/Worker.h"
#include "editor/plugin/PluginIndex.h"
#include "editor/plugin/Plugins.h"
#include "editor/project/HubSettings.h"
#include "platform/Gfx.h"

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <string>

namespace rm::editor {

class ScreenWindow;

// ダウンロードできるプラグインの一覧 (docs/PLUGIN_DISTRIBUTION.md 段階 1)。アプリを立ち上げたときに
// 1 度だけ読む (Worker で)。読めなくても、今入っているものはそのまま動く。
struct PluginCatalog {
    enum class State { Loading, Ready, Failed };
    State state = State::Loading;
    std::string url;                   // 読んだ所 (zip の相対 URL はここから)
    std::optional<PluginIndex> index;
    std::wstring error;                // Failed のとき
    std::optional<PluginId> downloading;  // 今落としている物
};

struct LaunchOptions {
    std::filesystem::path projectFile;  // --project <file.rmpf>: Hub を飛ばす (プロジェクトへ再起動するのに使う)
    // 書き出しの確認 (tools/check_export.ps1) のコマンドライン。ウィンドウなし。終わると終了コードは 0:
    //   --new-from-template <author> <plugin> <id> <folder>   そのテンプレートから作ったプロジェクト: <folder>/Project/Project.rmpf
    //   --export <file.rmpf> <folder>                          プロジェクトを <folder> に書き出す (先にスクリプトをビルドする)
    //   --export-template <author> <plugin> <id> <folder>     両方: <folder>/Project を作り、<folder>/Game に書き出す
    std::vector<std::wstring> newFromTemplate, exportProject, exportTemplate;
};

class App {
public:
    App();
    ~App();

    int Run(const LaunchOptions& options);
    // 書き出しの確認 (LaunchOptions::exportTemplate): その終了コード。
    int RunExportCheck(const LaunchOptions& options);

    platform::Gfx& Gfx() { return gfx_; }
    HubSettings& Settings() { return settings_; }
    const Plugins& GetPlugins() const { return plugins_; }

    // 開く前に、プロジェクトが記録しているプラグインを調べる (なければ開かない)。skipNotice でなければ、
    // Hub は、必要なものが足りない入っているプラグインがあることも 1 度だけ知らせる。
    void RequestOpenProject(const std::filesystem::path& file, bool skipNotice = false);
    // エディターのプラグインメニュー (エディターは保存した): 入れてから、同じプロジェクトをもう一度開く。
    void RequestInstallAndReopen(std::vector<PluginId> ids, const std::filesystem::path& project);
    // Hub だけ (エディターは開いていないこと): 入っているプラグインを変えて、読み直す。
    bool InstallPlugins(const std::vector<PluginId>& ids, std::string* error);
    bool UninstallPlugin(const PluginId& id, std::string* error);
    const PluginCatalog& Catalog() const { return catalog_; }
    // 今のプラグインで出す「ダウンロードできる物」(PluginOffers)。一覧が無ければ空。
    std::vector<PluginOffer> PluginOffersNow() const;
    // Hub だけ: 一覧の `id` の一番新しい合う版を落として入れる (Worker)。終わると読み直し、だめなら
    // Hub にエラーを出す。前提がこのエンジンにあって入っていなければ一緒に入れる。
    void DownloadPlugin(const PluginId& id);
    void RequestHub(std::string errorMessage = {});
    void RequestQuit();

    // オプション (色のテーマ、言語) を全ウィンドウに当てる。起動時と変更の後に。
    void ApplyOptions();
    // Windows の明るい / 暗い設定が変わった (「PC の設定に合わせる」のときだけ関係する)。
    void SystemThemeChanged();

    // GPU が失われた (Gfx::Lost): デバイスを作り直し、全ウィンドウをもう一度描く (各ウィンドウは
    // 次のフレームで自分の描画を作り直す)。今はできないなら false。頼んだウィンドウが少し後に
    // またやってみる。何度か失敗したら、ユーザーに知らせて保存を勧める。
    bool RecoverGraphics(ScreenWindow& asking);

private:
    enum class Pending { None, OpenProject, InstallAndReopen, Hub, Quit };
    void ApplyPending();
    void OpenProjectNow();
    void ShowHub(const std::string& errorMessage);
    class HubScreen* EnsureHub();
    void Replace(std::unique_ptr<ScreenWindow> next);
    void LoadLanguageCatalogs();
    // テーマパックのテーマ: 有効な全プラグインの themes/*.json (と、その絵。プラグインのコードが
    // w::kOrnamentPoint で出す)。プラグインを読むたびに。選んだテーマのパックがなくなったら、
    // オプションの最初のテーマに置き換える (以後ずっと)。
    void LoadThemes();
    // プラグインを (入れた / 消した後に) 読み直し、次にそのテーマ、次に見た目。
    void ReloadPlugins();
    // エンジンを更新した後: 入っているプラグインの新しいコピー (起動時に Worker で作り、
    // エディターが開いていないときに当てる。Hub ならすぐ、そうでなければ Hub に戻ったとき)。
    void StartEngineRefresh();
    void ApplyEngineRefreshIfReady();
    void StartCatalog();
    // 落とし終えた物を置く (エディターが開いていない間だけ。開いていれば Hub に戻ったとき)。
    void ApplyDownloadIfReady();
    struct FinishedDownload {
        PluginId id;
        std::string version, sha256, url;
        std::filesystem::path folder;
        std::wstring error;  // 空: 落として展開できた
        std::vector<PluginId> requirements;  // 一緒に入れる、このエンジンにある前提
    };
    std::optional<FinishedDownload> finishedDownload_;
    PluginCatalog catalog_;

    std::optional<Plugins::EngineRefreshPlan> engineRefresh_;  // 作ったが、まだ当てていない
    platform::Gfx gfx_;
    HubSettings settings_;
    Plugins plugins_;
    std::unique_ptr<ScreenWindow> screen_;
    Pending pending_ = Pending::None;
    std::filesystem::path pendingFile_;
    bool pendingSkipNotice_ = false;
    std::vector<PluginId> pendingInstall_;
    std::string pendingMessage_;
    bool running_ = true;
    int graphicsFailures_ = 0;  // Recreate がこの回数続けて失敗した
    bool graphicsTold_ = false; // ... ユーザーには知らせた
    Worker catalogWorker_;
    Worker downloadWorker_;
    Worker engineWorker_;       // 最後に: App がなくなった後は、これが post するものは捨てられる
};

}  // namespace rm::editor
