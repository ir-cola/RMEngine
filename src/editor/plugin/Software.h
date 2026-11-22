#pragma once
// プラグインが、エディターに「ソフト」(ゲーム、イラスト、音楽 ...) を足すのに使うもの。
//
//   プラグイン (エディター側)  --RegisterEditor(PluginRegistry&)-->  SoftwareType { id, title, create }
//   エディターがプロジェクトを開く --create(SoftwareHost&)----------->  Software (開いたプロジェクトごとに 1 つ)
//
// 設計 RME_PLATFORM 4: ソフトはウィンドウのように (タブで) 切り替わり、1 つのソフトのパネルが
// 別のソフトのものと混ざることはなく、ソフトごとに自分のレイアウト、選択、Undo の履歴を持つ
// (音楽での Ctrl+Z がメッシュを戻すことはない)。プロジェクトの「未保存」の印は 1 つだけ。
// 核のパネル (階層、インスペクター、コンソール、アセット) は、ソフトごとに 1 つずつある。
//
// プラグインは exe に組み込む (静的リンク、設計 RME_PLATFORM 10 (a))。その一覧は
// plugins/BuiltinPlugins.cpp にある - 核はプラグインのヘッダーを include しない。

#include "base/Json.h"
#include "core/PluginId.h"
#include "core/RuntimeRegistry.h"
#include "editor/plugin/PluginManifest.h"
#include "core/UndoHistory.h"
#include "core/reflect/FieldCommand.h"
#include "editor/dock/DockLayout.h"
#include "editor/dock/DockSpace.h"
#include "editor/tree/TreeView.h"
#include "platform/Gfx.h"
#include "platform/Shell.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace rm::editor {

struct Project;
class PluginRegistry;

// どのソフトにもある核のパネル。ソフトの初期レイアウトは、この ID で並べる。
namespace core_panel {
inline constexpr char kHierarchy[] = "core.hierarchy";
inline constexpr char kInspector[] = "core.inspector";
inline constexpr char kConsole[] = "core.console";
inline constexpr char kAssets[] = "core.assets";
inline constexpr char kHelp[] = "core.help";  // ヘルプ (最初は閉じている。ヘルプのメニュー / F1 で開く)
}  // namespace core_panel

// 核のインスペクターが見せるもの: 題名と、ブロック ("値のかたまり" - ゲームのエンティティなら、
// エンティティと、その部品それぞれ) の一覧。リフレクションされた型なら何でもよい: インスペクターが
// その記述から描き、編集はこのソフトの履歴に reflect::SetFieldCommand として入る。
// ブロックはインスペクターを描くたびに尋ねるので、Undo の後 (部品が戻った、または消えた) でも
// 今あるものが出る。対象のオブジェクトが消えたブロックは出さない。
struct InspectBlock {
    const reflect::TypeDesc* type = nullptr;  // null: `note` だけ出す
    std::function<reflect::EditTarget()> resolve;  // オブジェクトが今ある場所 (と、その未知の値)。オブジェクトが null = 消えた
    std::wstring title;           // 見出し (空 = 無し)
    std::wstring note;            // 見出しの下の 1 行
    std::vector<w::Action> menu;  // 見出しの ⋮ メニュー (例: 外す)
    // ブロックの見せ方 (例: Transform を ローカル / ワールド で): 欄の上の切り替え。
    // 選ぶと setMode を呼び、ソフトはその見せ方のブロックを返す。
    std::vector<std::wstring> modes;
    int mode = 0;
    std::function<void(int)> setMode;
    // 見せているオブジェクトが、編集するものと違うとき (それから計算した値 - ワールドで見せた
    // Transform) に設定する: ソフトは変更を自分のデータの編集に変えて、自分の履歴に積む。
    // 未設定: 変更は `resolve` のオブジェクトへの reflect::SetFieldCommand になる。`merge` が 0 以外:
    // 1 つの欄への入力 (inspector::Edit::merge) - Undo は 1 つにまとまる。
    struct Change {
        reflect::FieldPath path;
        json::Value before, after;
        std::string label;
        uint64_t merge = 0;
    };
    std::function<void(Change)> onEdit;
    // 複数を選んでいるとき: 他のもののうち同じブロック (同じ型) を、`resolve` のオブジェクトと一緒に
    // 編集する。違う値は "—" と出し、変更はすべてに入る (ベクトルは打った数だけ)。Undo は 1 つ。
    std::vector<std::function<reflect::EditTarget()>> also;
    // 見せるだけで編集しない: 欄は入力を受けない。
    bool readOnly = false;
    // 履歴の外で、その場で編集する (Play 中のゲーム: 遊んでいる写し - 保存するものも Undo するものもない。
    // ■ で元に戻る): 変更はすぐにオブジェクトに入り、未保存の印は付かない。
    bool direct = false;
    // 欄での右クリック (読み取り専用のブロックでも): その欄のメニュー (inspector::Block::fieldMenu)。
    std::function<std::vector<w::Action>(const reflect::FieldPath& path, std::string_view label)> fieldMenu;
    // 決まった値を一度に入れる: 欄の上の 1 行 (inspector::Block::presets)。
    std::wstring presetLabel;
    std::vector<w::Action> presets;
};

