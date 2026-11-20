#pragma once
// 1 つのプロジェクトを開いたエディターのウィンドウ。
//
// レイアウト (設計ドキュメント "body -> software -> panels"):
//   メニューバー                  File / Edit / Window / Plugins / Help
//   ソフトのタブ                  プラグインが足す "software" (ゲーム、イラスト、音楽 ...) ごとに 1 つ
//   選んだソフトのパネル          ドックされている (editor/dock) - 自分のウィンドウに出すこともできる
//   ステータスバー
//
// ソフトはそれぞれ 1 つの Workspace: 自分のパネル、レイアウト、フローティングウィンドウ、Undo の履歴を持つ
// (あるソフトの Ctrl+Z が別のソフトの編集を戻すことはない)。プロジェクトの「未保存」の印は 1 つ:
// どれかのワークスペースの履歴に積まれた編集が、これを立てる。ソフトが 1 つも入っていないときは、
// ワークスペース 1 つがコアのパネルだけを出す。
//
// 閉じる (タイトルバーの X、"Back to Hub"、"Quit") は必ず RequestClose を通り、
// 未保存の変更を尋ねる - 道は 1 本で、2 本にはしない。
//
// パネルのレイアウトはユーザーの見え方であって、プロジェクトのデータではない: Undo できず、
// プロジェクトを未保存にせず、<project>/UserSettings/Layout.json に残す (ソフトごとに 1 項目と、
// 最後にどのソフトを表示していたか)。名前を付けて保存したレイアウトもそこに、ソフトごとに残す
// (Window メニュー: 保存 / 切り替え / 管理 - EditorLayouts.cpp)。

#include "base/Log.h"
#include "core/UndoHistory.h"
#include "editor/app/ScreenWindow.h"
#include "editor/assets/AssetLibrary.h"
#include "editor/assets/AssetPanel.h"
#include "editor/dock/DockSpace.h"
#include "editor/dock/NamedLayouts.h"
#include "editor/inspector/Inspector.h"
#include "editor/lang/Lang.h"
#include "editor/plugin/Software.h"
#include "editor/project/Project.h"
#include "editor/widgets/Widgets.h"
#include "platform/Shell.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::editor {

class FloatWindow;

class EditorScreen final : public ScreenWindow {
public:
    EditorScreen(App& app, Project project);
    ~EditorScreen() override;
    bool Open();
    // 書き出しチェックのコマンドライン (ウィンドウなし): 書き出せる最初のソフトが `outDir` に書き出す。
    bool ExportForCheck(const std::filesystem::path& outDir, std::string* error);
    // プロジェクトのロック (ProjectLockFile)。このエディターが開いている間持つ。
    void AdoptLock(std::unique_ptr<platform::FileLock> lock) { lock_ = std::move(lock); }
    void Close() override;  // 先にフローティングのパネルウィンドウを閉じる
    void ShowError(std::wstring message);
    void OnLookChanged() override;  // パネルウィンドウも

    // --- フローティングのパネルウィンドウから呼ばれる ---
    void DrawFloating(ui::Context& c, float width, float height, FloatWindow& window);
    // パネルから引きずり出したもの (アセット、階層の項目) が、このエディターの別のウィンドウの上に来た:
    // そのウィンドウに場所を伝え、そこの参照フィールドが受け取れるようにする (w::SetDragElsewhere)。
    void ForwardDrag(ui::Context& c, HWND self, float width, float height);
    // インスペクターで変えた取り込みの設定は、編集が終わってから .meta に書く
    // (AssetLibrary::FlushImports) - どのウィンドウもフレームの終わりに呼ぶ。
    void FlushAssetEdits(const ui::Context& c);
    void CloseFloatingWindow(uint32_t floatingId);
    void FloatingMoving(uint32_t floatingId, HWND hwnd);
    void FloatingMoveEnded(uint32_t floatingId);

protected:
    void OnFrame(ui::Context& c, float width, float height) override;
    void OnCloseRequested() override;
    bool OnQueryEndSession() override;  // 未保存の変更: 尋ねる (閉じるときと同じダイアログ)
    void OnEndSession() override;
    void OnTimer(UINT_PTR id) override;  // 自動保存
    std::optional<bool> SaveWork() override { return Save(); }  // GPU が完全に失われた

private:
    static constexpr UINT_PTR kAutoSaveTimer = 100;
    enum class CloseAction { None, ToHub, Quit };
    enum class Modal { None, UnsavedChanges, Error, About, InstallPlugin, SaveLayoutAs, Layouts, Ask };
    enum class Menu { None = -1, File, Edit, Window, Plugins, Help, Debug, Count };

