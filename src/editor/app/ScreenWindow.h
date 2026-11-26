#pragma once
// 1 つの画面 (Hub かエディター) を出す、1 つのトップレベルウィンドウ。
// 入力を集め、イミディエイトモードの UI を動かし、表示する - 何か変わったときだけ。
//
// フレームの流れ:  入力メッセージ -> Invalidate -> WM_PAINT -> RunFrame (OnFrame + 描画 + Present)
//                   サイズ変更    -> WM_SIZE の中で直接 RunFrame (ドラッグ中に絵が固まらない)
// ユーザーが何もしない間は何も動かない (CPU 0%)。フォーカスのある欄のカーソル点滅タイマーと、
// 動くテーマの絵 (Context::RequestFrameAfter: 約 30 fps のタイマー。最小化中・ほかのアプリが前にいる間は止める) は別。

#include "platform/Gfx.h"
#include "platform/Window.h"
#include "ui/Context.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace rm::editor {

class App;

class ScreenWindow : public platform::WindowListener {
public:
    explicit ScreenWindow(App& app) : app_(app) {}
    ~ScreenWindow() override;
    ScreenWindow(const ScreenWindow&) = delete;
    ScreenWindow& operator=(const ScreenWindow&) = delete;

    // スワップチェーンを放してウィンドウを壊す。2 回呼んでも安全。
    virtual void Close();

    HWND Hwnd() const { return window_.Handle(); }
    void Invalidate();
    // あとでメッセージループから fn を走らせる (描画の外で)。モーダルな OS のダイアログを開く
    // 処理には必ずこれを使う。そういうダイアログは自分でメッセージループを回すので、
    // フレームに入り直してしまう。
    void Defer(std::function<void()> fn);
    void SetTitle(std::wstring_view title) { window_.SetTitle(title); }
    float Scale() const { return window_.Scale(); }
    const ui::Context& UiContext() const { return ctx_; }
    // 色のテーマか言語が変わった: タイトルバーもそれに合わせ、全部をもう一度描く。
    virtual void OnLookChanged();
    // GPU を作り直せないとき: 画面の作業を保存する (あれば。nullopt: 保存する物はない。
    // true / false: 保存できた / できなかった)。
    virtual std::optional<bool> SaveWork() { return std::nullopt; }

protected:
    bool CreateScreenWindow(const platform::WindowDesc& desc, int showCmd);

    virtual void OnCreated() {}
    virtual void OnFrame(ui::Context& c, float width, float height) = 0;
    virtual void OnCloseRequested() = 0;  // タイトルバーの X / Alt+F4
    // Windows がシャットダウン / サインアウトする (WM_QUERYENDSESSION)。false = 今はだめ (未保存の
    // 変更: 画面がユーザーに尋ねる。理由は Windows の「アプリがシャットダウンを妨げています」画面に出る)。
    virtual bool OnQueryEndSession() { return true; }
    // どのみち終わる (WM_ENDSESSION): もう何も尋ねられない - 残せる物を、急いで残す。
    virtual void OnEndSession() {}
    // 画面のウィンドウ自身が扱わないメッセージ (WM_MOVING など)。DefWindowProc はそのあと走る。
    // 画面が SetTimer(Hwnd(), id, ...) で設定したタイマー (id は 100 から。1 はカーソルの点滅)。
    virtual void OnTimer(UINT_PTR id) { (void)id; }
    // GPU のデバイスが作り直された (失われていた): 画面が持つ Direct2D の物 (ビットマップなど) は
    // ここで作り直す。スワップチェーンとペインターはもう新しい。
    virtual void OnDeviceChanged() {}
    // 画面のウィンドウが見る前の、すべてのメッセージ: ここで答えるなら true (と `result`)
    // (形を変えたウィンドウの WM_NCHITTEST / WM_NCCALCSIZE)。
    virtual bool OnWindowMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
        (void)msg;
        (void)wp;
        (void)lp;
        (void)result;
        return false;
    }
    virtual void OnOtherMessage(UINT msg, WPARAM wp, LPARAM lp) {
        (void)msg;
        (void)wp;
        (void)lp;
    }

    App& app_;
    platform::Window window_;
    platform::SwapTarget target_;
    ui::Painter painter_;
    ui::Context ctx_;
    ui::Input input_;       // メッセージが運んでくる物を、次のフレームが取るまで集めておく
    ui::Input frameInput_;  // 描いているフレームが見る物 (開始時に input_ から取る)

private:
    LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& handled) override;
    void RunFrame();
    void OnInput();
    void MouseButton(int button, LPARAM lp, bool down, bool doubleClick);
    void UpdateMouse(LPARAM lp);
    void MouseMoved(LPARAM lp);
    void UpdateMouseLock();
    void UpdateModifiers();
    void UpdateIme();
    void RunDeferred();
    void ScheduleTick(double seconds);  // Context::RequestFrameAfter (0: 止める)
    bool EnsureDevice();  // GPU が使え、このウィンドウの描画が今のデバイスの上にできている

    bool alive_ = false;
    bool inFrame_ = false;
    bool trackingMouse_ = false;
    bool imeOn_ = true;
    bool caretTimer_ = false;
    bool tickTimer_ = false;
    bool mouseLocked_ = false;      // Context::LockMouse: 隠して、固定位置に留める
    float anchorX_ = 0, anchorY_ = 0;  // クライアント領域の DIP
    int chainedFrames_ = 0;
    ui::Cursor cursor_ = ui::Cursor::Arrow;
    std::vector<std::function<void()>> deferred_;
};

}  // namespace rm::editor
