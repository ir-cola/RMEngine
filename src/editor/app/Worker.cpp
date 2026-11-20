#include "editor/app/Worker.h"

#include "platform/Win.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace rm::editor {

namespace {

// 決して壊さない: 手放した (detach した) 作業スレッドが、アプリの終了中にまだ post するかもしれない。
struct Queue {
    std::mutex mutex;
    std::vector<std::function<void()>> posted;
};
Queue& TheQueue() {
    static Queue* q = new Queue;
    return *q;
}
std::atomic<DWORD> g_uiThread{0};

}  // namespace

void SetUiThread() { g_uiThread = GetCurrentThreadId(); }

void PostToUi(std::function<void()> fn) {
    {
        Queue& q = TheQueue();
        std::lock_guard lock(q.mutex);
        q.posted.push_back(std::move(fn));
    }
    // App のループの GetMessage を起こす (そのあと RunPostedToUi が走る)。モーダルなループ
    // (ウィンドウのドラッグ) はこのメッセージを落とすかもしれない。作業は次のメッセージで走る。
    if (const DWORD ui = g_uiThread.load()) PostThreadMessageW(ui, WM_NULL, 0, 0);
}

UiWake::UiWake(std::function<void()> onUi) : onUi_(std::make_shared<std::function<void()>>(std::move(onUi))) {}

std::function<void()> UiWake::Signal() const {
    return [weak = std::weak_ptr<std::function<void()>>(onUi_)] {
        PostToUi([weak] {
            if (auto fn = weak.lock()) (*fn)();  // (UI スレッドで: なくなっているのは、その前に壊された場合だけ)
        });
    };
}

void RunPostedToUi() {
    std::vector<std::function<void()>> list;
    {
        Queue& q = TheQueue();
        std::lock_guard lock(q.mutex);
        list.swap(q.posted);
    }
    for (auto& fn : list) fn();
}

// Worker と、それが post した物すべてで共有する: `run` は、post がまだ有効な Run。UI スレッドだけが
// 読み書きする (post は走るときに、UI スレッドで確かめる) ので、ロックはない。
struct Worker::State {
    uint64_t run = 0;
    bool busy = false;
};

Worker::Worker() : state_(std::make_shared<State>()) {}

Worker::~Worker() {
    Stop();
    // 待たない: 届かないネットワークドライブの読み込みは何秒もかかることがあり、画面を閉じるのが
    // それを待ってはいけない。作業は自分のコピーだけを持ち (画面は持たない)、post した物は捨てられる。
    if (thread_.joinable()) thread_.detach();
}

void Worker::Stop() {
    ++state_->run;  // 古い作業がまだ post する物は捨てられる
    state_->busy = false;
    if (thread_.joinable()) thread_.request_stop();
}

void Worker::Run(Work work) {
    Stop();
    if (thread_.joinable()) {
        // 前の作業は止めるよう頼んである: 自分で終わらせる (捨てられた run に post するだけ)。
        thread_.detach();
    }
    const uint64_t run = ++state_->run;
    state_->busy = true;
    std::weak_ptr<State> weak = state_;
    Post post = [weak, run](std::function<void()> fn) {
        PostToUi([weak, run, fn = std::move(fn)] {
            const auto st = weak.lock();
            if (!st || st->run != run) return;  // もっと新しい Run、Stop、または Worker がなくなった
            fn();
        });
    };
    Post finished = [weak, run](std::function<void()>) {
        PostToUi([weak, run] {
            const auto st = weak.lock();
            if (st && st->run == run) st->busy = false;
        });
    };
    thread_ = std::jthread([work = std::move(work), post, finished](std::stop_token stop) {
        work(stop, post);
        finished({});
    });
}

bool Worker::Busy() const { return state_->busy; }

}  // namespace rm::editor
