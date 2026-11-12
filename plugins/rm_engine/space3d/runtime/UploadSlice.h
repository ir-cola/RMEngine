#pragma once
// 3D 空間 - how much time a frame may spend sending pictures to the GPU (the idea of Unity's
// asyncUploadTimeSlice). Pure (tested in tests/TestSpace3d.cpp): the caller measures each copy
// (QueryPerformanceCounter: platform::NowSeconds) and says how long it took.
//
//   if (slice.May(frame)) { const double t0 = NowSeconds(); Upload(...); slice.Spent(NowSeconds() - t0); }
//
// The first copy of a frame always may (so pictures keep coming, however big one is); after it,
// copies stop when the frame has spent the budget. `frame` is any value that changes every frame.

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
    // Copies put off (in this frame or the one before): draw again until 0.
    size_t Deferred() const { return std::max(deferred_, deferredBefore_); }
    double SpentThisFrame() const { return spent_; }

private:
    void NewFrame(double frame) {
        if (frame == frame_) return;
        frame_ = frame;
        spent_ = 0;
        copies_ = 0;
        deferredBefore_ = deferred_;
        deferred_ = 0;
    }

    double budget_;
    double frame_ = -1;
    double spent_ = 0;
    size_t copies_ = 0;
    size_t deferred_ = 0, deferredBefore_ = 0;
};

}  // namespace rm::space3d
