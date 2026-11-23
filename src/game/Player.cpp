#include "game/Player.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/pak/Pak.h"
#include "platform/Crypto.h"
#include "platform/Shell.h"
#include "platform/Window.h"

#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>
#include <algorithm>

#include <imm.h>
#pragma comment(lib, "imm32.lib")  // (ゲームの入力欄の IME)

namespace rm::game {

namespace {

// ゲームの入力欄が使うキー (ほかのキーは集めない: ゲームはキーを GetAsyncKeyState で読む)。
ui::Key EditKey(WPARAM vk) {
    switch (vk) {
        case VK_LEFT: return ui::Key::Left;
        case VK_RIGHT: return ui::Key::Right;
        case VK_HOME: return ui::Key::Home;
        case VK_END: return ui::Key::End;
        case VK_BACK: return ui::Key::Backspace;
        case VK_DELETE: return ui::Key::Delete;
        case VK_RETURN: return ui::Key::Enter;
        case VK_ESCAPE: return ui::Key::Escape;
        case 'A': return ui::Key::A;
        case 'C': return ui::Key::C;
        case 'V': return ui::Key::V;
        case 'X': return ui::Key::X;
        default: return ui::Key::None;  // VK_PROCESSKEY (IME が食べたキー) も
    }
}

std::wstring ReadIme(HIMC imc, DWORD type) {
    const LONG bytes = ImmGetCompositionStringW(imc, type, nullptr, 0);
    if (bytes <= 0) return {};
    std::wstring s(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    ImmGetCompositionStringW(imc, type, s.data(), static_cast<DWORD>(bytes));
    return s;
}

// exe の隣の game.log (そのフォルダに書けるとき。Program Files の下では書けない)。
void InstallLog(const std::filesystem::path& folder) {
    static std::ofstream file;
    static std::mutex mtx;
    file.open(folder / kLogName, std::ios::binary | std::ios::trunc);
    if (!file) return;
    log::AddSink([](const log::Entry& e) {
        std::lock_guard lock(mtx);
        const char* tag = e.level == log::Level::Error ? "ERROR" : e.level == log::Level::Warn ? "WARN " : "INFO ";
        file << std::format("{:9.3f} {} {}\n", e.time, tag, e.text);
        file.flush();
    });
}

class Frame final : public Host, public platform::WindowListener {
public:
    Frame(const RmGameImage& image, bool smoke) : image_(image), smoke_(smoke), folder_(platform::ExeDirectory()) {}

    int Run() {
        InstallLog(folder_);
        log::Info("ゲームを始めます: {} (書き出しの番号 {})", image_.title ? image_.title : "", image_.build);
        if (!OpenPak()) return Fail(error_);
        for (unsigned long long i = 0; i < image_.count; ++i) {
            const RmLinkedPlugin& p = image_.plugins[i];
            if (!p.author || !p.name) continue;
            log::Info("プラグイン: {} / {}", p.author, p.name);
            if (!p.registerRuntime) continue;
            registry_.BeginPlugin({p.author, p.name});
            p.registerRuntime(registry_);
            registry_.EndPlugin();
        }
        const auto makers = registry_.Contributions<GameInfo>(kGamePoint);
        if (makers.empty()) return Fail("このゲームにはゲームを作るプラグイン (ゲーム制作) が入っていません");
        if (!gfx_.Init()) return Fail("GPU (Direct3D 11) を使えません");
        game_ = makers.front().item->create(*this);
        std::string err;
        if (!game_ || !game_->Load(&err)) return Fail(err.empty() ? std::string("ゲームを読めませんでした") : err);

        display_ = game_->GetDisplay();
        platform::WindowDesc d;
        d.title = Utf8ToWide(title_.empty() ? std::string(image_.title ? image_.title : "Game") : title_);
        d.widthDip = std::clamp(display_.width, 320, 7680);
        d.heightDip = std::clamp(display_.height, 240, 4320);
        d.minWidthDip = 320;
        d.minHeightDip = 240;
        d.captionRgb = 0x000000;
        if (!window_.Create(d, this)) return Fail("ウィンドウを作れませんでした");
        ImmAssociateContextEx(window_.Handle(), nullptr, 0);  // IME は入力欄に打つ間だけ (SetTextInput)
        if (display_.mode != Game::Display::Mode::Windowed) CoverMonitor();
        if (!target_.Create(gfx_, window_.Handle(), window_.Dpi())) return Fail("描く場所 (スワップチェーン) を作れませんでした");
        ShowWindow(window_.Handle(), smoke_ ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);
        started_ = true;

        auto last = std::chrono::steady_clock::now();
        int frames = 0;
        while (!quit_) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) quit_ = true;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (quit_) break;
            // 最小化中: 何も見えないので、描きも鳴らしもしない。メッセージを待つ (ループでコアを
            // 使わない)。離れていた時間は数えない。
            if (IsIconic(window_.Handle()) && !smoke_) {
                WaitMessage();
                last = std::chrono::steady_clock::now();
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            // 最大でも 4 分の 1 秒まで (エディターの Play と同じ): 長くかかったフレーム (ウィンドウを
            // ドラッグした、PC が忙しい) があっても、ものが飛ばない。
            const double dt = std::min(std::chrono::duration<double>(now - last).count(), 0.25);
            last = now;
            game_->Update(dt);
            if (!DrawFrame()) {
                if (!Recover()) return Fail("描けませんでした (GPU が失われ、作り直せませんでした)");
                continue;
            }
            // vsync なし: 1 秒に目標のフレーム数を超えない。
            if (!display_.vsync && display_.targetFps > 0) {
                const auto next = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / display_.targetFps));
                std::this_thread::sleep_until(next);
            }
            if (smoke_ && ++frames >= 5) break;
        }
        game_->Stop();
        log::Info("ゲームを終わりました{}", smoke_ ? " (--smoke: 5 フレーム描けました)" : "");
        window_.Destroy();
        return 0;
    }

