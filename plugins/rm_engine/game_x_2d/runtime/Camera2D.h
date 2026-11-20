#pragma once
// ゲーム制作 × 2D - 2D のゲームのカメラ部品: ゲームが何を見るか、Transform 2D が決める場所
// (エンティティの場所が真ん中) から。ゲームはゲーム設定の 最初のカメラ で選んだカメラ越しに、
// ゲーム画面の形で見る。背景はカメラ自身の色がなければゲーム設定の 背景の色。
// 純粋 (I/O も描画もしない)。tests/TestSpace2d.cpp でテストする。
//
// エンティティと一緒に回る (親も含めたワールドの回転)。エンティティの拡大縮小は
// 見える範囲を変えない (変えるのは 見える大きさ)。
//
// 3D のゲームでは、3D の上に重ねる を付けたカメラが、ゲームビューで 3D の絵の上に 2D の絵を
// 重ねる (背景なし)。使うのは 最初のカメラ が カメラ (2D) ならそれ、そうでなければ階層で
// 最初の有効なもの。

#include "core/reflect/Reflect.h"
#include "rm_engine/space2d/runtime/View2D.h"

namespace rm::game_x_2d {

RM_STRUCT(Label="カメラ (2D)")
struct Camera2D {
    RM_PROPERTY(Edit, Label="見える大きさ", Tooltip="画面の縦に映る長さの半分 (単位)。小さいほど大きく映ります", Min=0.001, Max=100000)
    float size = 5.f;
    RM_PROPERTY(Edit, Label="自分の背景の色を使う", Tooltip="外すと、ゲームの設定の「背景の色」で塗ります") bool ownBackground = false;
    RM_PROPERTY(Edit, Label="背景の色", Tooltip="「自分の背景の色を使う」のとき", Min=0, Max=1) Color background{0.10f, 0.10f, 0.12f, 1};
    RM_PROPERTY(Edit, Label="3D の上に重ねる", Tooltip="ゲームの空間が 3D のとき、このカメラで見た 2D の絵をゲームビューの 3D の上に重ねます (点数や体力の表示など)。背景は塗りません")
    bool overlay = true;
};

// `world` (場所と回転) に置いたカメラ越しに、`viewHeight` DIP の絵をどう見るか。
space2d::Camera2D ViewOf(const Camera2D& camera, const space2d::Mat3& world, float viewHeight);
// `aspect` (幅 / 高さ) の絵に対してカメラが (平面で) 見る範囲: その半分の大きさ。
Vec2 HalfExtent(const Camera2D& camera, float aspect);
// 何かを描く前に絵を塗りつぶす色。
Color BackgroundOf(const Camera2D& camera, Color gameBackground);

}  // namespace rm::game_x_2d
