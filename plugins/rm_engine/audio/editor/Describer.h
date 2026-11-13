#pragma once
// 音 - the line the アセット panel shows about a sound ("長さ 0:12.5 · 44100 Hz ステレオ · Ogg Vorbis"),
// read on a thread of its own: once per file and time it was changed (the panel asks every frame).
//
//   Line(file, known)   the line when it is known; else "長さを調べています…" and the file is read -
//                       `known` is called (on the describer's thread) once the line is there, so the
//                       panel can have the windows drawn again (a frame drawn before that would show
//                       "長さを調べています…" until the mouse moved)

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rm::audio {

class Describer {
public:
    Describer();
    ~Describer();
    Describer(const Describer&) = delete;
    Describer& operator=(const Describer&) = delete;

    std::wstring Line(const std::filesystem::path& file, const std::function<void()>& known);
    // The line of the file's bytes (on any thread; what Line shows when it is known).
    static std::wstring Read(const std::filesystem::path& file);

private:
    struct Known {
        std::filesystem::file_time_type time{};
        bool ready = false;
        std::wstring line;
        std::vector<std::function<void()>> waiting;  // told when it is ready
    };
    std::mutex m_;
    std::condition_variable_any cv_;
    std::unordered_map<std::wstring, Known> known_;
    std::deque<std::filesystem::path> asked_;
    std::jthread thread_;  // (last: stopped and joined first)
};

}  // namespace rm::audio
