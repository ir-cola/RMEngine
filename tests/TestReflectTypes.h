#pragma once
// テストで使う反映された型 (rmgen が RMTests のためにこのファイルを読む)。

#include "core/reflect/Reflect.h"

#include <string>
#include <vector>

namespace rmtest {

RM_ENUM(Label="品質")
enum class Quality { RM_ITEM(Label="低") Low, Medium, RM_ITEM(Label="高") High = 7 };

RM_STRUCT()
struct Window {
    RM_PROPERTY(Edit, Min=320, Max=7680) int width = 1280;
    RM_PROPERTY(Edit, Min=240) int height = 720;
    RM_PROPERTY(Edit) bool fullscreen = false;
};

RM_STRUCT(Label="テスト用の設定")
struct Settings {
    RM_PROPERTY(Edit, Label="名前", Tooltip="ゲームの名前") std::string title = "My Game";
    RM_PROPERTY(Edit, Label="速さ", Min=0, Max=10, Step=0.5) float speed = 5.0f;
    RM_PROPERTY(Edit, Min=-3, Max=3) int level = 1;
    RM_PROPERTY(Edit) bool vsync = true;
    RM_PROPERTY(Edit, Min=0, Max=1) rm::Color clear{0.1f, 0.2f, 0.3f, 1.0f};
    RM_PROPERTY(Edit) rm::Vec2 gravity2d{0, -9.8f};
    RM_PROPERTY(Edit) rm::Vec3 gravity{0, -9.8f, 0};
    RM_PROPERTY(Edit) Quality quality = Quality::Medium;
    RM_PROPERTY(Edit) Window window;
    RM_PROPERTY(Edit) std::vector<int> scores;
    RM_PROPERTY(Edit) std::vector<Window> monitors;
    RM_PROPERTY(Edit) std::vector<Quality> history;
    RM_PROPERTY(ReadOnly) std::string build = "dev";
    RM_TRANSIENT() float timer = 0;
    RM_TRANSIENT() void* runtimeHandle = nullptr;
};

// Decimals=1: 小数第 1 位までを残し (残りは切り捨て)、そう表示する。
RM_STRUCT()
struct Tenths {
    RM_PROPERTY(Edit, Min=0, Max=100, Decimals=1) float amount = 20.f;
    RM_PROPERTY(Edit, Decimals=2) rm::Vec2 place{0, 0};
};

// 参照 (base/Ref.h): ID で保存する。
RM_STRUCT()
struct Links {
    RM_PROPERTY(Edit, AssetKind="model") rm::AssetRef model;
    RM_PROPERTY(Edit) rm::EntityRef target;
    RM_PROPERTY(Edit, AssetKind="image,model") std::vector<rm::AssetRef> textures;
    RM_PROPERTY(Edit) std::vector<rm::EntityRef> followers;
};

}  // namespace rmtest
