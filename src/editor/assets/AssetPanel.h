#pragma once
// The アセット panel (core_panel::kAssets): the project's Assets/ folder as a tree - folders and
// files with a picture of their kind. Drawn with the core tree (editor/tree), so it selects, searches,
// renames (F2 / double-click) and drags like the 階層:
//
//   drag rows onto a folder           move the files there (their ids go along)
//   drag rows out of the panel        onto a reference field (AssetRef) of the インスペクター
//   right-click                       新しいフォルダ / エクスプローラーで表示 / 削除 (ごみ箱へ)
//                                     (on empty space: 新しいフォルダ / エクスプローラーで開く)
//   under the tree                    what the selected one is and where, and the slider of the
//                                     thumbnails' size (left end: no thumbnails, the kinds' pictures)
//
// Pictures and 3D models show a thumbnail (Thumbnails.h) instead of their kind's picture.
// One per software (its own selection and open folders); the files are the project's (AssetLibrary).

#include "editor/assets/AssetLibrary.h"
#include "editor/assets/Thumbnails.h"
#include "editor/inspector/Inspector.h"
#include "editor/tree/TreeView.h"
#include "editor/widgets/Widgets.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rm::editor {

class AssetPanel final : public tree::Source {
public:
    static constexpr float kMinThumbnail = 16.f;  // and below kThumbnailsFrom: no thumbnails
    static constexpr float kThumbnailsFrom = 20.f;
    static constexpr float kMaxThumbnail = 96.f;

    // showError: a problem with a file operation (a dialog). thumbnails: null = none. thumbnailSize:
    // the thumbnails' size in DIPs (the software's view: kept in UserSettings by the editor).
    AssetPanel(AssetLibrary& library, Thumbnails* thumbnails, float& thumbnailSize, std::function<void(std::wstring)> showError);

    void Draw(ui::Context& c, const ui::Rect& r);

    // tree::Source
    void Items(std::vector<tree::Item>& out) override;
    std::wstring EmptyText() const override;
    bool CanRename(tree::ItemId id) const override { return guids_.contains(id); }
    void Rename(tree::ItemId id, const std::string& nameUtf8) override;
    bool CanMove() const override { return true; }
    void Move(std::span<const tree::ItemId> items, tree::ItemId parent, size_t index) override;
    std::vector<tree::Action> Actions(std::span<const tree::ItemId> selection) override;
    w::DragThing DragOut(std::span<const tree::ItemId> items) override;
    float RowHeight() const override;
    bool DrawPicture(ui::Context& c, const tree::Item& item, const ui::Rect& r) override;

private:
    std::vector<std::string> GuidsOf(std::span<const tree::ItemId> ids) const;
    void Report(const std::wstring& problem);

    AssetLibrary& lib_;
    Thumbnails* thumbnails_;
    float& thumbnailSize_;
    std::function<void(std::wstring)> showError_;
    tree::State state_;
    std::unordered_map<tree::ItemId, std::string> guids_;  // tree id -> asset id (from the last Items)
};

// The picture and the name of a kind ("画像" ...), in the language in use.
w::Icon IconOf(asset::Kind kind);
tree::Icon TreeIconOf(asset::Kind kind);
std::wstring KindLabel(asset::Kind kind);

// What a reference field (AssetRef) of the インスペクター shows and lists.
std::optional<inspector::RefChoice> AssetChoice(const AssetLibrary& library, std::string_view guid);
std::vector<inspector::RefChoice> AssetChoices(const AssetLibrary& library);

}  // namespace rm::editor
