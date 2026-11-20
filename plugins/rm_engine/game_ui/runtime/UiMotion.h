#pragma once
// ゲーム UI - ゲームの Play 中に UI のものがどう動くか (UI-4: docs/UI_EDITOR.md)。純粋 (描画も
// シーンも扱わない): UI サービスが毎フレームそれを進めて UiLive (UiComponents.h) に書き、
// レイアウトがそれを UI の配置の上に重ねる。tests/TestGameUi.cpp でテストする。
//
//   出る・消える動き   UiTransition: その値から今のままのものへ (出る)、またはその逆 (消える)
//   道に沿って動く     UiPath: 点を通る線 (曲線: Catmull-Rom) を、ある速さで歩く
//   速さの曲線         Ease: 時間 0..1 を道のり 0..1 にする (はねる / 行き過ぎて戻る は途中で
//                      0..1 をはみ出し、終わりは 1)

#include "rm_engine/game_ui/runtime/UiComponents.h"

#include <vector>

namespace rm::game_ui {

// 時間 `t` (0..1) を曲線に通したもの: 0 で 0、1 で 1。
float Ease(UiEase ease, float t);

// 2 つの動きを同時に (窓が入ってくる間にそのものが道を歩く): 両方で動かし、両方で拡大縮小し、
// 両方で回し、両方でフェードする。
UiLive Combine(const UiLive& a, const UiLive& b);

// 始まって `time` 秒後の動き: 出る (`showing`) - 最初はその値、終わりでは今のままのもの。
// 消える - その逆。待ち の前は始まりのまま。
UiLive TransitionAt(const UiTransition& t, bool showing, float time);
// 終わっている (または何もしない: 使われていない)。
bool TransitionDone(const UiTransition& t, float time);

// 道を短い直線の連なりとして (smooth なら曲線)、そして各点までの線に沿った長さ。
struct UiPathLine {
    std::vector<Vec2> points;
    std::vector<float> along;  // 最初の点から
    float Length() const { return along.empty() ? 0.f : along.back(); }
};
UiPathLine PathLine(const UiPath& path);
// 線に沿って `distance` の所 (線の内側に収める) と、そこで進む向き (度: 右が 0、下が 90 -
// 画面の Y は下向き)。
struct UiPathPose {
    Vec2 offset{0, 0};
    float angle = 0;
};
UiPathPose PathPoseAt(const UiPathLine& line, float distance);
// 道の 1 フレーム (UiPath の保存されない値: started, playing, travelled, backward) と、いま
// その上のどこにいるか。
UiPathPose StepPath(UiPath& path, const UiPathLine& line, float dt);

}  // namespace rm::game_ui
