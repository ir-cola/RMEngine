#pragma once
// ゲーム UI - a 動画 (UiVideo) played: Media Foundation's Media Engine reads the file from memory, decodes
// it (on the GPU when it can) and plays its sound itself; each new picture is copied into a texture of the
// same Direct3D device, which Direct2D draws as a bitmap (no copy through the CPU).
//
//   VideoPlayer::Open(gfx, bytes)   the file's bytes (the editor's file, the game's .rmpak)
//   Set(...) each frame              played / stopped, くり返す, 速さ, 音の大きさ, 始める所 / 終わる所
//   Frame(dc)                        the picture now (null until the first)

#include "platform/Gfx.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <memory>
#include <string>

struct IMFMediaEngine;
struct IMFMediaEngineNotify;
struct ID3D11Device;
struct ID3D11Texture2D;

namespace rm::game_ui {

class VideoPlayer {
public:
    // Null (and *why, Japanese) when it cannot be played: not a video Windows can read, no GPU device.
    static std::unique_ptr<VideoPlayer> Open(platform::Gfx& gfx, const std::string& bytes, std::string* why);
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;
    ~VideoPlayer();

    // As it should be now: `play` false - stopped (on its first picture until it has played). end <= 0: to
    // its end. Cheap when nothing changed.
    void Set(bool play, bool loop, float speed, float volume, double start, double end);
    // The picture now, as a bitmap of `dc`'s device (null until the first picture came).
    ID2D1Bitmap1* Frame(ID2D1DeviceContext* dc);
    // The file could not be played after all (told once by the renderer).
    bool Failed() const;

private:
    VideoPlayer() = default;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;  // the Gfx's (the engine decodes on it, the texture is made on it)
    Microsoft::WRL::ComPtr<IMFMediaEngine> engine_;
    Microsoft::WRL::ComPtr<IMFMediaEngineNotify> notify_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    ID2D1DeviceContext* bitmapOf_ = nullptr;
    unsigned width_ = 0, height_ = 0;
    bool started_ = false;    // 始める所 set (once the file's length is known)
    bool hasFrame_ = false;
    float speed_ = -1, volume_ = -1;
    int loop_ = -1;
};

}  // namespace rm::game_ui
