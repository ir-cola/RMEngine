#pragma once
// アセットパネル (core_panel::kAssets): プロジェクトの Assets/ フォルダーを、エクスプローラーのように
// 見せる (2026-11-25 から。前はファイルも全部ツリーだった):
//
//   上           パンくずリスト (Assets > Models > ...: 押すとそのフォルダーへ) と、検索欄
//   左           フォルダーだけの木 (核のツリー editor/tree)。選んだフォルダーの中身が右に出る
//   右           今のフォルダーの中身をタイルで並べる (フォルダー・ファイル。画像と 3D モデルは
//                サムネイル)。一番上のフォルダーでなければ、先頭に「戻る」
//   下           選んだ物が何で、どこにあるか。絵の大きさのスライダー
//
//   タイルをクリック / Ctrl / Shift      選ぶ (1 つならインスペクターに出す)
//   フォルダーをダブルクリック / Enter    その中へ。「戻る」/ Backspace で親へ
//   タイルをフォルダーへドラッグ           そこへ移す (右のフォルダー・「戻る」・パンくず・左の木の行)
//   タイルをパネルの外へドラッグ           インスペクターの参照欄 (AssetRef) へ
//   F2 / 右クリック → 名前を変更          その場で名前を変える
//   右クリック                           新しいフォルダ / エクスプローラーで表示 / 削除 (ごみ箱へ)
//   検索欄に打つ                         Assets 全体から、名前に含むものを並べる (Esc で消す)
//
// ソフトごとに 1 つ (それぞれの選択と開いたフォルダー)。ファイルはプロジェクトのもの (AssetLibrary)。
// 並びの計算は AssetGrid.h (純粋)。

#include "core/reflect/Object.h"
#include "editor/assets/AssetLibrary.h"
#include "editor/app/Worker.h"
#include "editor/assets/AssetPreview.h"
#include "editor/assets/Thumbnails.h"
#include "editor/inspector/Inspector.h"
#include "editor/plugin/Software.h"
#include "editor/tree/TreeView.h"
#include "editor/widgets/Widgets.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::editor {

class AssetPanel final : public tree::Source {
public:
    static constexpr float kMinThumbnail = 32.f;  // これ以下は kThumbnailsFrom: サムネイルなし (種類の絵)
    static constexpr float kThumbnailsFrom = 36.f;
    static constexpr float kMaxThumbnail = 128.f;

    // showError: ファイル操作の問題 (ダイアログ)。thumbnails: null = なし。thumbnailSize:
    // タイルの絵の大きさ (DIP。ソフトの見え方: エディターが UserSettings に保つ)。
    AssetPanel(AssetLibrary& library, Thumbnails* thumbnails, float& thumbnailSize, std::function<void(std::wstring)> showError);

    void Draw(ui::Context& c, const ui::Rect& r);
    // 右クリックメニューが開いている (メニューの下にある物はクリックを受けない: EditorScreen::OnFrame)。
    bool MenuOpen() const { return state_.menu.open || grid_.menu.open; }

    // アセットを 1 つ選ぶと、そのソフトのインスペクター (`inspect`) に出す: それが何かと、
    // 持っている種類なら取り込みの設定 (core/asset/ImportSettings.h)。変更はすぐその .meta に
    // 書かれ、ソフトの履歴 (`push`) に Undo の 1 件として積まれる - Ctrl+Z で古い値を書き戻す。
    // ドラッグせずにボタンを離したときに出す (アセットをインスペクターの欄へドラッグしても、
    // インスペクターが出している物は奪われない)。
    void SetInspector(std::function<void(Inspectable)> inspect, std::function<void(std::unique_ptr<ICommand>)> push);
    // プラグインのプレビューがある所 (AssetPreview.h: 音の長さ、「鳴らしてみる」)と、別のスレッドで
    // 読む行が出来たときに (UI スレッドで) 全ウィンドウを描き直させる方法。
    void SetPreviews(const PluginRegistry* registry, std::function<void()> redraw);

