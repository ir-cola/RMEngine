#pragma once
// 書き出したゲームの描き方: ゲームの空間を知るプラグイン (3D: ゲーム制作 × 3D、2D:
// ゲーム制作 × 2D - ゲーム制作は空間を知らない) が行う。それぞれが自分のランタイム側から
// 描画担当を出す。ゲームは、ゲーム設定の空間 (GameSettings::space) のものを使う。
// それが未設定かなければ、order が一番小さいものを使う。
//
//   // コネクターの runtime/Register.cpp で:
//   static const game_production::GameDrawInfo kDraw{"3d", 0, &CreateGameDraw3D};
//   registry.Contribute(game_production::kGameDrawPoint, kDraw);

#include "base/Math.h"
#include "base/Ref.h"
#include "core/RuntimeRegistry.h"
#include "platform/Gfx.h"
#include "rm_engine/game_production/runtime/GameSettings.h"
#include "rm_engine/game_production/runtime/Scene.h"

#include <d2d1_1.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rm::game_production {

// ゲームの 1 フレームを描くための材料。
struct GameDrawFrame {
    const Scene& scene;
    uint64_t camera = 0;         // ゲームが通して見るエンティティ (SystemContext::camera)
    Color background{0, 0, 0, 1};  // ゲームの設定の 背景の色
    platform::Gfx& gfx;
    ID2D1DeviceContext* dc;      // ウィンドウのもの (クライアント領域全体)
    float width = 0, height = 0;  // DIPs
    // アセットのバイト列 (ゲームでは .rmpak から - 書き出しがそのために入れたもの。ベイクしたものか
    // ファイルそのもの)。なければ nullopt。
    std::function<std::optional<std::string>(const AssetRef&)> asset;
    // ゲーム自身のファイルをパスで (ベイクしたモデルの絵: "Baked/<id>/0.dds")。
    std::function<std::optional<std::string>(std::string_view path)> file;
    // プラグインが出すもの (描画担当は、他のプラグインが自分の絵に描くものをこれで頼む)。
    const RuntimeRegistry* registry = nullptr;
    float dt = 0;  // 前に描いたフレームからゲームが進んだ秒数 (一時停止中は 0)
};

class GameDrawer {
public:
    virtual ~GameDrawer() = default;
    virtual void Draw(const GameDrawFrame& f) = 0;
    // 別の空間の絵の上に (3D のゲームに重ねる 2D の層: カメラ (2D)「3D の上に重ねる」)。ゲーム自身の
    // 描画担当の後に、他の空間の描画担当が描く。既定では何もしない。
    virtual void DrawOverlay(const GameDrawFrame& f) { (void)f; }
};

// ゲーム画面全体の上に、すべての空間の絵の後に描く (ゲーム UI: rm_engine/game_ui)。空間には属さず、
// どの空間のゲームでも描く。どちらのホストも描く: 書き出したゲームはそのウィンドウに、エディターの
// ゲームビューはゲームの絵の上に (止まっているときも) - `area` の中に、ゲーム画面は `screen` として
// (エディターではゲーム設定の 画面: 描くものはエリアに合わせて拡大縮小される)。
struct ScreenDrawFrame {
    const Scene& scene;
    const GameSettings& settings;
    platform::Gfx& gfx;
    ID2D1DeviceContext* dc;
    float x = 0, y = 0, width = 0, height = 0;  // dc 上の場所 (DIPs)
    Vec2 screen{1280, 720};                    // それが表すゲームの画面 (DIPs)
    std::function<std::optional<std::string>(const AssetRef&)> asset;  // GameDrawFrame::asset と同じ
    bool playing = false;  // ゲームが動いている (エディターでは Play) - そうでなければ作っている途中のシーン
    bool paused = false;   // ... そして一時停止中 (エディターの ⏸): 自分で動くもの (動画) は待つ
};

class ScreenDrawer {
public:
    virtual ~ScreenDrawer() = default;
    virtual void Draw(const ScreenDrawFrame& f) = 0;
};

// RuntimeRegistry の口: 中身は ScreenDrawInfo (static)。`order` の順 (小さいほうが先) に描く。
inline constexpr char kScreenDrawPoint[] = "rm_engine.game_production.screen_draw";
struct ScreenDrawInfo {
    int order = 0;
    std::unique_ptr<ScreenDrawer> (*create)() = nullptr;
};

// RuntimeRegistry の口: 中身は GameDrawInfo (static)。
inline constexpr char kGameDrawPoint[] = "rm_engine.game_production.game_draw";
struct GameDrawInfo {
    const char* space = "";  // SceneViewInfo::space と同じ ("3d"、"2d")
    int order = 0;
    std::unique_ptr<GameDrawer> (*create)() = nullptr;
};

}  // namespace rm::game_production
