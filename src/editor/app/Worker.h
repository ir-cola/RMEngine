#pragma once
// UI スレッドの外でするディスクの重い作業。ディスクを待つかもしれない物 (届かないネットワーク
// ドライブのプロジェクト、エンジンの全ファイルのハッシュ) は、ウィンドウを描く間は走らない。
// 専用のスレッドで走り、結果を UI スレッドに返す。結果はそこで、フレームとフレームの間に
// 当てる (App のメッセージループが走らせる - フレームの中では決して走らせない)。
//
//   worker_.Run([files](std::stop_token stop, const Worker::Post& post) {
//       for (...) {
//           if (stop.stop_requested()) return;
//           Result r = ReadSlowly(...);                 // 作業スレッドで: UI もアプリの状態も触らない
//           post([this, r] { rows_[...] = r; });         // あとで UI スレッドで
//       }
//   });
//
// 新しい Run (または Stop、Worker の破棄) は、前の作業に止まるよう頼み、それがまだ post する物を
// 捨てる。結果が、もうない画面や、あらためて頼まれたデータに落ちることはない。
// 作業は、誰にも待たれずに走り続ける (join せず手放す)。だから渡された物 (コピー) だけを使うこと。
// 画面とアプリは `post` の中でだけ触る。

#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace rm::editor {

// fn をもうすぐ UI スレッドで走らせる (どのスレッドからでも)。App のメッセージループが
// RunPostedToUi を呼ぶ。
void PostToUi(std::function<void()> fn);
void RunPostedToUi();      // UI スレッドだけ
void SetUiThread();        // RunPostedToUi を走らせるスレッド (App::Run で 1 度)

// どのスレッドからも呼べる起こし (フォルダー監視の合図): もうすぐ UI スレッドで `onUi` を走らせる。
// ただしその時までにこの UiWake がなくなっていたら (その間に画面が閉じた)、何も起きない。
class UiWake {
public:
    explicit UiWake(std::function<void()> onUi);
    UiWake(const UiWake&) = delete;
    UiWake& operator=(const UiWake&) = delete;
    std::function<void()> Signal() const;  // (コピーはどのスレッドから呼んでも安全)

private:
    std::shared_ptr<std::function<void()>> onUi_;
};

class Worker {
public:
    using Post = std::function<void(std::function<void()>)>;
    using Work = std::function<void(std::stop_token, const Post&)>;

    Worker();
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    ~Worker();

    void Run(Work work);
    void Stop();          // 作業に止まるよう頼む。これ以降それが post する物は捨てられる
    bool Busy() const;    // 最後の Run がまだ終わっていない (UI スレッド)

private:
    struct State;
    std::shared_ptr<State> state_;
    std::jthread thread_;
};

}  // namespace rm::editor
