#pragma once
// 3D 空間 - how much time a frame may spend sending pictures to the GPU (the idea of Unity's
// asyncUploadTimeSlice). Pure (tested in tests/TestSpace3d.cpp): the caller measures each copy
// (QueryPerformanceCounter: platform::NowSeconds) and says how long it took.
//
//   if (slice.May(frame)) { const double t0 = NowSeconds(); Upload(...); slice.Spent(NowSeconds() - t0); }
//
// The first copy of a frame always may (so pictures keep coming, however big one is); after it,
// copies stop when the frame has spent the budget. `frame` is any value that changes every frame.
// Deferred(frame) counts the copies put off in that frame only: a frame that asked for none (nothing
// is waiting any more) has none - so nobody draws again for copies that were made long ago.

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
    // Copies put off in `frame` (each of them asks again in the next frame): draw again until 0.
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
