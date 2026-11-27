#pragma once
// 2D のシーンビューに、ほかのプラグインが道具を足す口 (タイルマップの 筆・四角・消しゴム・スポイト)。
// 道具は左上の帯の、移動 / 回転 / 拡大縮小の隣に正方形のボタンで並ぶ (使えるとき = Available だけ)。
// 道具を選んでいる間は、左ボタンは道具のもの (選ぶ・ギズモ・物のドラッグはしない)。右 / 中ドラッグと
// ホイールは今のまま (動かす・近づく)。W / E / R か Esc で移動の道具に戻る。Play 中は出さない。
//
//   // プラグインの RegisterEditor で:
//   static const game_x_2d::SceneTool2DInfo kTool{&CreateMyTool};
//   registry.Contribute(game_x_2d::kSceneTool2DPoint, kTool);
//
// 編集は道具が自分で Undo に積む (憲法 7: 1 回のドラッグ = 1 件)。

#include "editor/widgets/Widgets.h"
#include "rm_engine/game_production/editor/SceneView.h"
#include "rm_engine/game_x_2d/runtime/World2D.h"
#include "rm_engine/space2d/runtime/View2D.h"

#include <memory>
#include <span>
#include <vector>

namespace rm::game_x_2d {

inline constexpr char kSceneTool2DPoint[] = "rm_engine.game_x_2d.scene_tool";

// 帯のボタン 1 つ = 道具の 1 つの使い方 (筆・四角 …)。
struct SceneTool2DButton {
    editor::w::Icon icon = editor::w::Icon::Brush;
    const wchar_t* tooltip = nullptr;  // TrKey の文字列 (キーも書く: 「筆 (B)」)
    ui::Key key = ui::Key::None;       // これで選ぶ (None: キーなし)
};

// 道具が毎フレーム受け取るもの。
struct SceneTool2DFrame {
    ui::Context& c;
    ui::Rect view;  // シーンビューの四角
    game_production::SceneViewHost& host;
    const space2d::Camera2D& camera;
    std::span<const Placed2D> placed;
    const ImageLookup2D& images;
    bool hovered = false;  // マウスがビューの上 (帯の上ではない)
};

class SceneTool2D {
public:
    virtual ~SceneTool2D() = default;
    // 今使えるか (タイルの層を選んでいる)。使えないときはボタンを出さない。
    virtual bool Available(const game_production::SceneViewHost& host) const = 0;
    virtual std::span<const SceneTool2DButton> Buttons() const = 0;
    // 選んでいる間、毎フレーム (`mode` = 押したボタンの番号)。左ボタンとキー (B などを除く) を受け取る。
    virtual void Input(const SceneTool2DFrame& f, int mode) = 0;
    // 絵の上に重ねる物 (置く前の絵・マスの枠)。平面の上の物で、全部の物の後に描く。
    virtual void Draw(const SceneTool2DFrame& f, int mode, std::vector<space2d::SpriteDraw>& sprites, std::vector<space2d::Line2D>& lines) = 0;
    // ドラッグ中か (その間はビューのキー (W / E / R / F …) を使わない)。
    virtual bool Busy() const = 0;
    // 道具をやめる (別の道具・使えなくなった): ドラッグ中なら元に戻して、何も積まない。
    virtual void Cancel(game_production::SceneViewHost& host) = 0;
};

struct SceneTool2DInfo {
    std::unique_ptr<SceneTool2D> (*create)() = nullptr;
};

}  // namespace rm::game_x_2d