    // --- Host -------------------------------------------------------------------------------------
    std::optional<std::string> Read(std::string_view path, std::string* error) const override { return pak_.Read(path, error); }
    const RuntimeRegistry& Registry() const override { return registry_; }
    platform::Gfx& Graphics() override { return gfx_; }
    std::filesystem::path Folder() const override { return folder_; }
    void Log(int level, std::string_view text) override {
        if (level >= 2) {
            log::Error("{}", text);
        } else if (level == 1) {
            log::Warn("{}", text);
        } else {
            log::Info("{}", text);
        }
    }
    void SetTitle(std::string_view title) override {
        title_ = std::string(title);
        if (window_.Handle()) window_.SetTitle(Utf8ToWide(title_));
    }
    PointerPresses TakePointerPresses() override { return std::exchange(presses_, PointerPresses{}); }
    ui::TypedText TakeTyped() override {
        ui::TypedText out = std::exchange(typed_, ui::TypedText{});
        typed_.imeComposition = out.imeComposition;  // (変換中の文字はフレームをまたいで残る)
        typed_.imeCursor = out.imeCursor;
        return out;
    }
    void SetTextInput(bool want, float x, float y, float w, float h) override {
        const HWND hwnd = window_.Handle();
        if (!hwnd) return;
        if (want != textWanted_) {
            textWanted_ = want;
            if (!want) {
                if (HIMC imc = ImmGetContext(hwnd)) {
                    ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                    ImmReleaseContext(hwnd, imc);
                }
                typed_ = {};
            }
            ImmAssociateContextEx(hwnd, nullptr, want ? IACE_DEFAULT : 0);
        }
        if (!want) return;
        // 候補の窓をその欄の下に (変換中の文字は欄の中にゲームが描く)。
        if (HIMC imc = ImmGetContext(hwnd)) {
            const float s = target_.Dpi() / 96.f;
            COMPOSITIONFORM cf{};
            cf.dwStyle = CFS_POINT;
            cf.ptCurrentPos = {static_cast<LONG>(x * s), static_cast<LONG>(y * s)};
            ImmSetCompositionWindow(imc, &cf);
            CANDIDATEFORM cand{};
            cand.dwIndex = 0;
            cand.dwStyle = CFS_EXCLUDE;
            cand.ptCurrentPos = {static_cast<LONG>(x * s), static_cast<LONG>((y + h) * s)};
            cand.rcArea = {static_cast<LONG>(x * s), static_cast<LONG>(y * s), static_cast<LONG>((x + w) * s), static_cast<LONG>((y + h) * s)};
            ImmSetCandidateWindow(imc, &cand);
            ImmReleaseContext(hwnd, imc);
        }
    }
    void Quit() override { quit_ = true; }
    // クライアント領域の上のマウス (DIP。Draw に渡される大きさ)。このウィンドウが手前にあるときだけ。
    std::optional<Vec2> Pointer() const override {
        const HWND hwnd = window_.Handle();
        if (!hwnd || GetForegroundWindow() != hwnd) return std::nullopt;
        POINT p{};
        RECT client{};
        if (!GetCursorPos(&p) || !ScreenToClient(hwnd, &p) || !GetClientRect(hwnd, &client)) return std::nullopt;
        if (p.x < client.left || p.y < client.top || p.x >= client.right || p.y >= client.bottom) return std::nullopt;
        const float scale = target_.Dpi() / 96.f;
        return Vec2{static_cast<float>(p.x) / scale, static_cast<float>(p.y) / scale};
    }

