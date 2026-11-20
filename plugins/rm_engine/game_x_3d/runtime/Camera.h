#pragma once
// ゲーム制作 × 3D - カメラの部品: ゲームが見るもの。見る位置は Transform が決める。
// その物自身の -Z の向きを見る (編集用カメラと同じ。Transform の回転で向きが変わる)。+Y が上。
// 純粋な値と、そこから作る数だけ - tests/TestSpace3d.cpp でテストする。
//
// ゲームは、ゲーム設定で選んだカメラ (最初のカメラ) を通して見る。背景は、カメラが自分の
// ものを持たない限りゲーム設定の 背景の色 (自分の背景の色を使う で持てる)。

#include "core/reflect/Reflect.h"
#include "rm_engine/space3d/runtime/Transform.h"

namespace rm::game_x_3d {

RM_ENUM(Label="投影")
enum class CameraProjection {
    RM_ITEM(Label="透視") Perspective,
    RM_ITEM(Label="平行投影") Orthographic,
};

RM_STRUCT(Label="カメラ")
struct Camera {
    RM_PROPERTY(Edit, Label="投影", Tooltip="透視: 遠くの物ほど小さく見える / 平行投影: 遠くても同じ大きさ (図面や 2D 風のゲーム)")
    CameraProjection projection = CameraProjection::Perspective;
    RM_PROPERTY(Edit, Label="画角 (度)", Tooltip="透視のとき。画面の縦に見える角度", Min=1, Max=179) float fieldOfView = 60.f;
    RM_PROPERTY(Edit, Label="見える大きさ", Tooltip="平行投影のとき。画面の縦に映る長さの半分", Min=0.001, Max=100000) float orthographicSize = 5.f;
    RM_PROPERTY(Edit, Label="近い面", Tooltip="これより近い物は映りません", Min=0.001, Max=100000) float nearClip = 0.1f;
    RM_PROPERTY(Edit, Label="遠い面", Tooltip="これより遠い物は映りません (近い面より遠く)", Min=0.01, Max=1000000) float farClip = 1000.f;
    RM_PROPERTY(Edit, Label="自分の背景の色を使う", Tooltip="外すと、ゲームの設定の「背景の色」で塗ります") bool ownBackground = false;
    RM_PROPERTY(Edit, Label="背景の色", Tooltip="「自分の背景の色を使う」のとき", Min=0, Max=1) Color background{0.10f, 0.10f, 0.12f, 1};
};

// 縦横比 `aspect` (幅 / 高さ) の絵のためのカメラの射影。near より遠くない far は、少しだけ
// その先にとる (数字を打っている間も絵は描ける)。
space3d::Mat4 ProjectionOf(const Camera& camera, float aspect);
// 遠い面を最大 `farthest` までに縮めた同じカメラ (シーンビューはその形をそこまでで描く:
// 1000 の遠い面だと、床全体に線が走ってしまう)。
Camera ShownNear(const Camera& camera, float farthest);
// 何かを描く前に絵を塗りつぶす色。
Color BackgroundOf(const Camera& camera, Color gameBackground);

}  // namespace rm::game_x_3d
