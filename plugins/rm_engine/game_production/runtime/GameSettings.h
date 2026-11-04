#pragma once
// Settings of the game being made (saved in the project; read by the exported game later).
// Written once here: saving, loading, the inspector and undo all come from these lines (rmgen).

#include "core/reflect/Reflect.h"

#include <string>
#include <vector>

namespace rm::game_production {

RM_ENUM(Label="表示のしかた")
enum class ScreenMode {
    RM_ITEM(Label="ウィンドウ") Windowed,
    RM_ITEM(Label="フルスクリーン") Fullscreen,
    RM_ITEM(Label="枠なしの全画面") Borderless,
};

RM_STRUCT(Label="画面")
struct DisplaySettings {
    RM_PROPERTY(Edit, Label="幅", Min=320, Max=7680) int width = 1280;
    RM_PROPERTY(Edit, Label="高さ", Min=240, Max=4320) int height = 720;
    RM_PROPERTY(Edit, Label="表示のしかた") ScreenMode mode = ScreenMode::Windowed;
    RM_PROPERTY(Edit, Label="垂直同期", Tooltip="画面の書き換えに合わせて描きます (ちらつきを防ぐ)") bool vsync = true;
};

RM_STRUCT(Label="ゲームの設定")
struct GameSettings {
    RM_PROPERTY(Edit, Label="ゲームの名前", Tooltip="書き出した exe の名前とウィンドウのタイトルに使います") std::string title = "My Game";
    RM_PROPERTY(Edit, Label="目標 FPS", Min=1, Max=240) int targetFps = 60;
    RM_PROPERTY(Edit, Label="物理の刻み (秒)", Min=0.001, Max=0.1, Step=0.001) float fixedTimestep = 0.02f;
    RM_PROPERTY(Edit, Label="重力") Vec3 gravity{0, -9.81f, 0};
    RM_PROPERTY(Edit, Label="背景の色", Min=0, Max=1) Color clearColor{0.10f, 0.10f, 0.12f, 1};
    RM_PROPERTY(Edit, Label="画面") DisplaySettings display;
    RM_PROPERTY(Edit, Label="タグ") std::vector<std::string> tags;
};

}  // namespace rm::game_production
