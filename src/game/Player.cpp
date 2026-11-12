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
#include <algorithm>

namespace rm::game {

namespace {

// game.log next to the exe (when that folder can be written: not under Program Files).
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
            // Minimized: nothing is shown, so nothing is drawn or played - wait for a message (no core
            // spent on a loop), and the time away does not count.
            if (IsIconic(window_.Handle()) && !smoke_) {
                WaitMessage();
                last = std::chrono::steady_clock::now();
                continue;
            }
            const auto now = std::chrono::steady_clock::now();
            // At most a quarter of a second (as the editor's Play): a frame that took long - the window
            // dragged, the PC busy - never makes things jump.
            const double dt = std::min(std::chrono::duration<double>(now - last).count(), 0.25);
            last = now;
            game_->Update(dt);
            if (!DrawFrame()) {
                if (!Recover()) return Fail("描けませんでした (GPU が失われ、作り直せませんでした)");
                continue;
            }
            // Without vsync: no more than the target frames a second.
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

    // --- Window -----------------------------------------------------------------------------------
    LRESULT OnMessage(HWND, UINT msg, WPARAM wp, LPARAM lp, bool& handled) override {
        switch (msg) {
            case WM_SIZE:
                if (target_.Context()) {
                    target_.Resize(LOWORD(lp), HIWORD(lp));
                    // Drawn again at once (while the edge is dragged the loop above does not run).
                    if (started_ && wp != SIZE_MINIMIZED && !drawing_) DrawFrame();
                }
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

    // False when the GPU was lost (Recover makes it again).
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
        return !gfx_.Lost();  // (a Present that failed for another reason: the next frame tries again)
    }

    // The GPU was lost (a driver updated, the GPU reset): the devices and the window's target made
    // again; the game's drawers see Gfx::Generation change and make theirs again.
    bool Recover() {
        log::Warn("GPU が失われたので作り直します ({:08X})", static_cast<uint32_t>(gfx_.LostReason()));
        target_.Reset();
        if (!gfx_.Recreate()) return false;
        return target_.Create(gfx_, window_.Handle(), window_.Dpi());
    }

    // Fullscreen / borderless: a window without its frame over the whole monitor it is on.
    void CoverMonitor() {
        const HWND hwnd = window_.Handle();
        MONITORINFO mi{sizeof(mi)};
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP);
        const RECT& r = mi.rcMonitor;
        SetWindowPos(hwnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }

    int Fail(const std::string& why) {
        if (game_ && started_) game_->Stop();  // (the scripts' EndPlay ...)
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
    bool started_ = false;   // the window is there and the game runs
    bool drawing_ = false;   // inside DrawFrame (WM_SIZE does not draw again inside it)
    bool toldDraw_ = false;  // a failed EndDraw was told (once)
};

}  // namespace

int Run(const RmGameImage& image, bool smoke) {
    Frame frame(image, smoke);
    return frame.Run();
}

}  // namespace rm::game
