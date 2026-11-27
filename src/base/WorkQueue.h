#pragma once
// 小さな作業の列 (docs/ANIMATION.md 7-3・6 章の 17。ゲームにも入る: 標準ライブラリだけ)。決まった数の作業スレッド
// (std::jthread) が、`ParallelFor` の範囲を区切って並べて処理する。呼んだスレッドも手伝い、全部終わるまで戻らない。
// 中で ParallelFor を呼ぶと (入れ子)、そのスレッドでそのまま順に処理する (待ち合わせで止まらない)。
// 同時に ParallelFor を呼べるのは 1 つのスレッドから (ゲームのシステムは主のスレッドから順に呼ぶ)。
//
//   SharedWorkQueue().ParallelFor(count, [&](size_t begin, size_t end) { for (size_t i = begin; i < end; ++i) Work(i); });
//
// 使うもの: システム「アニメーション」(アニメーターごとの姿勢)。後で物理・パーティクル・天候も。
// tests/TestWorkQueue.cpp でテストする。

#include <cstdint>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace rm {

class WorkQueue {
public:
    // `threads`: 作業スレッドの数 (呼んだスレッドの分は含まない)。0: このパソコンの論理コア - 1 (多くて 7)。
    explicit WorkQueue(unsigned threads = 0);
    ~WorkQueue();
    WorkQueue(const WorkQueue&) = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;

    // [0, n) を `chunk` 個ずつに区切り、`fn(begin, end)` を並べて呼ぶ。全部終わってから戻る。
    // chunk が 0: スレッドの数から決める。n が小さい・作業スレッドが無い・入れ子: 呼んだスレッドで順に。
    void ParallelFor(size_t n, const std::function<void(size_t begin, size_t end)>& fn, size_t chunk = 0);
    unsigned Threads() const { return static_cast<unsigned>(workers_.size()); }

private:
    void Work(std::stop_token stop);
    bool RunChunk();

    std::mutex mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(size_t, size_t)>* fn_ = nullptr;
    size_t n_ = 0, chunk_ = 1, chunks_ = 0;
    size_t next_ = 0;         // 次に渡す区切り (mutex_ の中で)
    size_t finished_ = 0;     // 終わった区切り (mutex_ の中で数える)
    uint64_t generation_ = 0;  // 新しい ParallelFor ごとに増える
    std::vector<std::jthread> workers_;
};

// プログラムに 1 つの作業の列 (初めて使うときに作る)。
WorkQueue& SharedWorkQueue();

}  // namespace rm
