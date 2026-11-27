#include "base/WorkQueue.h"

#include <algorithm>

namespace rm {

namespace {

thread_local bool t_inside = false;  // このスレッドが今 ParallelFor の区切りを処理している (入れ子は順に)

}  // namespace

WorkQueue::WorkQueue(unsigned threads) {
    if (threads == 0) {
        const unsigned cores = std::thread::hardware_concurrency();
        threads = cores > 1 ? std::min(cores - 1, 7u) : 0u;
    }
    for (unsigned i = 0; i < threads; ++i) workers_.emplace_back([this](std::stop_token stop) { Work(stop); });
}

WorkQueue::~WorkQueue() {
    for (auto& w : workers_) w.request_stop();
    {
        std::lock_guard lock(mutex_);
        ++generation_;
    }
    wake_.notify_all();
    workers_.clear();  // (jthread は止めて join する)
}

bool WorkQueue::RunChunk() {
    // 区切りを 1 つもらう (列の中身は mutex_ の中でだけ読む: 遅れて起きた作業スレッドが、次の ParallelFor と混ざらない)。
    const std::function<void(size_t, size_t)>* fn = nullptr;
    size_t begin = 0, end = 0;
    {
        std::lock_guard lock(mutex_);
        if (!fn_ || next_ >= chunks_) return false;
        const size_t k = next_++;
        fn = fn_;
        begin = k * chunk_;
        end = std::min(n_, begin + chunk_);
    }
    const bool was = t_inside;
    t_inside = true;
    (*fn)(begin, end);
    t_inside = was;
    {
        std::lock_guard lock(mutex_);
        if (++finished_ == chunks_) done_.notify_all();
    }
    return true;
}

void WorkQueue::Work(std::stop_token stop) {
    uint64_t seen = 0;
    while (true) {
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop.stop_requested() || generation_ != seen; });
            if (stop.stop_requested()) return;
            seen = generation_;
            if (!fn_) continue;
        }
        while (RunChunk()) {
        }
    }
}

void WorkQueue::ParallelFor(size_t n, const std::function<void(size_t, size_t)>& fn, size_t chunk) {
    if (n == 0) return;
    if (workers_.empty() || t_inside || n == 1) {
        const bool was = t_inside;
        t_inside = true;
        fn(0, n);
        t_inside = was;
        return;
    }
    if (chunk == 0) chunk = std::max<size_t>(1, n / ((workers_.size() + 1) * 4));
    {
        std::lock_guard lock(mutex_);
        fn_ = &fn;
        n_ = n;
        chunk_ = chunk;
        chunks_ = (n + chunk - 1) / chunk;
        next_ = 0;
        finished_ = 0;
        ++generation_;
    }
    wake_.notify_all();
    while (RunChunk()) {
    }
    std::unique_lock lock(mutex_);
    done_.wait(lock, [&] { return finished_ == chunks_; });
    fn_ = nullptr;  // (遅れて起きた作業スレッドは、区切りが無いのでそのまま待ちに戻る)
}

WorkQueue& SharedWorkQueue() {
    static WorkQueue queue;
    return queue;
}

}  // namespace rm
