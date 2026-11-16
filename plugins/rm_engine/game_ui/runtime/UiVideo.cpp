#include "rm_engine/game_ui/runtime/UiVideo.h"

#include <d3d11.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <shlwapi.h>

#include <algorithm>
#include <atomic>
#include <mutex>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

namespace rm::game_ui {

using Microsoft::WRL::ComPtr;

namespace {

// What the Media Engine tells (on its own threads): only whether it failed.
class Notify final : public IMFMediaEngineNotify {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *out = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override {
        if (event == MF_MEDIA_ENGINE_EVENT_ERROR) failed = true;
        return S_OK;
    }
    std::atomic<bool> failed{false};

private:
    std::atomic<ULONG> refs_{1};
};

bool StartMediaFoundation() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] { ok = SUCCEEDED(MFStartup(MF_VERSION)); });  // (for the process: never shut down)
    return ok;
}

}  // namespace

std::unique_ptr<VideoPlayer> VideoPlayer::Open(platform::Gfx& gfx, const std::string& bytes, std::string* why) {
    auto fail = [&](const char* text) {
        if (why) *why = text;
        return nullptr;
    };
    ID3D11Device* device = gfx.D3D();
    if (!device) return fail("GPU (Direct3D 11) がありません");
    if (bytes.empty()) return fail("動画のファイルが空か、読めません");
    if (!StartMediaFoundation()) return fail("Media Foundation を使えません (Windows の N エディションなど)");
    // The Media Engine decodes on the same device from its own threads: the device must take that.
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&mt)))) mt->SetMultithreadProtected(TRUE);
    UINT token = 0;
    ComPtr<IMFDXGIDeviceManager> manager;
    if (FAILED(MFCreateDXGIDeviceManager(&token, &manager)) || FAILED(manager->ResetDevice(device, token))) return fail("動画のための GPU の準備ができません");
    std::unique_ptr<VideoPlayer> p(new VideoPlayer());
    p->device_ = device;
    auto* notify = new Notify();
    p->notify_.Attach(notify);
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaEngineClassFactory> factory;
    if (FAILED(MFCreateAttributes(&attributes, 3)) || FAILED(attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, manager.Get())) ||
        FAILED(attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, p->notify_.Get())) ||
        FAILED(attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM)) ||
        FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateInstance(0, attributes.Get(), &p->engine_))) {
        return fail("動画を流す仕組み (Media Engine) を作れません");
    }
    ComPtr<IMFMediaEngineEx> ex;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()), static_cast<UINT>(bytes.size())));  // (a copy of the bytes)
    ComPtr<IMFByteStream> byteStream;
    if (!stream || FAILED(p->engine_.As(&ex)) || FAILED(MFCreateMFByteStreamOnStream(stream.Get(), &byteStream))) return fail("動画を読む準備ができません");
    BSTR hint = SysAllocString(L"video.mp4");  // (a name for its kind: the bytes themselves decide)
    const HRESULT hr = ex->SetSourceFromByteStream(byteStream.Get(), hint);
    SysFreeString(hint);
    if (FAILED(hr)) return fail("この動画は読めません");
    p->engine_->SetAutoPlay(FALSE);
    p->engine_->SetPreload(MF_MEDIA_ENGINE_PRELOAD_AUTOMATIC);
    return p;
}

VideoPlayer::~VideoPlayer() {
    if (engine_) engine_->Shutdown();  // (its threads and its sound stop here)
}

bool VideoPlayer::Failed() const { return !engine_ || static_cast<const Notify*>(notify_.Get())->failed; }

void VideoPlayer::Set(bool play, bool loop, float speed, float volume, double start, double end) {
    if (Failed()) return;
    if (!started_ && engine_->GetReadyState() >= MF_MEDIA_ENGINE_READY_HAVE_METADATA) {
        started_ = true;
        if (start > 0) engine_->SetCurrentTime(start);
    }
    const int wantLoop = loop && end <= 0 ? 1 : 0;  // (with an end: looped by hand below)
    if (wantLoop != loop_) {
        loop_ = wantLoop;
        engine_->SetLoop(wantLoop != 0);
    }
    if (speed != speed_) {
        speed_ = speed;
        engine_->SetDefaultPlaybackRate(speed);
        engine_->SetPlaybackRate(speed);
    }
    if (volume != volume_) {
        volume_ = volume;
        engine_->SetVolume(std::clamp(static_cast<double>(volume), 0.0, 1.0));
    }
    if (end > 0 && started_ && engine_->GetCurrentTime() >= end) {
        if (loop) {
            engine_->SetCurrentTime(std::max(start, 0.0));
        } else if (!engine_->IsPaused()) {
            engine_->Pause();
        }
        if (!loop) return;
    }
    if (play && engine_->IsPaused() && !(end > 0 && !loop && engine_->GetCurrentTime() >= end)) engine_->Play();
    if (!play && !engine_->IsPaused()) engine_->Pause();
}

ID2D1Bitmap1* VideoPlayer::Frame(ID2D1DeviceContext* dc) {
    if (Failed() || !dc) return nullptr;
    DWORD w = 0, h = 0;
    if (FAILED(engine_->GetNativeVideoSize(&w, &h)) || w == 0 || h == 0) return nullptr;
    if (!texture_ || w != width_ || h != height_ || dc != bitmapOf_) {
        // A texture of the picture's size the engine copies into, seen by Direct2D as a bitmap.
        texture_.Reset();
        bitmap_.Reset();
        hasFrame_ = false;
        width_ = w;
        height_ = h;
        bitmapOf_ = dc;
        ComPtr<IDXGISurface> surface;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        // On the device the engine was given (the Gfx's, which Direct2D draws with too).
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &texture_)) || FAILED(texture_.As(&surface))) return nullptr;
        const D2D1_BITMAP_PROPERTIES1 props =
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap_))) return nullptr;
    }
    LONGLONG pts = 0;
    if (engine_->OnVideoStreamTick(&pts) == S_OK) {
        const MFVideoNormalizedRect src{0, 0, 1, 1};
        const RECT dst{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        const MFARGB black{0, 0, 0, 255};
        if (SUCCEEDED(engine_->TransferVideoFrame(texture_.Get(), &src, &dst, &black))) hasFrame_ = true;
    }
    return hasFrame_ ? bitmap_.Get() : nullptr;
}

}  // namespace rm::game_ui