struct Inspectable {
    std::wstring title;  // 一番上。ブロックが無いときは、これだけ出す (例: "3 個選んでいます")
    std::function<std::vector<InspectBlock>()> blocks;
    std::wstring addLabel;  // ブロックの下のボタン (例: "部品を足す") ...
    std::function<std::vector<w::Action>()> addMenu;  // ... とそのメニュー

    // オブジェクト 1 つで、見出しは無し (ゲームの設定)。
    static Inspectable Single(const reflect::TypeDesc& type, std::function<reflect::EditTarget()> resolve, std::wstring title) {
        Inspectable i;
        i.title = std::move(title);
        i.blocks = [&type, resolve = std::move(resolve)] { return std::vector<InspectBlock>{{&type, resolve, {}, {}, {}}}; };
        return i;
    }
};

// プロジェクトを保存するときにソフトが書くファイル (Software::SaveData)。
struct ProjectFileWrite {
    std::string path;   // プロジェクトフォルダーからの相対で、区切りは "/" ("Assets/Scenes/Main.rmscene")。外には出ない
    std::string bytes;
};

namespace inspector {
struct Refs;
}  // namespace inspector

// ソフトが開く自分用のウィンドウ (ゲーム制作の デバッグ): エディターのドッキングの一部ではない -
// Windows の枠は無く、ソフトが決めた形 (角丸の四角の合併) で、エディターのウィンドウが持つ
// (その上に出て、タスクバーのボタンは無く、それと一緒に消える)。中で何かあったときと
// `refreshSeconds` ごとに (タイマーで) 描く - 自分でフレームを続けて描くことはない:
// エディターのウィンドウも描き続けているから。
enum class ToolHit { Client, Caption, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight, Nowhere };
struct ToolRoundRect {
    ui::Rect rect;  // ウィンドウの左上からの DIP
    float radius = 0;
};
struct ToolWindowSpec {
    std::wstring title;  // Alt+Tab に出る名前
    int widthDip = 560, heightDip = 640;
    int minWidthDip = 320, minHeightDip = 240;
    double refreshSeconds = 0;  // 開いている間、この間隔で描き直す (0: 何かあったときだけ)
    // 大きさ (DIP) に対するその形。空: 四角全体。
    std::function<std::vector<ToolRoundRect>(float w, float h)> shape;
    // ウィンドウの 1 点 (DIP) が何か: その UI のもの、動かすための取っ手、大きさを変える縁。
    std::function<ToolHit(float x, float y, float w, float h)> hit;
    std::function<void(ui::Context& c, float w, float h)> draw;
};
class ToolWindow {
public:
    virtual ~ToolWindow() = default;  // 閉じる
    virtual bool IsOpen() const = 0;  // 閉じたら false (その X、Alt+F4、Close)
    // このフレームの後で閉じる (自分の描画中でも安全: その X ボタン)。
    virtual void Close() = 0;
    virtual void Redraw() = 0;
    virtual void BringToFront() = 0;
};