    void RequestClose(CloseAction action);
    void FinishClose(CloseAction action);
    void EndShutdownBlock();  // 尋ねている間に Windows へ伝える理由 (ShutdownBlockReasonCreate)
    bool Save();
    void SetDirty(bool dirty);
    void UpdateTitle();
    // インスペクターの参照フィールドが出すもの (アセット、表示中のソフトの 階層 の項目)。
    inspector::Refs MakeRefs();
    std::vector<w::MenuItem> MenuItems(Menu m) const;
    void RunMenu(Menu m, int index);
    void HandleShortcuts(ui::Context& c);
    void UndoRedo(bool redo);  // 表示中のソフトの履歴 (動いている間は使わない)
    // プラグインメニュー: エディターからインストールすると、保存し、インストールして、同じプロジェクトをもう一度開く
    // (設計 RME_PLATFORM 5-a、2026-09-29 に決定)。取り除くのは Hub でだけできる。
    void BeginInstallPlugin(const PluginId& id);
    void RedrawAll();

    // このプロジェクトの中の 1 つのソフト (ソフトが入っていないときは、コアのパネルだけ)。
    class Workspace final : public SoftwareHost {
    public:
        explicit Workspace(EditorScreen& editor) : editor_(editor) {}
        UndoHistory& History() override { return history; }
        const Project& GetProject() const override { return editor_.project_; }
        void Redraw() override { editor_.RedrawAll(); }
        void Inspect(Inspectable target) override {
            // 何度も (毎フレーム) 尋ねるソフトは、そのたびにインスペクターのスクロールとメニューを
            // 捨ててしまう: 1 度だけ知らせる (そのソフト側の直すべきバグ)。
            const double now = platform::NowSeconds();
            if (now - inspectSince_ > 1.0) {
                inspectSince_ = now;
                inspectCount_ = 0;
            }
            if (++inspectCount_ == 20 && !inspectTold_) {
                inspectTold_ = true;
                log::Warn("インスペクターが 1 秒に 20 回以上作り直されています (スクロールとメニューが戻ります。直すべき不具合です)");
            }
            inspected = std::move(target);
            // 新しいものを表示した: スクロール、フィールドの入力、メニューは最初からになる。部品を足す
            // パネルの開いているフォルダーと動かした場所は残る (次のエンティティで探し直す)。
            auto picker = std::move(inspectorState.addPicker);
            inspectorState = {};
            inspectorState.addPicker = std::move(picker);
            editor_.RedrawAll();
        }
        const PluginRegistry& Registry() const override;
        platform::Gfx& Graphics() override;
        bool SetClipboard(const std::string& textUtf8) override;
        std::string Clipboard() override;
        std::filesystem::path AssetFile(const AssetRef& ref) const override;
        AssetRef AssetOf(const std::filesystem::path& file) override;
        std::optional<std::string> AssetContent(const AssetRef& ref) override;
        void SetAssetContent(const AssetRef& ref, std::string bytes) override;
        std::vector<PluginManifest> PluginManifests() const override;
        void PickFolder(std::wstring title, std::filesystem::path start, std::function<void(std::filesystem::path)> done) override;
        bool Unsaved() const override { return editor_.dirty_; }
        bool SaveProject() override { return editor_.Save(); }
        void Ask(std::wstring title, std::wstring text, std::vector<std::wstring> buttons, std::function<void(int)> done) override;
        void ClearHistory() override {
            const bool was = editor_.dirty_;
            history.Clear();
            editor_.SetDirty(was);
        }
        std::wstring RenameAsset(const AssetRef& ref, const std::string& newNameUtf8) override;
        std::unique_ptr<ToolWindow> OpenToolWindow(ToolWindowSpec spec) override;
        void FillInspectorRefs(inspector::Refs& out) override { out = editor_.MakeRefs(); }
        void SelectInTree(std::vector<tree::ItemId> ids, bool rename) override {
            tree::RequestSelect(treeState, std::move(ids), rename);
            editor_.RedrawAll();
        }
        dock::Layout DefaultLayout() const;
        std::wstring Title() const { return Tr(type ? type->title : TrKey(L"コア")); }  // 使っている言語で

