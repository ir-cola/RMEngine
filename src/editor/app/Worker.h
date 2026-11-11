#pragma once
// Slow disk work off the UI thread. Nothing that may wait on a disk (a project on an unreachable
// network drive, hashing every file of the engine) runs while a window is drawing: it runs on a
// thread of its own and hands its results back to the UI thread, where they are applied between
// frames (App's message loop runs them - never inside a frame).
//
//   worker_.Run([files](std::stop_token stop, const Worker::Post& post) {
//       for (...) {
//           if (stop.stop_requested()) return;
//           Result r = ReadSlowly(...);                 // on the worker thread: no UI, no app state
//           post([this, r] { rows_[...] = r; });         // on the UI thread, later
//       }
//   });
//
// A new Run (or Stop, or destroying the Worker) asks the previous work to stop and drops whatever
// it still posts: a result never lands on a screen that is gone or on data that was asked for again.
// The work runs on with no one waiting for it (it is let go, not joined), so it must use only what it
// was given (copies) - the screen and the app are touched only inside `post`.

#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace rm::editor {

// Runs fn on the UI thread soon (from any thread). App's message loop calls RunPostedToUi.
void PostToUi(std::function<void()> fn);
void RunPostedToUi();      // UI thread only
void SetUiThread();        // the thread that runs RunPostedToUi (App::Run, once)

class Worker {
public:
    using Post = std::function<void(std::function<void()>)>;
    using Work = std::function<void(std::stop_token, const Post&)>;

    Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker();

    void Run(Work work);
    void Stop();          // asks the work to stop; what it posts from now on is dropped
    bool Busy() const;    // the last Run has not finished (UI thread)

private:
    struct State;
    std::shared_ptr<State> state_;
    std::jthread thread_;
};

}  // namespace rm::editor
