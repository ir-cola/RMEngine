#pragma once
// アセットパネル (core_panel::kAssets): プロジェクトの Assets/ フォルダーをツリーで見せる -
// フォルダーとファイルを、その種類の絵と一緒に。核のツリー (editor/tree) で描くので、階層と
// 同じように選べて、検索でき、改名 (F2 / ダブルクリック) でき、ドラッグできる:
//
//   行をフォルダーへドラッグ          ファイルをそこへ移す (id はついて行く)
//   行をパネルの外へドラッグ          インスペクターの参照欄 (AssetRef) へ
//   右クリック                        新しいフォルダ / エクスプローラーで表示 / 削除 (ごみ箱へ)
//                                     (空いた所で: 新しいフォルダ / エクスプローラーで開く)
//   ツリーの下                        選んだ物が何で、どこにあるか。サムネイルの大きさの
//                                     スライダー (左の端: サムネイルなし、種類の絵)
//
// 画像と 3D モデルは、その種類の絵の代わりにサムネイル (Thumbnails.h) を出す。
// ソフトごとに 1 つ (それぞれの選択と開いたフォルダー)。ファイルはプロジェクトのもの (AssetLibrary)。

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
    static constexpr float kMinThumbnail = 16.f;  // これ以下は kThumbnailsFrom: サムネイルなし
    static constexpr float kThumbnailsFrom = 20.f;
    static constexpr float kMaxThumbnail = 96.f;

    // showError: ファイル操作の問題 (ダイアログ)。thumbnails: null = なし。thumbnailSize:
    // サムネイルの大きさ (DIP。ソフトの見え方: エディターが UserSettings に保つ)。
    AssetPanel(AssetLibrary& library, Thumbnails* thumbnails, float& thumbnailSize, std::function<void(std::wstring)> showError);

    void Draw(ui::Context& c, const ui::Rect& r);
    // 右クリックメニューが開いている (メニューの下にある物はクリックを受けない: EditorScreen::OnFrame)。
    bool MenuOpen() const { return state_.menu.open; }

    // アセットを 1 つ選ぶと、そのソフトのインスペクター (`inspect`) に出す: それが何かと、
    // 持っている種類なら取り込みの設定 (core/asset/ImportSettings.h)。変更はすぐその .meta に
    // 書かれ、ソフトの履歴 (`push`) に Undo の 1 件として積まれる - Ctrl+Z で古い値を書き戻す。
    // ドラッグせずにボタンを離したときに出す (アセットをインスペクターの欄へドラッグしても、
    // インスペクターが出している物は奪われない)。
    void SetInspector(std::function<void(Inspectable)> inspect, std::function<void(std::unique_ptr<ICommand>)> push);
    // プラグインのプレビューがある所 (AssetPreview.h: 音の長さ、「鳴らしてみる」)と、別のスレッドで
    // 読む行が出来たときに (UI スレッドで) 全ウィンドウを描き直させる方法。
    void SetPreviews(const PluginRegistry* registry, std::function<void()> redraw);

    // tree::Source
    void Items(std::vector<tree::Item>& out) override;
    std::wstring EmptyText() const override;
    bool CanRename(tree::ItemId id) const override { return guids_.contains(id); }
    void Rename(tree::ItemId id, const std::string& nameUtf8) override;
    bool CanMove() const override { return true; }
    void Move(std::span<const tree::ItemId> items, tree::ItemId parent, size_t index) override;
    std::vector<tree::Action> Actions(std::span<const tree::ItemId> selection) override;
    w::DragThing DragOut(std::span<const tree::ItemId> items) override;
    void SelectionChanged(std::span<const tree::ItemId> ids) override;
    float RowHeight() const override;
    bool DrawPicture(ui::Context& c, const tree::Item& item, const ui::Rect& r) override;

private:
    std::vector<std::string> GuidsOf(std::span<const tree::ItemId> ids) const;
    void Report(const std::wstring& problem);
    void InspectSelected();

    // インスペクターに出す取り込みの設定 (アセットを 1 つずつ)。
    struct ImportView {
        std::string guid;
        reflect::Object object;
        reflect::Unknowns unknowns;
    };
    std::shared_ptr<ImportView> view_ = std::make_shared<ImportView>();
    std::function<void(Inspectable)> inspect_;
    std::function<void(std::unique_ptr<ICommand>)> push_;
    bool pendingInspect_ = false;
    const PluginRegistry* registry_ = nullptr;
    std::unique_ptr<UiWake> described_;  // プレビューの行がわかった: 描き直す (パネルと一緒になくなる)
    friend class ImportCommand;

    AssetLibrary& lib_;
    Thumbnails* thumbnails_;
    float& thumbnailSize_;
    std::function<void(std::wstring)> showError_;
    tree::State state_;
    std::unordered_map<tree::ItemId, std::string> guids_;  // ツリーの id -> アセットの id (最後の Items から)
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
