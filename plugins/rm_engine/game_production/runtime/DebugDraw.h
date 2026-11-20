#pragma once
// ゲームのスクリプトが、自分のしていることを見るために描く線 (RM3D.h の rm::Debug::Line / Box /
// Sphere / Ray、RM2D.h の Line / Rect / Circle - 設計 RME_PLATFORM 4-b)。見せるのはエディターだけ
// (Play 中の、ゲームビューとシーンビュー): 書き出したゲームはスクリプトにバッファーを渡さない
// (ScriptWorld::debug は null のまま) ので、そこでは呼んでも何も起きない - 保持も描画もしない。
//
// どの形も線として保持する (箱・球・長方形・円は、コネクターの部分が線にする)。描かれた空間のまま。
// 0 秒で与えた線は描かれたフレームの間だけ見える。N 秒なら、Play の時間で N 秒たつまで。
// 純粋 (I/O も描画もしない)。tests/TestScripts.cpp でテストする。
//
//   毎フレーム (Play):   debug.BeginFrame(dt)   -> 時間の切れたものが消える。その後スクリプトが動く
//   スクリプト:          debug.Add(...)          -> 保持する (false: いっぱい - kMaxLines)
//   ビュー:              debug.Lines()           -> 描く (3D: 隠れた部分は薄く、2D: 一番上に)
//   Stop:                debug.Clear()

#include "base/Math.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rm::game_production {

enum class DebugSpace : uint8_t { Space3D, Space2D };

struct DebugLine {
    Vec3 a, b;  // 2D: z = 0
    Color color;
    DebugSpace space = DebugSpace::Space3D;
    bool oneFrame = true;  // 0 秒で与えた: 次のフレームで消える
    double until = 0;      // そうでなければ: 時計がこの値以下の間は保持する
};

class DebugDraw {
public:
    // これより多いものは捨てる (最初の 1 回だけ知らせる: TakeOverflowNotice)。線は描くのが安い
    // (全部を 1 回の描画で描く) ので、この上限は、暴走したスクリプト (追加し続けるループ) が
    // メモリをいっぱいにするのを止めるためだけのもの。
    static constexpr size_t kMaxLines = 50'000;

    // Play の新しいフレーム、前から `dt` 秒後: 前のフレームで 0 秒で与えられた線と、秒数が
    // 過ぎた線を捨てる。
    void BeginFrame(double dt);
    // `seconds` 秒間見せる線 (0 以下: このフレームだけ)。捨てたとき (いっぱい) は false。
    bool Add(DebugSpace space, Vec3 a, Vec3 b, Color color, double seconds);
    // 線が初めて捨てられた後で 1 回だけ true (コンソールに 1 回だけ知らせるため)。もう一度は、Clear の後だけ。
    bool TakeOverflowNotice();
    // Stop: すべて消える (次のときに、また知らせてよい)。
    void Clear();

    std::span<const DebugLine> Lines() const { return lines_; }
    size_t Dropped() const { return dropped_; }
    double Clock() const { return clock_; }

private:
    std::vector<DebugLine> lines_;
    double clock_ = 0;
    size_t dropped_ = 0;
    bool noticeDue_ = false, noticeTold_ = false;
};

}  // namespace rm::game_production
