#pragma once
// ゲーム制作 × 2D - the camera component of a 2D game: what the game sees, from where its Transform 2D
// puts it (its middle at the entity's place). The game sees through the camera chosen in the game
// settings (最初のカメラ), in the game screen's shape. Its background is the game settings' 背景の色
// unless the camera has its own. Pure - tested in tests/TestSpace2d.cpp.
//
// ★ The camera does not turn or scale with its Transform 2D yet (only its place is used).

#include "core/reflect/Reflect.h"
#include "rm_engine/space2d/runtime/View2D.h"

namespace rm::game_x_2d {

RM_STRUCT(Label="カメラ (2D)")
struct Camera2D {
    RM_PROPERTY(Edit, Label="見える大きさ", Tooltip="画面の縦に映る長さの半分 (単位)。小さいほど大きく映ります", Min=0.001, Max=100000)
    float size = 5.f;
    RM_PROPERTY(Edit, Label="自分の背景の色を使う", Tooltip="外すと、ゲームの設定の「背景の色」で塗ります") bool ownBackground = false;
    RM_PROPERTY(Edit, Label="背景の色", Tooltip="「自分の背景の色を使う」のとき", Min=0, Max=1) Color background{0.10f, 0.10f, 0.12f, 1};
};

// How the picture of `viewHeight` DIPs is looked at through a camera placed by `world`.
space2d::Camera2D ViewOf(const Camera2D& camera, const space2d::Mat3& world, float viewHeight);
// What the camera sees (in the plane) for a picture of `aspect` (width / height): its half size.
Vec2 HalfExtent(const Camera2D& camera, float aspect);
// What the picture is filled with before anything is drawn.
Color BackgroundOf(const Camera2D& camera, Color gameBackground);

}  // namespace rm::game_x_2d
