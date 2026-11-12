#pragma once
// 2D 空間's drawing: the plane seen through a Camera2D, drawn with Direct2D into the rectangle of
// the screen it is given (the device context the window already draws its UI with - no picture of
// its own, unlike 3D: Direct2D draws turned, scaled and tinted images on the GPU already).
//
//   lines    (in the plane: the grid)                        under everything
//   sprites  (an image, or a square of one colour)            back to front, as given
//   overlay  (in the plane: outlines of what is selected)     over everything
//   debug    (in the plane: a game's rm::Debug lines)          over that: many at once, one draw per
//                                                              run of the same colour and width
//
// Images are read by the core's one way (platform/Image: WIC) into Direct2D bitmaps (BitmapFromImage).
// Bitmaps belong to the Direct2D device, which every window shares, so a panel moved to a window of its
// own keeps them.

#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/space2d/runtime/View2D.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <span>

namespace rm::space2d {

struct Line2D {
    Vec2 a, b;    // in the plane
    Color color;
    float width = 1.f;  // DIPs (the same on the screen however near the camera is)
};

struct SpriteDraw {
    ID2D1Bitmap* image = nullptr;  // null: a square of `color`
    PixelRect part;                // the part of the image (an atlas's cell, pixels); empty: all of it
    Mat3 world;                    // where it is (its parents included)
    Vec2 size{1, 1};               // in the plane, before `world` (its middle at the thing's origin)
    Color color{1, 1, 1, 1};       // multiplies the image's colours (white: as it is)
    bool flipX = false, flipY = false;
    bool crisp = false;            // nearest pixel (dot pictures) instead of smoothing
};

struct RenderFrame2D {
    ScreenRect viewport;
    Camera2D camera;
    Color clear{0, 0, 0, 1};  // alpha 0: not filled (a layer over another space's picture)
    std::span<const Line2D> lines;
    std::span<const SpriteDraw> sprites;
    std::span<const Line2D> overlay;
    std::span<const Line2D> debug;
};

class Renderer2D {
public:
    // Draws into `dc` (its transform is put back afterwards). The caller clips to the viewport.
    void Render(ID2D1DeviceContext* dc, const RenderFrame2D& f);

private:
    void DrawLines(ID2D1DeviceContext* dc, const RenderFrame2D& f, std::span<const Line2D> lines);
    // Lines in runs of the same colour and width, each run one path drawn at once (thousands stay cheap).
    void DrawBatched(ID2D1DeviceContext* dc, const RenderFrame2D& f, std::span<const Line2D> lines);
    ID2D1SolidColorBrush* Brush(ID2D1DeviceContext* dc, Color c);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;  // ColorMatrix: multiplies an image by a colour
    ID2D1DeviceContext* owner_ = nullptr;       // the context brush_ and tint_ were made for
};

}  // namespace rm::space2d
