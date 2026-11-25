#pragma once
// ゲーム制作 × アニメーション - アニメーターの決まり (純粋: 描画もファイルもシーンもない)。
// tests/TestGameAnimation.cpp でテストする。描く側 (AnimatorPoser) はこれを呼ぶだけ。
//
//   FindClip        クリップを名前で (空: 最初のクリップ)
//   WhatToShow      今の場面で何を見せるか (流す / 最初のコマ / プレビュー / バインドの形)
//   PreviewClock    止まっているエディターのプレビューの時計 (選んだアニメーター。再生 / 一時停止・つまみ)
//   PoseStamp       姿勢の版 (MeshDraw::poseVersion): 姿勢を決めるものが変わったときだけ新しい番号

#include "rm_engine/space3d/runtime/Skeleton.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rm::game_x_animation {

// 名前 `name` のクリップの番号 (同じ名前が 2 つあれば最初のもの)。空の名前: 最初のクリップ。
// 見つからない・クリップが無い: -1。
int FindClip(std::span<const space3d::Clip> clips, std::string_view name);

enum class Showing {
    BindPose,    // クリップが無い (見つからない): バインドの形 (スキンの行列を渡さない)
    FirstFrame,  // クリップの始めのコマで止める
    Playing,     // 時間で流す (ゲーム)
    Preview,     // プレビューの時計の時刻 (止まっているエディターで選んだもの)
};
// gamePlaying: Play・書き出したゲーム。previewed: 止まっているエディターでプレビューしているもの。
//   クリップが無い → BindPose
//   ゲーム → 始めから流す なら Playing、でなければ FirstFrame
//   エディター → プレビューしているなら Preview、でなければ FirstFrame
Showing WhatToShow(bool gamePlaying, bool playOnStart, bool previewed, bool hasClip);

// 流し始めの時刻: 逆に流すループでないクリップは終わりから (animation3d::Player::Play と同じ)。
float StartTime(float speed, float duration, bool loop);

// 止まっているエディターのプレビューの時計 (選んだアニメーター 1 つ)。シーンビューの絵が進め、
// 絵の上の操作 (再生 / 一時停止・時間のつまみ) が変える。
struct PreviewClock {
    int entity = 0;       // 時計の持ち主 (0: 無し)
    bool paused = false;  // 一時停止
    bool fresh = true;    // 持ち主が変わった: 次の Advance で始めの時刻に置く
    float time = 0;       // 秒
    // 描く側が知らせるもの (つまみの幅と、見せる名前)。
    bool hasClip = false;
    float duration = 0;
    std::string clip;
};
// 選んだもの `entity` (0: 無し) に合わせる: 持ち主が変わったら、始めから流す。
void FollowSelection(PreviewClock& clock, int entity);
// dt 秒 x speed 進める (一時停止中・つまみで動かしている間は進めない)。ループ: [0, duration) に回す。
// ループでない: 端で止まって一時停止になる (▶ でまた始めから)。
void AdvancePreview(PreviewClock& clock, float dt, float speed, float duration, bool loop);
// ▶ / ‖: 一時停止を入れ替える。ループでないクリップが端で止まっていたら、始めの時刻から流し直す。
void TogglePreview(PreviewClock& clock, float speed, float duration, bool loop);
// 時間のつまみ: その時刻に置いて一時停止にする ([0, duration] に収める)。
void SeekPreview(PreviewClock& clock, float time, float duration);

// 姿勢の版 (MeshDraw::poseVersion)。姿勢を決めるもの (モデル・クリップ・時刻・ループ) が前と同じなら
// 前の番号のまま (影の地図を使い回せる・骨の行列を計算し直さない)、違えば `next` から新しい番号を取る。
class PoseStamp {
public:
    // 新しい番号にしたとき true (呼ぶ側は骨の行列を計算し直す)。
    bool Update(const void* model, int clip, float time, bool loop, uint64_t (*next)());
    uint64_t Version() const { return version_; }
    void Reset() { version_ = 0; }

private:
    const void* model_ = nullptr;
    int clip_ = -1;
    float time_ = 0;
    bool loop_ = false;
    uint64_t version_ = 0;  // 0: まだ無い
};

}  // namespace rm::game_x_animation
