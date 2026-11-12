// Tests for the Worker (editor/app/Worker): results come back to the UI thread in order; a stopped,
// replaced or destroyed Worker drops what its work still posts. Uses real threads.

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

// Runs what was posted until `done` says so (at most 3 s).
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
    auto sawStop = std::make_shared<std::atomic<int>>(0);  // 1 = asked to stop, 2 = not
    Worker w;
    w.Run([&got, release, sawStop](std::stop_token stop, const Worker::Post& post) {
        while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sawStop->store(stop.stop_requested() ? 1 : 2);  // asked to stop by the second Run
        post([&got] { got.push_back(1); });             // too late: dropped
    });
    w.Run([&got](std::stop_token, const Worker::Post& post) { post([&got] { got.push_back(2); }); });
    release->store(true);
    CHECK(PumpUntil([&] { return !w.Busy() && sawStop->load() != 0; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // the first work's late result is queued
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
    }  // destroyed while the work runs: not waited for
    release->store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RunPostedToUi();
    CHECK(ran->load() == 0);
}

// A folder watch wakes the UI thread when a file comes (no one asking), is seen once, and tells when
// the folder it watched was deleted and made again (its owner then starts it again).
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
        // Deleted and made again: the watch followed the old folder.
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        CHECK(watch.Lost());
        CHECK(watch.Start(dir, true, wake.Signal()));
        CHECK(!watch.Lost());
        // Destroyed with changes coming in: no callback outlives it.
        for (int i = 0; i < 20; ++i) std::ofstream(dir / (L"b" + std::to_wstring(i) + L".txt")) << i;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    RunPostedToUi();  // (what was posted before it went is dropped: the UiWake is gone)
    fs::remove_all(dir, ec);
}
