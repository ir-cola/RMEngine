#pragma once
// 2D 空間の描画: Camera2D を通して見た平面を、渡された画面の長方形に Direct2D で描く
// (ウィンドウがすでに UI を描いているデバイスコンテキスト - 3D と違い自前の絵は持たない:
// Direct2D は回転・拡大縮小・色味付きの画像をもう GPU で描ける)。
//
//   lines    (平面上: 格子)                                  すべての下
//   sprites  (画像、または 1 色の四角)                        奥から手前へ、渡された順
//   overlay  (平面上: 選んだものの輪郭)                       すべての上
//   debug    (平面上: ゲームの rm::Debug の線)                その上: 一度に多数。同じ色と太さの
//                                                              ひと続きごとに 1 回描く
//
// 画像は核の 1 つのやり方 (platform/Image: WIC) で読み、Direct2D のビットマップにする (BitmapFromImage)。
// ビットマップは Direct2D のデバイスのもので、全ウィンドウが共有するので、
// 別ウィンドウに移したパネルもそれを使える。

#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/space2d/runtime/View2D.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <span>

namespace rm::space2d {

struct Line2D {
    Vec2 a, b;    // 平面上
    Color color;
    float width = 1.f;  // DIP (カメラがどれだけ近くても画面上で同じ)
};

struct SpriteDraw {
    ID2D1Bitmap* image = nullptr;  // null: `color` の四角
    PixelRect part;                // 画像の一部 (アトラスのセル、ピクセル)。空: 全部
    Mat3 world;                    // 置かれた所 (親も含む)
    Vec2 size{1, 1};               // 平面上で `world` の前の大きさ (中心は物の原点)
    Color color{1, 1, 1, 1};       // 画像の色にかける (白: そのまま)
    bool flipX = false, flipY = false;
    bool crisp = false;            // 滑らかにせず最近傍のピクセル (ドット絵)
};

struct RenderFrame2D {
    ScreenRect viewport;
    Camera2D camera;
    Color clear{0, 0, 0, 1};  // アルファ 0: 塗らない (別の空間の絵の上の層)
    std::span<const Line2D> lines;
    std::span<const SpriteDraw> sprites;
    std::span<const Line2D> overlay;
    std::span<const Line2D> debug;
};

class Renderer2D {
public:
    // `dc` に描く (変換はあとで元に戻す)。ビューポートへの切り抜きは呼ぶ側がする。
    void Render(ID2D1DeviceContext* dc, const RenderFrame2D& f);

private:
    void DrawLines(ID2D1DeviceContext* dc, const RenderFrame2D& f, std::span<const Line2D> lines);
    // 同じ色と太さのひと続きにまとめた線。1 続きを 1 つのパスにして一度に描く (何千本でも軽い)。
    void DrawBatched(ID2D1DeviceContext* dc, const RenderFrame2D& f, std::span<const Line2D> lines);
    ID2D1SolidColorBrush* Brush(ID2D1DeviceContext* dc, Color c);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;  // ColorMatrix: 画像に色をかける
    ID2D1DeviceContext* owner_ = nullptr;       // brush_ と tint_ を作ったコンテキスト
};

}  // namespace rm::space2d
