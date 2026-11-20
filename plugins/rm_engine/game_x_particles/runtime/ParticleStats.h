#pragma once
// 3D の絵 (シーンビュー、ゲームビュー、書き出したゲーム) ごとに、パーティクルをいくつ再生しているか:
// 描き手が直近のフレームで数えた値で、エディターのデバッグパネルが読む。プログラムが動いている間
// ずっと、描き手ごとに持つ。しばらく知らせてこない項目は忘れる。

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rm::game_x_particles {

struct PictureParticles {
    std::string picture;  // "scene" / "game" / "preview" (WorldDrawFrame::picture。空: ゲーム自身のもの)
    bool playing = false;
    size_t systems = 0;   // 見つけた パーティクル の部品
    size_t effects = 0;   // 再生中のエフェクトの数 (それぞれ GPU 上に自分のプレイヤーを持つ)
    uint32_t emitters = 0;
    uint64_t capacity = 0;  // 持てるパーティクルの最大数
    uint64_t alive = 0;     // 今生きている数 (GPU から読み戻したもの)
    bool aliveKnown = false;
    double at = 0;  // 知らせた時の NowSeconds()
};

// 描き手がこのフレームに再生した数を知らせる (`drawer`: 描き手が誰か - 1 人に 1 項目)。
void ReportParticles(const void* drawer, PictureParticles stats);
// 直近 `maxAge` 秒以内に知らせてきた描き手。順番は決まっていない。
std::vector<PictureParticles> RecentParticles(double maxAge = 1.0);

}  // namespace rm::game_x_particles
