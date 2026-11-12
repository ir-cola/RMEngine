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

        platform::WindowDesc d;
        d.title = Utf8ToWide(title_.empty() ? std::string(image_.title ? image_.title : "Game") : title_);
        game_->PreferredSize(d.widthDip, d.heightDip);
        d.minWidthDip = 320;
        d.minHeightDip = 240;
        d.captionRgb = 0x000000;
        if (!window_.Create(d, this)) return Fail("ウィンドウを作れませんでした");
        if (!target_.Create(gfx_, window_.Handle(), window_.Dpi())) return Fail("描く場所 (スワップチェーン) を作れませんでした");
        ShowWindow(window_.Handle(), smoke_ ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL);

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
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - last).count();
            last = now;
            game_->Update(dt);
            if (!DrawFrame()) return Fail("描けませんでした (GPU が失われた可能性があります)");
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
    LRESULT OnMessage(HWND, UINT msg, WPARAM, LPARAM lp, bool& handled) override {
        switch (msg) {
            case WM_SIZE:
                if (target_.Context()) target_.Resize(LOWORD(lp), HIWORD(lp));
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

    bool DrawFrame() {
        ID2D1DeviceContext* dc = target_.Context();
        if (!dc) return true;
        dc->BeginDraw();
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->Clear(D2D1::ColorF(0, 0, 0, 1));
        const float scale = target_.Dpi() / 96.f;
        game_->Draw(dc, static_cast<float>(target_.Width()) / scale, static_cast<float>(target_.Height()) / scale);
        const HRESULT hr = dc->EndDraw();
        if (FAILED(hr)) log::Warn("描画が失敗しました (0x{:08X})", static_cast<uint32_t>(hr));
        return target_.Present();
    }

    int Fail(const std::string& why) {
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
    bool quit_ = false;
};

}  // namespace

int Run(const RmGameImage& image, bool smoke) {
    Frame frame(image, smoke);
    return frame.Run();
}

}  // namespace rm::game