// エディターが 1 つのソフトに渡すもの。
class SoftwareHost {
public:
    virtual ~SoftwareHost() = default;
    // このソフト自身の履歴。編集を積むとプロジェクト全体が未保存になる。
    virtual UndoHistory& History() = 0;
    virtual const Project& GetProject() const = 0;
    virtual void Redraw() = 0;
    // このソフトのインスペクターにオブジェクトを出す (空の Inspectable で消す)。
    virtual void Inspect(Inspectable target) = 0;
    // このソフトの階層の項目を選ぶ (たとえば作った後に)。rename = その名前の変更を始める。
    virtual void SelectInTree(std::vector<tree::ItemId> ids, bool rename = false) = 0;
    // 有効なプラグインが登録したもの (その貢献。例: ゲームの部品)。
    virtual const PluginRegistry& Registry() const = 0;
    // GPU (Direct3D 11 + Direct2D)。それで描くソフト (3D のシーンビュー) 用。
    virtual platform::Gfx& Graphics() = 0;
    // Windows のクリップボードをテキスト (UTF-8) として: エンティティをコピーすると JSON で入るので、
    // 別のプロジェクト (や別の RM Engine) に貼れる。使えないときは false / 空。
    virtual bool SetClipboard(const std::string& textUtf8) = 0;
    virtual std::string Clipboard() = 0;
    // AssetRef が今指しているファイル (プロジェクトの Assets/ の中で、移動や名前の変更があっても追う)。
    // そこに無ければ空。ID ではなくプロジェクト内のパスの値 (参照ができる前に作ったデータ:
    // "Assets/Models/robot.glb") は、そのパスとして扱うので、古いデータも動く。
    virtual std::filesystem::path AssetFile(const AssetRef& ref) const = 0;
    // 今エディターが持っているアセットの中身: まだ書いていない編集 (SetAssetContent) があればそれ、
    // 無ければそのファイル。そこに無ければ nullopt。
    virtual std::optional<std::string> AssetContent(const AssetRef& ref) {
        const std::filesystem::path file = AssetFile(ref);
        std::string err;
        return file.empty() ? std::nullopt : platform::ReadFileBytes(file, &err);
    }
    // アセットの中身の編集 (ソフト独自の種類のファイル: パーティクルの効果)。AssetContent で
    // 読む側はすぐにそれを見る。編集が終わったときにファイルへ書く (インスペクターの取り込みの設定と
    // 同じ)。その Undo は呼ぶ側のもの: 古い値に戻すコマンドを使う。
    virtual void SetAssetContent(const AssetRef& ref, std::string bytes) {
        const std::filesystem::path file = AssetFile(ref);
        std::string err;
        if (!file.empty()) platform::WriteFileAtomic(file, bytes, &err);
    }
    // プロジェクトの Assets/ の中のファイルのアセット ID (ソフトが今書いたファイルは、今その
    // .meta ができる)。ファイルが無い、または Assets/ の外なら空。
    virtual AssetRef AssetOf(const std::filesystem::path& file) = 0;
    // このエンジンが持つプラグイン (その plugin.json): 書き出しは、プロジェクトが使うプラグインの
    // 前提とコネクターを足す (editor/export ExportPlugins)。
    virtual std::vector<PluginManifest> PluginManifests() const = 0;
    // OS のフォルダーダイアログ。このフレームの後で開く (描画中には開かない)。`done` は選んだ
    // フォルダーを受け取る (キャンセルしたときは呼ばない)。
    virtual void PickFolder(std::wstring title, std::filesystem::path start, std::function<void(std::filesystem::path)> done) = 0;
    // プロジェクトに保存していない変更がある (ステータスバーの ● 未保存)。
    virtual bool Unsaved() const = 0;
    // プロジェクト全体を今保存する (Ctrl+S と同じ)。失敗したら false (エラーは画面に出る)。
    virtual bool SaveProject() = 0;
    // エディターの上にダイアログで質問する: `buttons` は右から (最初が主なボタン)。
    // `done` は押されたものを受け取り、閉じられたとき (Esc) は -1。同時に 2 つ以上は出さない:
    // 別の質問が開いている間にした質問は、すぐ -1 を返す。
    virtual void Ask(std::wstring title, std::wstring text, std::vector<std::wstring> buttons, std::function<void(int)> done) = 0;
    // このソフトの履歴を忘れる (その項目は、もう無いデータを指している: 別のシーンを開いた)。
    // プロジェクトは、保存済みか未保存かそのままにしておく。
    virtual void ClearHistory() = 0;
    // アセットのファイルの名前を変える (その .meta も付いてくる。ID は変わらない)。空、
    // または失敗した理由 (画面の言語で)。
    virtual std::wstring RenameAsset(const AssetRef& ref, const std::string& newNameUtf8) = 0;
    // ソフトが自分で (自分用のウィンドウで) 描くインスペクターのために、参照の欄が並べるもの
    // (プロジェクトのファイル、階層の項目): `out` に入れる。何も入れなければ "(なし)" だけ出る。
    virtual void FillInspectorRefs(inspector::Refs& out) { (void)out; }
    // ソフトが開く自分用のウィンドウ (ToolWindow)。すぐに出す。作れなければ null。
    virtual std::unique_ptr<ToolWindow> OpenToolWindow(ToolWindowSpec spec) {
        (void)spec;
        return nullptr;
    }
};

