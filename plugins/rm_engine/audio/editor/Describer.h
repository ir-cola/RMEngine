#pragma once
// 音 - アセット パネルが音について見せる 1 行 ("長さ 0:12.5 · 44100 Hz ステレオ · Ogg Vorbis")。
// 専用のスレッドで読む: ファイルと更新時刻ごとに 1 回 (パネルは毎フレーム尋ねる)。
//
//   Line(file, known)   分かっていればその 1 行。分かっていなければ "長さを調べています…" を返し、
//                       ファイルを読む -
//                       1 行ができたら `known` を (describer のスレッドで) 呼ぶ。パネルがウィンドウを
//                       描き直せるようにするため (その前に描いたフレームは、マウスが動くまで
//                       "長さを調べています…" のままになる)

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
    // ファイルのバイト列から作る 1 行 (どのスレッドでも呼べる。分かっているとき Line が返すもの)。
    static std::wstring Read(const std::filesystem::path& file);

private:
    struct Known {
        std::filesystem::file_time_type time{};
        bool ready = false;
        std::wstring line;
        std::vector<std::function<void()>> waiting;  // できたら知らせる先
    };
    std::mutex m_;
    std::condition_variable_any cv_;
    std::unordered_map<std::wstring, Known> known_;
    std::deque<std::filesystem::path> asked_;
    std::jthread thread_;  // (最後に置く: 最初に止めて join される)
};

}  // namespace rm::audio