        const SoftwareType* type = nullptr;  // nullptr: ソフトなし (コアのパネル + "start")
        UndoHistory history;                 // `software` より前に宣言する。こちらのほうが長く生きるように
        dock::DockSpace dock;
        std::unique_ptr<Software> software;
        Inspectable inspected;  // インスペクターが見せているもの
        inspector::State inspectorState;
        double inspectSince_ = 0;  // この 1 秒間に頼まれた Inspect: 回数 (多すぎるときは 1 度だけ知らせる)
        int inspectCount_ = 0;
        bool inspectTold_ = false;
        tree::State treeState;  // the 階層: selection, open items, search
        dock::NamedLayouts named;  // ユーザーが名前を付けて保存したレイアウト (UserSettings)
        dock::NamedLayouts shared;  // どのプロジェクトでも使える名前付きレイアウト (%APPDATA%\RM_Engine\Layouts.json)
        std::string namedShown;    // 最後に切り替えた / 保存した名前付きレイアウト (メニューでチェックが付く)
        bool namedShownShared = false;  // ... それがどちらの一覧にあるか
        dock::NamedLayouts& Named(bool sharedList) { return sharedList ? shared : named; }
        const dock::NamedLayouts& Named(bool sharedList) const { return sharedList ? shared : named; }
        std::unique_ptr<AssetPanel> assetPanel;  // アセット パネル (最初に描くときに作る)
        float assetThumbnail = 40.f;             // そのサムネイルの大きさ (UserSettings: "assets": {"thumbnail"})

    private:
        EditorScreen& editor_;
    };

    // ワークスペース、パネル、そのレイアウト
    void BuildWorkspaces();
    // .rmpf の "softwareData": 各ソフト自身のプロジェクトの部分 (1 度読み、保存時に書く)。
    void LoadSoftwareData();
    void StoreSoftwareData(std::vector<ProjectFileWrite>& files);
    std::vector<dock::PanelType> CorePanels(bool withStart);
    Workspace& Active() { return *workspaces_[active_]; }
    const Workspace& Active() const { return *workspaces_[active_]; }
    void SwitchSoftware(size_t index);
    std::filesystem::path LayoutFile() const;
    void LoadLayout();
    void SaveLayout();
    void ResetLayout();
    void CaptureFloatingRects();
    void LoadSharedLayouts();
    void SaveSharedLayouts();  // ユーザーがパネルウィンドウを置いた場所を、レイアウトに
    // 名前付きレイアウト (EditorLayouts.cpp)
    void BeginSaveLayoutAs();
    bool SaveNamedLayout(const std::string& name, bool shared);
    void ApplyNamedLayout(const std::string& name, bool shared);
    void SaveNamedList(bool shared);  // 一覧が入っているファイルを書く
    // 名前付きの一覧を変える前に: 共有のほうはファイルから読み直す (同時に開いている別の RM Engine が
    // 変えているかもしれない - それがしたことは残す)。false (知らせ済み): その一覧が入っているファイルを
    // このバージョンは書かない (新しいバージョンのもの、または読めなかったもの) - 変更はしない。
    // しないと、次の起動で消えてしまう。
    bool BeginNamedEdit(bool shared);
    void DrawSaveLayoutAs(ui::Context& c);
    void DrawLayouts(ui::Context& c);
    void RequestSyncFloating();
    void SyncFloating(bool activate);
    void CloseFloats();