    // --- Window -----------------------------------------------------------------------------------
    LRESULT OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& handled) override {
        switch (msg) {
            case WM_SIZE:
                if (target_.Context()) {
                    target_.Resize(LOWORD(lp), HIWORD(lp));
                    // すぐに描き直す (枠をドラッグしている間、上のループは回らない)。
                    if (started_ && wp != SIZE_MINIMIZED && !drawing_) DrawFrame();
                }
                return 0;
            case WM_DPICHANGED:
                // 別の倍率のモニターに移った: そのモニターの DIP で描く (ウィンドウは、Windows の提案どおり、
                // Window から新しい大きさ - WM_SIZE - をもらう)。
                if (target_.Context()) target_.SetDpi(static_cast<float>(HIWORD(wp)));
                return 0;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                ++presses_.down;
                return 0;
            case WM_LBUTTONUP:
                ++presses_.up;
                return 0;
            // ゲームの入力欄 (打っている間だけ集める。そうでないときは何もしない: Windows のまま)。
            case WM_KEYDOWN:
                if (textWanted_) {
                    if (const ui::Key k = EditKey(wp); k != ui::Key::None) {
                        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                        typed_.keys.push_back({k, ctrl, shift, false});
                    }
                }
                return 0;
            case WM_CHAR:
                if (textWanted_) {
                    const wchar_t ch = static_cast<wchar_t>(wp);
                    if (ch >= 0x20 && ch != 0x7F) typed_.chars.push_back(ch);
                    handled = true;  // (Esc などの音を鳴らさない)
                }
                return 0;
            case WM_IME_SETCONTEXT:
                // 変換中の文字はゲームが欄の中に描く (Windows の窓は出さない)。
                handled = true;
                return DefWindowProcW(hwnd, msg, wp, lp & ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW));
            case WM_IME_COMPOSITION:
                if (HIMC imc = ImmGetContext(hwnd)) {
                    if (lp & GCS_RESULTSTR) typed_.imeResult += ReadIme(imc, GCS_RESULTSTR);
                    if (lp & GCS_COMPSTR) {
                        typed_.imeComposition = ReadIme(imc, GCS_COMPSTR);
                        typed_.imeCursor = (lp & GCS_CURSORPOS) ? LOWORD(ImmGetCompositionStringW(imc, GCS_CURSORPOS, nullptr, 0))
                                                                : static_cast<int>(typed_.imeComposition.size());
                    } else if (lp & GCS_RESULTSTR) {
                        typed_.imeComposition.clear();
                    }
                    ImmReleaseContext(hwnd, imc);
                }
                handled = true;  // (WM_IME_CHAR / WM_CHAR と重ならないように先へ渡さない)
                return 0;
            case WM_IME_ENDCOMPOSITION:
                typed_.imeComposition.clear();
                typed_.imeCursor = 0;
                handled = true;
                return 0;
            case WM_IME_CHAR:
                handled = true;
                return 0;
            case WM_CLOSE:
                quit_ = true;
                handled = true;
                return 0;
            case WM_DESTROY:
                quit_ = true;
                return 0;
        }
        return 0;
    }