// 開いている 1 つのプロジェクトの中の、1 つのソフト。
class Software {
public:
    virtual ~Software() = default;
    // このソフトが足すパネル (ID はソフト内で一意で、"core." で始めない)。
    virtual std::vector<dock::PanelType> Panels() = 0;
    // 最初のレイアウト (「レイアウトを元に戻す」でも使う)。core_panel の ID を使ってよい。
    virtual dock::Layout DefaultLayout() const = 0;
    // 核の階層パネルが出すもの (ゲーム: エンティティ、3D モデル: メッシュとボーン ...)。
    // 核が木を描き、ソフトが項目を並べ、名前の変更・移動・メニューの行が何をするかを決める。
    // null: パネルはヒントを出す。
    virtual tree::Source* Hierarchy() { return nullptr; }

    // ソフトの分の、プロジェクトファイル (.rmpf の "softwareData")。LoadData はソフトを作った後に
    // 1 回呼ぶ - プロジェクトにまだ何も無いときは null。SaveData はプロジェクトの保存時に、
    // 読んだままのデータと一緒に呼ぶ (知らないキーも残る - 守るのはソフトの仕事)。
    // true を返すと `data` をファイルに残し、そのプラグインをプロジェクトが使うものとして記録する
    // (それが無いと開かない)。
    // .rmpf には大きすぎるデータ (ゲームのシーン) は、プロジェクトのファイルに入れる: SaveData が
    // `files` に入れ (変わったものだけ)、エディターが 1 つずつアトミックに書く - .rmpf より前に
    // 全部書くので、.rmpf が無いファイルを指すことはない。Saved() は全部書き終わったときに 1 回呼ぶ
    // (保存に失敗したときは呼ばない: 次の保存でまたファイルを渡す)。
    virtual void LoadData(const json::Value& data) { (void)data; }
    virtual bool SaveData(json::Value& data, std::vector<ProjectFileWrite>& files) {
        (void)data;
        (void)files;
        return false;
    }
    virtual void Saved() {}
    // ソフトのデータが使う、ほかのプラグイン (ゲーム: そのシーンと Prefab の部品を出すプラグイン)。
    // SaveData が true を返した後に尋ね、ソフト自身のものと一緒に .rmpf の "plugins" に記録する -
    // 書き出したゲームがそれをリンクする (設計 RME_PLATFORM 5-a、条件 1)。
    virtual std::vector<PluginId> UsedPlugins() const { return {}; }

