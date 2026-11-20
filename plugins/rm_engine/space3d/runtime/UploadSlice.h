#pragma once
// 3D 空間 - 1 フレームが絵の GPU への送信に使ってよい時間 (Unity の asyncUploadTimeSlice と
// 同じ考え)。純粋 (tests/TestSpace3d.cpp でテストする): 呼ぶ側が各コピーを測り
// (QueryPerformanceCounter: platform::NowSeconds)、かかった時間を渡す。
//
//   if (slice.May(frame)) { const double t0 = NowSeconds(); Upload(...); slice.Spent(NowSeconds() - t0); }
//
// フレームの最初のコピーは必ず通す (どんなに大きくても絵が届き続けるように)。その後は、
// フレームが予算を使い切ったらコピーを止める。`frame` はフレームごとに変わる値なら何でもよい。
// Deferred(frame) はそのフレームで先送りしたコピーだけを数える。何も頼まなかったフレーム (もう
// 待っているものがない) は 0 - だから、ずっと前に済んだコピーのために描き直す者はいない。

#include <algorithm>
#include <cstddef>

namespace rm::space3d {

class UploadSlice {
public:
    explicit UploadSlice(double budgetSeconds) : budget_(budgetSeconds) {}

    bool May(double frame) {
        NewFrame(frame);
        if (copies_ == 0 || spent_ < budget_) return true;
        ++deferred_;
        return false;
    }
    void Spent(double seconds) {
        spent_ += std::max(seconds, 0.0);
        ++copies_;
    }
    // `frame` で先送りしたコピー (どれも次のフレームでまた頼む): 0 になるまで描き直す。
    size_t Deferred(double frame) const { return frame == frame_ ? deferred_ : 0; }
    double SpentThisFrame() const { return spent_; }

private:
    void NewFrame(double frame) {
        if (frame == frame_) return;
        frame_ = frame;
        spent_ = 0;
        copies_ = 0;
        deferred_ = 0;
    }

    double budget_;
    double frame_ = -1;
    double spent_ = 0;
    size_t copies_ = 0;
    size_t deferred_ = 0;
};

}  // namespace rm::space3d