private:
    bool OpenPak() {
        const std::filesystem::path file = folder_ / kPakName;
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(file, ec);
        if (ec) {
            error_ = std::format("アセットのファイル ({}) がありません。書き出したフォルダのまま起動してください", WideToUtf8(kPakName));
            return false;
        }
        pakFile_ = std::make_shared<std::ifstream>(file, std::ios::binary);
        if (!*pakFile_) {
            error_ = std::format("アセットのファイル ({}) を開けません", WideToUtf8(kPakName));
            return false;
        }
        auto in = pakFile_;
        auto mtx = std::make_shared<std::mutex>();
        return pak_.Open(
            [in, mtx](uint64_t at, size_t n, char* out) {
                std::lock_guard lock(*mtx);
                in->clear();
                in->seekg(static_cast<std::streamoff>(at));
                in->read(out, static_cast<std::streamsize>(n));
                return static_cast<size_t>(in->gcount()) == n;
            },
            size, image_.build, platform::PakCipher(image_.build), &error_);
    }

    // GPU が失われたときは false (Recover で作り直す)。
    bool DrawFrame() {
        ID2D1DeviceContext* dc = target_.Context();
        if (!dc) return true;
        drawing_ = true;
        dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->Clear(D2D1::ColorF(0, 0, 0, 1));
        const float scale = target_.Dpi() / 96.f;
        game_->Draw(dc, static_cast<float>(target_.Width()) / scale, static_cast<float>(target_.Height()) / scale);
        const HRESULT hr = dc->EndDraw();
        drawing_ = false;
        if (hr == D2DERR_RECREATE_TARGET) return false;
        if (FAILED(hr) && !toldDraw_) {
            log::Warn("描画が失敗しました (0x{:08X})。この後は書きません", static_cast<uint32_t>(hr));
            toldDraw_ = true;
        }
        if (target_.Present(display_.vsync ? 1u : 0u)) return true;
        return !gfx_.Lost();  // (Present が別の理由で失敗した: 次のフレームでもう一度やる)
    }

    // GPU が失われた (ドライバーの更新、GPU のリセット): デバイスとウィンドウの描画先を作り直す。
    // ゲームの描画側は Gfx::Generation が変わるのを見て、自分の分を作り直す。
    bool Recover() {
        log::Warn("GPU が失われたので作り直します ({:08X})", static_cast<uint32_t>(gfx_.LostReason()));
        target_.Reset();
        if (!gfx_.Recreate()) return false;
        return target_.Create(gfx_, window_.Handle(), window_.Dpi());
    }

    // フルスクリーン / 枠なし: 今いるモニターの全体を、枠のないウィンドウで覆う。
    void CoverMonitor() {
        const HWND hwnd = window_.Handle();
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP);
        const RECT& r = mi.rcMonitor;
        SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    int Fail(const std::string& why) {
        if (game_ && started_) game_->Stop();  // (スクリプトの EndPlay など)
        started_ = false;
        log::Error("{}", why);
        if (!smoke_) MessageBoxW(window_.Handle(), Utf8ToWide(why).c_str(), Utf8ToWide(image_.title ? image_.title : "Game").c_str(), MB_ICONERROR);
        return 1;
    }

    const RmGameImage& image_;
    bool smoke_ = false;
    std::filesystem::path folder_;
    std::string error_, title_;
    RuntimeRegistry registry_;
    std::shared_ptr<std::ifstream> pakFile_;
    pak::Reader pak_;
    platform::Gfx gfx_;
    platform::Window window_;
    platform::SwapTarget target_;
    std::unique_ptr<Game> game_;
    Game::Display display_;
    bool quit_ = false;
    PointerPresses presses_;  // 前のフレームから今までの左ボタン (TakePointerPresses)
    ui::TypedText typed_;     // 前のフレームから今までに打たれた文字 (TakeTyped。打っている間だけ)
    bool textWanted_ = false; // ゲームが入力欄に打っている (SetTextInput): IME をつないでいる
    bool started_ = false;   // ウィンドウがあって、ゲームが動いている
    bool drawing_ = false;   // DrawFrame の中 (その中では WM_SIZE は描き直さない)
    bool toldDraw_ = false;  // EndDraw の失敗を知らせた (1 回だけ)
};

}  // namespace

int Run(const RmGameImage& image, bool smoke) {
    Frame frame(image, smoke);
    return frame.Run();
}

}  // namespace rm::game