    void DrawMenuBar(ui::Context& c, ui::Rect r);
    void DrawSoftTabs(ui::Context& c, ui::Rect r);
    void DrawStatusBar(ui::Context& c, ui::Rect r);
    void DrawOpenMenu(ui::Context& c);
    void DrawModal(ui::Context& c);

    void DrawHierarchy(ui::Context& c, ui::Rect r);
    void DrawInspector(ui::Context& c, ui::Rect r);
    void DrawStart(ui::Context& c, ui::Rect r);
    void DrawConsole(ui::Context& c, ui::Rect r);
    void DrawAssets(ui::Context& c, ui::Rect r);

    Project project_;
    std::unique_ptr<platform::FileLock> lock_;  // Close() で (と壊れるときに) 手放す
    bool dirty_ = false;
    double dirtySince_ = 0;  // 最初の未保存の変更の platform::NowSeconds()

    Modal modal_ = Modal::None;
    CloseAction pendingClose_ = CloseAction::None;
    bool shutdownBlocked_ = false;
    std::wstring modalText_;
    // Modal::Ask (SoftwareHost::Ask): そのタイトル、ボタン、答えを受け取るもの。
    std::wstring askTitle_;
    std::vector<std::wstring> askButtons_;
    std::function<void(int)> askDone_;
    std::wstring installName_;
    std::vector<PluginId> installPlan_;
    // 名前付きレイアウトのダイアログ
    std::string layoutName_;      // "名前を付けて保存" に入力した名前
    bool layoutShared_ = false;   // "名前を付けて保存" を共有の一覧へ
    bool focusLayoutName_ = false;
    std::string renamingLayout_;  // 一覧で名前を変えている行 ("" = なし。行のキー: 一覧 + 名前)
    std::string renameText_;
    bool focusRename_ = false;
    std::string confirmDelete_;   // 削除 を 1 度押した行

    w::Popup menuPopup_;
    Menu openMenu_ = Menu::None;
    ui::Rect menuTitleRects_[static_cast<int>(Menu::Count)] = {};

    // ワークスペースより「前」に宣言する。そうすれば、ワークスペースより「後」に壊れる (メンバーは
    // 逆の順に壊れる): すべてのワークスペースの AssetPanel、Undo 履歴の ImportCommands、
    // Workspace::AssetFile は、ワークスペースを壊している間これらを指しているため。
    UiWake assetsWake_{[this] { RedrawAll(); }};
    std::unique_ptr<AssetLibrary> assets_;    // プロジェクトの Assets/ (ID、ファイル操作) - どのソフトのものでも
    std::unique_ptr<Thumbnails> thumbnails_;  // アセットの小さな絵 - どのソフトのパネルも共有する (そのスレッドはここで終わる)

    std::vector<std::unique_ptr<Workspace>> workspaces_;  // 空にはならない
    size_t active_ = 0;
    std::vector<std::unique_ptr<FloatWindow>> floats_;  // アクティブなワークスペースのものだけ
    bool syncQueued_ = false;
    json::Value layoutDoc_;        // Layout.json 全体 (ほかのソフトと知らないキーは残す)
    bool layoutReadOnly_ = false;  // 新しいエンジンが書いた: 使うが、上書きはしない
    json::Value sharedDoc_;        // 共有の Layouts.json 全体 (ほかのソフトと知らないキーは残す)
    bool sharedReadOnly_ = false;

    HWND dragTarget_ = nullptr;  // 最後にドラッグの上に来たと知らせたウィンドウ (ForwardDrag)

    int logSink_ = 0;
    // 各ウィンドウのコンソールが最後に追ったログの項目 (パネルはウィンドウの間を動ける)。
    std::unordered_map<const ui::Context*, uint64_t> consoleSeenSeq_;
};

}  // namespace rm::editor
