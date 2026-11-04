#pragma once
// Process-wide log (pure). Platform code adds sinks (debugger output, log file);
// the editor console reads Snapshot().

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

int AddSink(Sink sink);
void RemoveSink(int id);

void Write(Level level, std::string text);

// The newest entries (at most 2000 are kept).
std::vector<Entry> Snapshot();
uint64_t LastSeq();

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