    // プロジェクトから自分のプログラムを作るソフト (ゲーム: exe、editor/export)。
    // ExportNow は呼んだスレッドで、ソフト自身の設定を使って `outDir` に作る (書き出しの確認の
    // コマンドライン: RM_Engine.exe --export-template ...)。ソフト自身の画面では worker でする。
    virtual bool CanExport() const { return false; }
    virtual bool ExportNow(const std::filesystem::path& outDir, std::string* error) {
        (void)outDir;
        (void)error;
        return false;
    }

    // ユーザーがこのソフトをどう見ているか (シーンビューのカメラ、選んだギズモ ...): プロジェクトの
    // データではない - レイアウトの隣の UserSettings/Layout.json に残し、プロジェクトを未保存に
    // せず、Undo もされない。LoadView は LoadData の後に 1 回呼ぶ (null: まだ残したものは無い)。
    // SaveView はレイアウトを保存するたびに呼ぶ (null: 残すものは無い)。
    virtual void LoadView(const json::Value& view) { (void)view; }
    virtual json::Value SaveView() const { return {}; }

    // パネルを描く前に、このソフトが表示されている間、1 フレームに 1 回呼ぶ (Play 中のゲームは
    // ここで進み、次のフレームは c.RequestAnimation() で求める)。
    virtual void Tick(ui::Context& c) { (void)c; }
    // タブの行の、タブの右にあるソフト自身のコントロール (ゲームの ▶ Play ■)。`r` はそこに使える
    // 場所 (行の高さいっぱい)。
    virtual void DrawToolbar(ui::Context& c, const ui::Rect& r) {
        (void)c;
        (void)r;
    }
    // ソフトが、データを変えてはいけない何かを実行している間 true (Play 中のゲーム: 写しを
    // 遊んで、止めると元に戻る)。エディターはタブの行に色を付け、その間は Undo / Redo をしない。
    virtual bool Running() const { return false; }
    // ソフトがエディターの デバッグ メニューに足す行 (ゲームの ゲームデバッグ: そのデバッグウィンドウ)。
    // メニューを開いたときと、行を選んだときにもう一度尋ねる - 順番は同じにすること。
    struct DebugMenuItem {
        std::wstring label;
        bool checked = false;
        std::function<void()> run;
    };
    virtual std::vector<DebugMenuItem> DebugMenu() { return {}; }
};

struct SoftwareType {
    PluginId plugin;      // レジストリーが入れる: それを足したプラグイン
    std::string id;       // プラグイン内で一意。例: "game" (プラグインの名前の部分と同じ決まり)
    std::wstring title;   // タブの名前。例: L"ゲーム"
    std::function<std::unique_ptr<Software>(SoftwareHost&)> create;
};

// プラグインが、核を通してほかのプラグインに出すもの。核はどちら側も、その中身も知らない。
// その "口" を読むプラグインが名前を付け、その項目の型を決める。例: ゲーム制作は
// "rm_engine.game_production.component" の項目 (game_production::ComponentInfo) を読み、
// 3D のコネクターがそこに Transform を出す。
template <class T>
using Contributed = rm::Contributed<T>;

// 貢献は核の RuntimeRegistry (core/RuntimeRegistry.h): プラグインのランタイムの部分が先に同じ
// 一覧に登録し (書き出したゲームにも見えるもの)、次にエディターの部分が登録する。
class PluginRegistry : public RuntimeRegistry {
public:
    void AddSoftware(SoftwareType type);
    const std::vector<SoftwareType>& Softwares() const { return softwares_; }

private:
    std::vector<SoftwareType> softwares_;
};

struct BuiltinPlugin {
    PluginIdLiteral id;  // プラグインの kPluginId (読み込み時に plugin.json と照らす)
    void (*registerEditor)(PluginRegistry& registry);  // ランタイムの部分だけのプラグインは null
};

// この exe に組み込まれた全プラグイン。plugins/BuiltinPlugins.cpp で定義する。
std::span<const BuiltinPlugin> BuiltinPlugins();

}  // namespace rm::editor
