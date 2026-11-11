#include "editor/app/Worker.h"

#include "platform/Win.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace rm::editor {

namespace {

// Never destroyed: a work thread that was let go (detached) may still post while the app exits.
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
    // Wakes GetMessage in App's loop (which then runs RunPostedToUi). A modal loop (a window being
    // dragged) may drop this message: the work still runs with the next message.
    if (const DWORD ui = g_uiThread.load()) PostThreadMessageW(ui, WM_NULL, 0, 0);
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

// Shared by the Worker and everything it posted: `run` is the Run whose posts still count. Only the
// UI thread reads or writes it (posts are checked when they run, on the UI thread), so no lock.
struct Worker::State {
    uint64_t run = 0;
    bool busy = false;
};

Worker::Worker() : state_(std::make_shared<State>()) {}

Worker::~Worker() {
    Stop();
    // Not waited for: a read of an unreachable network drive can take seconds, and closing a screen
    // must not. The work holds only its own copies (never the screen), and what it posts is dropped.
    if (thread_.joinable()) thread_.detach();
}

void Worker::Stop() {
    ++state_->run;  // what the old work still posts is dropped
    state_->busy = false;
    if (thread_.joinable()) thread_.request_stop();
}

void Worker::Run(Work work) {
    Stop();
    if (thread_.joinable()) {
        // The previous work was asked to stop: let it end on its own (it only posts into a dropped run).
        thread_.detach();
    }
    const uint64_t run = ++state_->run;
    state_->busy = true;
    std::weak_ptr<State> weak = state_;
    Post post = [weak, run](std::function<void()> fn) {
        PostToUi([weak, run, fn = std::move(fn)] {
            const auto st = weak.lock();
            if (!st || st->run != run) return;  // a newer Run, Stop, or the Worker is gone
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
