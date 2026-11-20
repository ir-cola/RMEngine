#pragma once
// ゲーム UI - 再生される 動画 (UiVideo): Media Foundation の Media Engine がファイルをメモリから読み、
// デコードし (できれば GPU で)、音も自分で再生する。新しい絵のたびに同じ Direct3D デバイスの
// テクスチャへコピーし、Direct2D がそれをビットマップとして描く (CPU を通すコピーはなし)。
//
//   VideoPlayer::Open(gfx, bytes)   ファイルのバイト列 (エディターのファイル、ゲームの .rmpak)
//   Set(...) 毎フレーム              再生 / 停止、くり返す、速さ、音の大きさ、始める所 / 終わる所
//   Frame(dc)                        いまの絵 (最初のものが来るまで null)

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
    // 再生できないときは null (と *why に日本語): Windows が読める動画ではない、GPU デバイスがない。
    static std::unique_ptr<VideoPlayer> Open(platform::Gfx& gfx, const std::string& bytes, std::string* why);
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;
    ~VideoPlayer();

    // いまそうあるべき状態: `play` が false - 止める (再生するまでは最初の絵で)。end <= 0: 終わりまで。
    // 何も変わっていなければ軽い。
    void Set(bool play, bool loop, float speed, float volume, double start, double end);
    // いまの絵を `dc` のデバイスのビットマップとして (最初の絵が来るまで null)。
    ID2D1Bitmap1* Frame(ID2D1DeviceContext* dc);
    // やはりファイルは再生できなかった (レンダラーが 1 度だけ知らせる)。
    bool Failed() const;

private:
    VideoPlayer() = default;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;  // Gfx のもの (エンジンはこれでデコードし、テクスチャもこれの上に作る)
    Microsoft::WRL::ComPtr<IMFMediaEngine> engine_;
    Microsoft::WRL::ComPtr<IMFMediaEngineNotify> notify_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    ID2D1DeviceContext* bitmapOf_ = nullptr;
    unsigned width_ = 0, height_ = 0;
    bool started_ = false;    // 始める所を設定した (ファイルの長さが分かったら 1 度だけ)
    bool hasFrame_ = false;
    float speed_ = -1, volume_ = -1;
    int loop_ = -1;
};

}  // namespace rm::game_ui
