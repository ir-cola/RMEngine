#pragma once
// ゲーム制作 × 2D - 絵 (スプライト) として描くエンティティ。場所は Transform 2D が決める。
// 画像がない (または読めない) ときは、その色の 1 単位四方の四角。
//
// `image` はアセットの参照 (ファイルの .meta の id): ファイルを動かしたり名前を変えたりしても
// 外れない。平面での大きさは画像から決まる: ピクセル数 ÷ 1 単位のピクセル数 (100: 100 x 50
// ピクセルの画像は 1 x 0.5 単位)、真ん中がエンティティの場所。
//
// アトラス (スプライトシート): 画像を同じ大きさの 横のコマ数 x 縦のコマ数 のセルに切ったもの。
// コマ が描くセル (0 = 左上、左から右、そして下へ - space2d::CellOf)、大きさはそのセルの大きさ。
// スプライトのアニメーション (SpriteAnimation.h) がゲームが動く間 コマ を進める。

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
    RM_PROPERTY(Edit, Label="横のコマ数", Tooltip="画像を同じ大きさのコマに切るとき (アトラス): 横に並ぶコマの数", Min=1, Max=1024) int columns = 1;
    RM_PROPERTY(Edit, Label="縦のコマ数", Tooltip="画像を同じ大きさのコマに切るとき (アトラス): 縦に並ぶコマの数", Min=1, Max=1024) int rows = 1;
    RM_PROPERTY(Edit, Label="コマ", Tooltip="描くコマの番号 (左上が 0。左から右へ、行の終わりで下の行へ)。アニメーションがあると、Play 中はそれが決めます", Min=0, Max=1000000) int cell = 0;
    RM_PROPERTY(Edit, Label="くっきり (ドット絵)", Tooltip="拡大してもぼかさず、ピクセルをそのまま大きくします") bool crisp = false;
};

}  // namespace rm::game_x_2d
