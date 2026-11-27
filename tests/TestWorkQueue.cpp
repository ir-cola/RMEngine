// 小さな作業の列 (src/base/WorkQueue) のテスト: どの番号も 1 回ずつ・作業スレッドが無くても・入れ子は順に・
// 何度呼んでも同じ・1 本と N 本で同じ結果。

#include "Test.h"

#include "base/WorkQueue.h"

#include <atomic>
#include <numeric>
#include <vector>

using namespace rm;

RM_TEST(WorkQueue_EveryIndexOnce) {
    for (const unsigned threads : {0u, 1u, 3u, 7u}) {
        WorkQueue q(threads);
        CHECK(threads == 0 ? q.Threads() <= 7 : q.Threads() == threads);
        for (const size_t n : {size_t{0}, size_t{1}, size_t{2}, size_t{17}, size_t{1000}}) {
            std::vector<std::atomic<int>> hits(n);
            q.ParallelFor(n, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) hits[i].fetch_add(1);
            });
            bool once = true;
            for (auto& h : hits) once = once && h.load() == 1;
            CHECK(once);
        }
    }
}

RM_TEST(WorkQueue_NestedRunsInPlaceAndRepeats) {
    WorkQueue q(3);
    std::atomic<int> total{0};
    q.ParallelFor(8, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            q.ParallelFor(10, [&](size_t b, size_t e) { total.fetch_add(static_cast<int>(e - b)); });  // (入れ子: そのまま順に)
        }
    });
    CHECK(total.load() == 80);
    // 何度も呼ぶ (作業スレッドが前の呼び出しと混ざらない)
    for (int round = 0; round < 200; ++round) {
        std::vector<int> v(64, 0);
        q.ParallelFor(v.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) v[i] = static_cast<int>(i) * 2;
        }, 3);
        long long sum = std::accumulate(v.begin(), v.end(), 0LL);
        CHECK(sum == 63 * 64);
    }
}
