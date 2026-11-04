#include "base/Log.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>

namespace rm::log {

namespace {

constexpr size_t kRingMax = 2000;

struct State {
    std::mutex mtx;
    std::deque<Entry> ring;
    std::vector<std::pair<int, Sink>> sinks;
    int nextSinkId = 1;
    uint64_t seq = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

State& S() {
    static State s;
    return s;
}

}  // namespace

int AddSink(Sink sink) {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    const int id = s.nextSinkId++;
    s.sinks.emplace_back(id, std::move(sink));
    return id;
}

void RemoveSink(int id) {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    std::erase_if(s.sinks, [id](const auto& p) { return p.first == id; });
}

void Write(Level level, std::string text) {
    auto& s = S();
    Entry e;
    std::vector<Sink> sinks;
    {
        std::lock_guard lock(s.mtx);
        e.level = level;
        e.time = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
        e.seq = ++s.seq;
        e.text = std::move(text);
        s.ring.push_back(e);
        if (s.ring.size() > kRingMax) s.ring.pop_front();
        sinks.reserve(s.sinks.size());
        for (const auto& [id, sink] : s.sinks) sinks.push_back(sink);
    }
    // Sinks run outside the lock so they may log or read Snapshot() themselves.
    for (const auto& sink : sinks) sink(e);
}

std::vector<Entry> Snapshot() {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    return {s.ring.begin(), s.ring.end()};
}

uint64_t LastSeq() {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    return s.seq;
}

}  // namespace rm::log
