#pragma once
// アイコンの絵を 1 枚にまとめた画像 (スプライト) の目次と、どの印にどの絵を使うかの表。純粋 (tests/TestIcons.cpp)。
//   絵    resources/icons/source/*.png (TestEngine で使っていた絵。名前 = 絵の名前)
//   1 枚  resources/icons/icons.png と icons.json - tools/make_icon_atlas.ps1 が作る。exe に埋め込む (IDR_ICONS / IDR_ICONS_INDEX)
// 描くのは w::DrawIcon (Widgets.cpp)。表に無い印 (× ✓ 矢印など) は、今までどおり線で描く。

#include "base/Json.h"
#include "editor/widgets/Widgets.h"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rm::editor::w {

struct IconCell {
    float x = 0, y = 0;  // マスの左上 (ピクセル)
    bool color = false;  // 色の付いた絵 (警告・エラー・ボーンの X Y Z): 色を残して描く。ほかは白 1 色で、テーマの色で塗る
};

struct IconAtlas {
    float cell = 0;  // 1 マスの大きさ (ピクセル)
    std::map<std::string, IconCell, std::less<>> cells;
    const IconCell* Find(std::string_view name) const;
};

// icons.json を読む。形が違えば nullopt と理由。
std::optional<IconAtlas> ParseIconAtlas(const json::Value& v, std::string* error);

// 印が使う絵の名前 (表に無い印は空)。
std::string_view IconPicture(Icon icon);
// 絵の名前から印 (RM_PROPERTY の Icon="..." - インスペクターの項目名の前)。無ければ nullopt。
std::optional<Icon> IconNamed(std::string_view picture);
// 表に並んだすべての絵の名前 (テスト: 1 枚の画像にそろっているか)。
std::span<const std::string_view> IconPictures();

}  // namespace rm::editor::w
