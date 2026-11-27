#pragma once
// 3D アニメーション - 遠い物を間引く (docs/ANIMATION.md 7-3): 画面の上の大きさで、姿勢を作るのを 1 / 2 / 4 フレームに
// 1 回にする (間は前の骨の行列のまま)。見えない物は姿勢を作らない (時刻・ルール・イベント・ルートモーションは進める:
// 6 章の 18)。純粋。tests/TestAnimation3d.cpp でテストする。

namespace rm::animation3d {

// 画面の高さに対する、物の高さの割合がこれ以上なら毎フレーム / 2 フレームに 1 回。より小さいと 4 フレームに 1 回。
inline constexpr float kThinNear = 0.2f;
inline constexpr float kThinMid = 0.07f;

// 姿勢を作る間隔 (フレーム)。`screenFraction`: 前のフレームの画面の上の高さの割合 (0 以下: 見えなかった)。
// `thin` が false: いつも 1。見えない物は 0 (作らない)。
int PoseInterval(float screenFraction, bool thin);

// このフレームに姿勢を作るか: 間隔ごとに 1 回 (物ごとに `phase` でずらして、同じフレームに集まらないように)。
bool PoseThisFrame(int interval, unsigned long long frame, unsigned phase);

}  // namespace rm::animation3d
