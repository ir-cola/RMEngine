// Tests for the log (src/base/Log): writing from many threads at once, sinks that run on any thread or
// wait for the owner's thread (DeliverQueued), and RemoveSink never leaving a sink being called.

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
    std::set<uint64_t> seen;  // every entry once, none torn
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
    // The ring keeps the newest 2000, in order.
    const auto snap = log::Snapshot();
    CHECK(snap.size() == 2000);
    bool ordered = true;
    for (size_t i = 1; i < snap.size(); ++i) ordered = ordered && snap[i].seq == snap[i - 1].seq + 1;
    CHECK(ordered);
}

RM_TEST(Log_RemoveSinkWaitsForCallsOnOtherThreads) {
    // A sink that takes a while; RemoveSink on this thread while other threads keep writing. After it
    // returns, the sink must never be called again (its owner may be gone).
    std::atomic<bool> removed{false};
    std::atomic<bool> calledAfter{false};
    std::atomic<int> calls{0};
    const int sink = log::AddSink([&](const log::Entry&) {
        if (removed.load()) calledAfter = true;
        calls.fetch_add(1);
        std::this_thread::sleep_for(std::chrono::microseconds(200));
        if (removed.load()) calledAfter = true;  // still inside when RemoveSink returned
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
    std::this_thread::sleep_for(std::chrono::milliseconds(20));  // writers go on a little
    stop = true;
    for (auto& th : threads) th.join();
    CHECK(!calledAfter.load());
    log::RemoveSink(sink);  // twice: nothing
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
    log::DeliverQueued();  // (anything left from before)
    got.clear();
    std::thread writer([] {
        for (int i = 0; i < 100; ++i) log::Info("queued {}", i);
    });
    writer.join();
    CHECK(got.empty());   // nothing until this thread asks
    CHECK(wakes.load() == 1);  // woken once (the queue was empty), not 100 times
    log::DeliverQueued();
    CHECK(got.size() == 100);
    CHECK(!otherThread);
    bool ordered = true;
    for (size_t i = 1; i < got.size(); ++i) ordered = ordered && got[i] > got[i - 1];
    CHECK(ordered);
    // Removed: what was waiting for it is dropped.
    log::Info("after");
    log::RemoveSink(sink);
    got.clear();
    log::DeliverQueued();
    CHECK(got.empty());
    log::SetWake({});
}

RM_TEST(Log_SinkMayRemoveItselfAndLog) {
    // A sink that logs and removes itself on the same thread does not wait for itself.
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
