#pragma once
// ゲーム制作 × 2D - an entity drawn as a picture (a sprite), placed by its Transform 2D.
// Without an image (or one that cannot be read) it is a square of its colour, 1 unit across.
//
// `image` is an asset reference (the id in the file's .meta): moving or renaming the file does not lose it.
// The size in the plane comes from the image: its pixels ÷ 1 単位のピクセル数 (100: an image of 100 x 50
// pixels is 1 x 0.5 units), its middle at the entity's place.

#include "core/reflect/Reflect.h"

namespace rm::game_x_2d {

RM_STRUCT(Label="スプライト (2D の絵)")
struct Sprite {
    RM_PROPERTY(Edit, Label="画像", Tooltip="画像のアセット (.png / .jpg / .bmp / .gif …)。アセットのパネルからドラッグするか、クリックして選びます。無いときは色の四角です", AssetKind="image") AssetRef image;
    RM_PROPERTY(Edit, Label="色", Tooltip="画像の色に掛けます (白でそのまま)。透明度も効きます", Min=0, Max=1) Color color{1, 1, 1, 1};
    RM_PROPERTY(Edit, Label="左右反転") bool flipX = false;
    RM_PROPERTY(Edit, Label="上下反転") bool flipY = false;
    RM_PROPERTY(Edit, Label="並び順", Tooltip="Transform 2D の並び順が同じ物の中で、大きいほど手前に描きます", Min=-100000, Max=100000) int order = 0;
    RM_PROPERTY(Edit, Label="1 単位のピクセル数", Tooltip="画像の何ピクセルを 1 単位の長さにするか", Min=0.01, Max=100000) float pixelsPerUnit = 100.f;
    RM_PROPERTY(Edit, Label="くっきり (ドット絵)", Tooltip="拡大してもぼかさず、ピクセルをそのまま大きくします") bool crisp = false;
};

}  // namespace rm::game_x_2d
