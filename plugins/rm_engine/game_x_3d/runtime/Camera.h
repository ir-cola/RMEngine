#pragma once
// ゲーム制作 × 3D - the camera component: what the game sees, from where its Transform puts it. It
// looks along its entity's own -Z (as the editing camera; the Transform's rotation turns it), +Y up.
// Pure values and the numbers made from them - tested in tests/TestSpace3d.cpp.
//
// The game sees through the camera chosen in the game settings (最初のカメラ). Its background is the
// game settings' 背景の色 unless the camera has its own (自分の背景の色を使う).

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

// The camera's projection for a picture of `aspect` (width / height). A far plane not beyond the near
// one is taken a little beyond it (the picture stays drawable while the numbers are being typed).
space3d::Mat4 ProjectionOf(const Camera& camera, float aspect);
// The same camera with its far plane brought in to at most `farthest` (the scene view draws its shape
// that far: a far plane at 1000 would be lines across the whole floor).
Camera ShownNear(const Camera& camera, float farthest);
// What the picture is filled with before anything is drawn.
Color BackgroundOf(const Camera& camera, Color gameBackground);

}  // namespace rm::game_x_3d
