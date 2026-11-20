// Worker (editor/app/Worker) のテスト: 結果は順番どおりに UI スレッドへ戻る。止めた・置き換えた・
// 壊した Worker は、仕事がまだ送ってくるものを捨てる。本物のスレッドを使う。

#include "Test.h"

#include "editor/app/Worker.h"
#include "platform/Shell.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace rm::editor;

namespace {

// `done` が言うまで (最長 3 秒)、送られたものを実行する。
template <class F>
bool PumpUntil(F done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end) {
        RunPostedToUi();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

}  // namespace

RM_TEST(Worker_ResultsComeBackInOrder) {
    SetUiThread();
    std::vector<int> got;
    Worker w;
    w.Run([&got](std::stop_token, const Worker::Post& post) {
        for (int i = 0; i < 5; ++i) post([&got, i] { got.push_back(i); });
    });
    CHECK(PumpUntil([&] { return !w.Busy(); }));
    CHECK(got == std::vector<int>({0, 1, 2, 3, 4}));
}

RM_TEST(Worker_StoppedOrReplacedWorkIsDropped) {
    SetUiThread();
    std::vector<int> got;
    auto release = std::make_shared<std::atomic<bool>>(false);
    auto sawStop = std::make_shared<std::atomic<int>>(0);  // 1 = 止めるよう頼まれた、2 = 頼まれていない
    Worker w;
    w.Run([&got, release, sawStop](std::stop_token stop, const Worker::Post& post) {
        while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sawStop->store(stop.stop_requested() ? 1 : 2);  // 2 回目の Run で止めるよう頼まれた
        post([&got] { got.push_back(1); });             // 遅すぎ: 捨てられる
    });
    w.Run([&got](std::stop_token, const Worker::Post& post) { post([&got] { got.push_back(2); }); });
    release->store(true);
    CHECK(PumpUntil([&] { return !w.Busy() && sawStop->load() != 0; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 最初の仕事の遅れた結果がキューに入る
    RunPostedToUi();
    CHECK(sawStop->load() == 1);
    CHECK(got == std::vector<int>({2}));
}

RM_TEST(Worker_GoneWorkerDropsPosts) {
    SetUiThread();
    auto ran = std::make_shared<std::atomic<int>>(0);
    auto release = std::make_shared<std::atomic<bool>>(false);
    {
        Worker w;
        w.Run([ran, release](std::stop_token, const Worker::Post& post) {
            while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            post([ran] { ++*ran; });
        });
    }  // 仕事の途中で壊した: 待たない
    release->store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RunPostedToUi();
    CHECK(ran->load() == 0);
}

// フォルダの監視は、ファイルが来たとき (誰も尋ねなくても) UI スレッドを起こし、1 回だけ見え、
// 監視していたフォルダが消されてまた作られたときはそれを知らせる (持ち主はまた監視を始める)。
RM_TEST(FolderWatch_WakesAndTellsWhenTheFolderIsGone) {
    namespace fs = std::filesystem;
    SetUiThread();
    const fs::path dir = fs::temp_directory_path() / L"rm_folder_watch_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    auto woken = std::make_shared<std::atomic<int>>(0);
    {
        UiWake wake([woken] { ++*woken; });
        rm::platform::FolderWatch watch;
        CHECK(watch.Start(dir, true, wake.Signal()));
        CHECK(!watch.Lost());
        std::ofstream(dir / L"a.txt") << "a";
        CHECK(PumpUntil([&] { return woken->load() > 0; }));
        CHECK(watch.Changed());
        while (watch.Changed()) {
        }
        // 消してまた作った: 監視は古いフォルダを追っていた。
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        CHECK(watch.Lost());
        CHECK(watch.Start(dir, true, wake.Signal()));
        CHECK(!watch.Lost());
        // 変更が入っている最中に壊した: そのあとにコールバックは呼ばれない。
        for (int i = 0; i < 20; ++i) std::ofstream(dir / (L"b" + std::to_wstring(i) + L".txt")) << i;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RunPostedToUi();  // (それが消える前に送られたものは捨てられる: UiWake はもうない)
    fs::remove_all(dir, ec);
}
