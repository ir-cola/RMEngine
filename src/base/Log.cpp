#include "base/Log.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace rm::log {

namespace {

constexpr size_t kRingMax = 2000;
constexpr size_t kQueueMax = 2000;

struct SinkRec {
    int id = 0;
    Sink fn;
    Delivery delivery = Delivery::AnyThread;
    bool removed = false;
    std::vector<std::thread::id> callers;  // 今呼んでいるスレッド (呼び出し 1 回につき 1 つ)
};

struct State {
    std::mutex mtx;
    std::condition_variable idle;  // シンクの呼び出しが 1 つ終わった
    std::deque<Entry> ring;
    std::vector<std::shared_ptr<SinkRec>> sinks;
    std::deque<std::pair<std::shared_ptr<SinkRec>, Entry>> queue;  // Queued のシンク用
    std::function<void()> wake;
    int nextSinkId = 1;
    uint64_t seq = 0;
    Counts counts;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

State& S() {
    static State s;
    return s;
}

// `rec` を `e` で呼ぶ (外されていなければ)。RemoveSink (別のスレッドから) は呼び出しが終わるまで待つ。
void Call(State& s, const std::shared_ptr<SinkRec>& rec, const Entry& e) {
    const auto me = std::this_thread::get_id();
    {
        std::lock_guard lock(s.mtx);
        if (rec->removed) return;
        rec->callers.push_back(me);
    }
    // ロックの外で: シンクが自分でログを書いたり Snapshot() を読んだりしてもよい。
    rec->fn(e);
    {
        std::lock_guard lock(s.mtx);
        rec->callers.erase(std::find(rec->callers.begin(), rec->callers.end(), me));
    }
    s.idle.notify_all();
}

// `now` を、地方時の 1970-01-01 0:00 からのミリ秒に。
int64_t LocalClockMs(std::chrono::system_clock::time_point now) {
    using namespace std::chrono;
    const int64_t ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
    const int64_t sec = ms >= 0 ? ms / 1000 : (ms - 999) / 1000;
    const std::time_t t = static_cast<std::time_t>(sec);
    std::tm tm{};
    if (localtime_s(&tm, &t) != 0) return ms;  // (地方時が分からない: UTC のまま)
    const sys_days d = year{tm.tm_year + 1900} / month{static_cast<unsigned>(tm.tm_mon + 1)} / day{static_cast<unsigned>(tm.tm_mday)};
    const int64_t localSec = duration_cast<seconds>(d.time_since_epoch()).count() + tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return localSec * 1000 + (ms - sec * 1000);
}

}  // namespace

int AddSink(Sink sink, Delivery delivery) {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    auto rec = std::make_shared<SinkRec>();
    rec->id = s.nextSinkId++;
    rec->fn = std::move(sink);
    rec->delivery = delivery;
    s.sinks.push_back(rec);
    return rec->id;
}

void RemoveSink(int id) {
    auto& s = S();
    std::unique_lock lock(s.mtx);
    const auto it = std::find_if(s.sinks.begin(), s.sinks.end(), [id](const auto& r) { return r->id == id; });
    if (it == s.sinks.end()) return;
    const std::shared_ptr<SinkRec> rec = *it;
    s.sinks.erase(it);
    rec->removed = true;
    std::erase_if(s.queue, [&](const auto& q) { return q.first == rec; });
    // 他のスレッドでの呼び出しを待つ (このスレッドでの呼び出し - シンクが自分を外す場合 - は、
    // こちらが戻るまで終われないし、呼んだ側の問題でもある)。
    const auto me = std::this_thread::get_id();
    s.idle.wait(lock, [&] { return std::all_of(rec->callers.begin(), rec->callers.end(), [&](const auto& t) { return t == me; }); });
}

void SetWake(std::function<void()> wake) {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    s.wake = std::move(wake);
}

void Write(Level level, std::string text) {
    auto& s = S();
    Entry e;
    std::vector<std::shared_ptr<SinkRec>> now;
    std::function<void()> wake;
    e.clockMs = LocalClockMs(std::chrono::system_clock::now());  // (ロックの外で: 地方時を調べるのは少し重い)
    {
        std::lock_guard lock(s.mtx);
        e.level = level;
        e.time = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count();
        e.seq = ++s.seq;
        (level == Level::Error ? s.counts.error : level == Level::Warn ? s.counts.warn : s.counts.info)++;
        e.text = std::move(text);
        s.ring.push_back(e);
        if (s.ring.size() > kRingMax) s.ring.pop_front();
        const bool wasEmpty = s.queue.empty();
        for (const auto& rec : s.sinks) {
            if (rec->delivery == Delivery::AnyThread) {
                now.push_back(rec);
            } else {
                s.queue.emplace_back(rec, e);
                if (s.queue.size() > kQueueMax) s.queue.pop_front();
            }
        }
        if (wasEmpty && !s.queue.empty()) wake = s.wake;
    }
    for (const auto& rec : now) Call(s, rec, e);
    if (wake) wake();
}

void DeliverQueued() {
    auto& s = S();
    std::deque<std::pair<std::shared_ptr<SinkRec>, Entry>> taken;
    {
        std::lock_guard lock(s.mtx);
        taken.swap(s.queue);
    }
    for (const auto& [rec, e] : taken) Call(s, rec, e);
}

std::vector<Entry> Snapshot() {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    return {s.ring.begin(), s.ring.end()};
}

Counts Counted() {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    return s.counts;
}

void ResetCounted() {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    s.counts = {};
}

uint64_t LastSeq() {
    auto& s = S();
    std::lock_guard lock(s.mtx);
    return s.seq;
}

}  // namespace rm::log