    // tree::Source (左のフォルダーの木: 一番上に "Assets"、その下にフォルダーだけ)
    void Items(std::vector<tree::Item>& out) override;
    std::wstring EmptyText() const override;
    bool CanRename(tree::ItemId id) const override { return guids_.contains(id); }
    void Rename(tree::ItemId id, const std::string& nameUtf8) override;
    bool CanMove() const override { return true; }
    void Move(std::span<const tree::ItemId> items, tree::ItemId parent, size_t index) override;
    std::vector<tree::Action> Actions(std::span<const tree::ItemId> selection) override;
    w::DragThing DragOut(std::span<const tree::ItemId> items) override;
    void SelectionChanged(std::span<const tree::ItemId> ids) override;
    bool Searchable() const override { return false; }
    bool CanDrop(const w::DragThing& thing, tree::ItemId onto) const override;
    void Drop(const w::DragThing& thing, tree::ItemId onto) override;

private:
    static constexpr tree::ItemId kRootItem = 1;  // 木の一番上の "Assets" (アセットの id の数ではない)

    std::vector<std::string> GuidsOf(std::span<const tree::ItemId> ids) const;
    void Report(const std::wstring& problem);
    void Inspect(const std::string& guid);
    // 右クリックメニュー (木と並びで同じ)。`where`: 新しいフォルダを作る所 (フォルダーの id、"" = 一番上)。
    // `made`: 作ったフォルダーの id を受け取る (選んで名前を打たせる)。
    std::vector<w::Action> ActionsFor(const std::vector<std::string>& guids, const std::string& where,
                                      std::function<void(const std::string&)> made);

    // 今のフォルダー ("" = Assets の一番上)。消えていたら一番上。
    std::string FolderPath() const;
    // そのフォルダーを開く (右に中身。左の木もそれを選ぶ)。
    void OpenFolder(const std::string& guid);
    // `guids` をフォルダー `folderGuid` ("" = 一番上) へ移す。自分の中や、そのまま同じ所へは移さない。
    void MoveInto(const std::vector<std::string>& guids, const std::string& folderGuid);
    bool CanMoveInto(const std::vector<std::string>& guids, const std::string& folderGuid) const;

    void DrawTop(ui::Context& c, const ui::Rect& r);
    void DrawGrid(ui::Context& c, const ui::Rect& r);

    // インスペクターに出す取り込みの設定 (アセットを 1 つずつ)。
    struct ImportView {
        std::string guid;
        reflect::Object object;
        reflect::Unknowns unknowns;
    };
    std::shared_ptr<ImportView> view_ = std::make_shared<ImportView>();
    std::function<void(Inspectable)> inspect_;
    std::function<void(std::unique_ptr<ICommand>)> push_;
    const PluginRegistry* registry_ = nullptr;
    std::unique_ptr<UiWake> described_;  // プレビューの行がわかった: 描き直す (パネルと一緒になくなる)
    friend class ImportCommand;

    AssetLibrary& lib_;
    Thumbnails* thumbnails_;
    float& thumbnailSize_;
    std::function<void(std::wstring)> showError_;
    tree::State state_;  // 左の木
    std::unordered_map<tree::ItemId, std::string> guids_;  // 木の id -> フォルダーの id (最後の Items から)

    std::string folder_;  // 今のフォルダーの id ("" = Assets の一番上)
    float treeW_ = 190;   // 左の木の幅 (区切りをドラッグして変える)
    // 右の並び。
    struct Grid {
        std::vector<std::string> sel;  // 選んだアセットの id
        std::string anchor;            // Shift+クリックの起点
        std::string search;
        std::string renaming;          // 名前を変えているもの
        bool renameStart = false;
        std::string renameText;
        std::string press;             // 左ボタンで押したタイル (まだ離していない)
        float pressX = 0, pressY = 0;
        bool pressDeferred = false;    // 選択中のタイルを押した: 離したときにそれだけを選ぶ
        bool dragging = false;
        std::string inspect;           // 離したらインスペクターに出すもの
        bool keys = false;             // 最後のクリックが並びだった (キーが効く)
        w::Popup menu;
        std::vector<w::Action> menuActions;
    } grid_;
};

// 種類の絵と名前 ("画像" など)。使っている言語で。
w::Icon IconOf(asset::Kind kind);
tree::Icon TreeIconOf(asset::Kind kind);
std::wstring KindLabel(asset::Kind kind);

// インスペクターの参照欄 (AssetRef) が出して並べる物。
std::optional<inspector::RefChoice> AssetChoice(const AssetLibrary& library, std::string_view guid);
// アセットのサムネイル。`size` DIP 四方 (ウィンドウのピクセルで)。null: なし、またはまだ作っていない。
ID2D1Bitmap* AssetThumbnail(ui::Context& c, const AssetLibrary& library, Thumbnails& thumbnails, std::string_view guid, float size);
std::vector<inspector::RefChoice> AssetChoices(const AssetLibrary& library);

}  // namespace rm::editor
