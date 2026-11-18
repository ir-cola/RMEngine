#pragma once
// Process-wide log (pure). Platform code adds sinks (debugger output, log file);
// the editor console reads Snapshot().
//
// Any thread may write (a worker reading a model, making thumbnails ...). Sinks come in two kinds:
//   Delivery::AnyThread  called on the writer's thread, right away - the sink must be thread-safe
//                        (the debugger output, the log file with its own lock)
//   Delivery::Queued     the entry waits in a queue until the owner's thread calls DeliverQueued()
//                        (the UI: it touches windows that only the UI thread may touch). SetWake
//                        is called when the queue gets something, so a waiting thread wakes up.
// RemoveSink returns only when the sink is no longer being called on another thread, and it is never
// called afterwards (an object may remove its sink in its destructor). Tested in tests/TestBase.cpp.

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <vector>

namespace rm::log {

enum class Level : uint8_t { Info, Warn, Error };

struct Entry {
    Level level = Level::Info;
    double time = 0.0;  // seconds since the first log call
    uint64_t seq = 0;   // 1, 2, 3 ...
    std::string text;   // UTF-8
};

using Sink = std::function<void(const Entry&)>;

enum class Delivery : uint8_t { AnyThread, Queued };

int AddSink(Sink sink, Delivery delivery = Delivery::AnyThread);
void RemoveSink(int id);

// Queued sinks: the entries waiting for them, given now on this thread (in the order written). At most
// 2000 wait; older ones are dropped (each sink still gets the newest).
void DeliverQueued();
// Called (on the writer's thread, outside the log's lock) when an entry starts waiting in the queue
// that was empty: wakes the thread that calls DeliverQueued. Empty: none.
void SetWake(std::function<void()> wake);

void Write(Level level, std::string text);

// The newest entries (at most 2000 are kept).
std::vector<Entry> Snapshot();
uint64_t LastSeq();
// How many messages of each level were written since the program started (the debug panel).
struct Counts {
    uint64_t info = 0, warn = 0, error = 0;
};
Counts Counted();

template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace rm::log
