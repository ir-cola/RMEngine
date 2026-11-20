// ログ (src/base/Log) のテスト: 多くのスレッドから同時に書く、どのスレッドでも動くシンクと
// 持ち主のスレッドを待つシンク (DeliverQueued)、そして RemoveSink の後にシンクが呼ばれないこと。

#include "Test.h"

#include "base/Log.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

using namespace rm;

RM_TEST(Log_ManyThreadsAtOnce) {
    constexpr int kThreads = 8, kEach = 2000;
    std::atomic<int> calls{0};
    std::mutex mtx;
    std::set<uint64_t> seen;  // どの記録も 1 回ずつで、欠けていない
    bool torn = false;
    const int sink = log::AddSink([&](const log::Entry& e) {
        calls.fetch_add(1);
        std::lock_guard lock(mtx);
        if (!e.text.starts_with("thread ")) torn = true;
        seen.insert(e.seq);
    });
    const uint64_t before = log::LastSeq();
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < kEach; ++i) log::Info("thread {} line {}", t, i);
        });
    }
    for (auto& th : threads) th.join();
    log::RemoveSink(sink);
    CHECK(calls.load() == kThreads * kEach);
    CHECK(!torn);
    CHECK(seen.size() == static_cast<size_t>(kThreads * kEach));
    CHECK(log::LastSeq() == before + kThreads * kEach);
    // リングは新しい 2000 件を順番に保つ。
    const auto snap = log::Snapshot();
    CHECK(snap.size() == 2000);
    bool ordered = true;
    for (size_t i = 1; i < snap.size(); ++i) ordered = ordered && snap[i].seq == snap[i - 1].seq + 1;
    CHECK(ordered);
}

RM_TEST(Log_RemoveSinkWaitsForCallsOnOtherThreads) {
    // 時間のかかるシンク。他のスレッドが書き続けるなか、このスレッドで RemoveSink する。戻った後は、
    // シンクを二度と呼んではいけない (持ち主はもう無いかもしれない)。
    std::atomic<bool> removed{false};
    std::atomic<bool> calledAfter{false};
    std::atomic<int> calls{0};
    const int sink = log::AddSink([&](const log::Entry&) {
        if (removed.load()) calledAfter = true;
        calls.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (removed.load()) calledAfter = true;  // RemoveSink が戻ったとき、まだ中にいた
    });
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            while (!stop.load()) log::Info("writing");
        });
    }
    while (calls.load() < 50) std::this_thread::yield();
    log::RemoveSink(sink);
    removed = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));  // 書き手は少し続ける
    stop = true;
    for (auto& th : threads) th.join();
    CHECK(!calledAfter.load());
    log::RemoveSink(sink);  // 2 回: 何もしない
}

RM_TEST(Log_QueuedSinksWaitForTheirThread) {
    std::vector<uint64_t> got;
    bool otherThread = false;
    const auto me = std::this_thread::get_id();
    const int sink = log::AddSink(
        [&](const log::Entry& e) {
            if (std::this_thread::get_id() != me) otherThread = true;
            got.push_back(e.seq);
        },
        log::Delivery::Queued);
    std::atomic<int> wakes{0};
    log::SetWake([&] { wakes.fetch_add(1); });
    log::DeliverQueued();  // (前から残っているもの)
    got.clear();
    std::thread writer([] {
        for (int i = 0; i < 100; ++i) log::Info("queued {}", i);
    });
    writer.join();
    CHECK(got.empty());   // このスレッドが頼むまで何も来ない
    CHECK(wakes.load() == 1);  // 1 回だけ起こされる (キューが空だったので)。100 回ではない
    log::DeliverQueued();
    CHECK(got.size() == 100);
    CHECK(!otherThread);
    bool ordered = true;
    for (size_t i = 1; i < got.size(); ++i) ordered = ordered && got[i] > got[i - 1];
    CHECK(ordered);
    // 外した: それを待っていたものは捨てる。
    log::Info("after");
    log::RemoveSink(sink);
    got.clear();
    log::DeliverQueued();
    CHECK(got.empty());
    log::SetWake({});
}

RM_TEST(Log_SinkMayRemoveItselfAndLog) {
    // ログを出して同じスレッドで自分を外すシンクは、自分を待たない。
    int id = 0;
    int calls = 0;
    id = log::AddSink([&](const log::Entry& e) {
        ++calls;
        if (e.text == "remove me") log::RemoveSink(id);
    });
    log::Info("remove me");
    log::Info("not seen");
    CHECK(calls == 1);
}
