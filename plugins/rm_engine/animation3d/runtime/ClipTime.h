#pragma once
// 3D アニメーション - クリップの再生の位置を進めることと、その間に通ったイベント (docs/ANIMATION.md 3-6)。
// 純粋。tests/TestAnimation3d.cpp でテストする。
//
// 「通った」の決まり (再生の位置が e に**着いた**フレームに 1 回):
//   前へ (speed > 0):  prev < e <= now        (ループの継ぎ目をまたいだら: prev < e < duration と 0 <= e <= now)
//   後ろへ (speed < 0): now <= e < prev        (またいだら: 0 <= e < prev と now <= e < duration)
//   1 フレームで何周もしたら、間の周の分も (1 周に 1 回ずつ) 出す。
// ループするクリップでは 時刻 duration と 時刻 0 は同じ所 (継ぎ目)。duration に置いたイベントは 0 のものとして数える。

#include <span>
#include <vector>

namespace rm::animation3d {

struct TimeStep {
    float time = 0;  // 進めた後の時刻 (ループ: [0, duration)、ループでない: [0, duration])
    int loops = 0;   // またいだ継ぎ目の数 (前へも後ろへも 0 以上。ループでないときは 0)
};

// `time` を dt x speed だけ進める (speed は負でもよい: 逆再生)。duration <= 0 のクリップは 0 のまま。
TimeStep AdvanceTime(float time, float dt, float speed, float duration, bool loop);

// 1 フレームで出すイベントの周の数の上限 (とても短いクリップを速く流しても、出す数が膨らまないように)。
inline constexpr int kMaxEventLoops = 64;

// AdvanceTime(prevTime, ...) = step の間に通ったイベントの番号 (`eventTimes` の番号) を、通った順に `out` に
// 足す (out は空にしない)。同じイベントが何周分も入ることがある。範囲 [0, duration] の外の時刻は無視する。
// prevTime は前の TimeStep::time (ループなら [0, duration) にあるもの)。
// includeStart: 再生を始めた最初のフレームで、始めの時刻ちょうどのイベントも出す (prev <= e)。
void PassedEvents(float prevTime, const TimeStep& step, float speed, float duration, std::span<const float> eventTimes,
                  std::vector<int>& out, bool includeStart = false);

}  // namespace rm::animation3d
