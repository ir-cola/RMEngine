#pragma once
// 2D 空間's drawing: the plane seen through a Camera2D, drawn with Direct2D into the rectangle of
// the screen it is given (the device context the window already draws its UI with - no picture of
// its own, unlike 3D: Direct2D draws turned, scaled and tinted images on the GPU already).
//
//   lines    (in the plane: the grid)                        under everything
//   sprites  (an image, or a square of one colour)            back to front, as given
//   overlay  (in the plane: outlines of what is selected)     over everything
//
// Images are read by WIC (platform::LoadBitmapFromMemory) into Direct2D bitmaps. Bitmaps belong to
// the Direct2D device, which every window shares, so a panel moved to a window of its own keeps them.

#include "platform/Gfx.h"
#include "rm_engine/space2d/runtime/View2D.h"

#include <filesystem>
#include <span>
#include <string>

namespace rm::space2d {

struct Line2D {
    Vec2 a, b;    // in the plane
    Color color;
    float width = 1.f;  // DIPs (the same on the screen however near the camera is)
};

struct SpriteDraw {
    ID2D1Bitmap* image = nullptr;  // null: a square of `color`
    Mat3 world;                    // where it is (its parents included)
    Vec2 size{1, 1};               // in the plane, before `world` (its middle at the thing's origin)
    Color color{1, 1, 1, 1};       // multiplies the image's colours (white: as it is)
    bool flipX = false, flipY = false;
    bool crisp = false;            // nearest pixel (dot pictures) instead of smoothing
};

struct RenderFrame2D {
    ScreenRect viewport;
    Camera2D camera;
    Color clear{0, 0, 0, 1};
    std::span<const Line2D> lines;
    std::span<const SpriteDraw> sprites;
    std::span<const Line2D> overlay;
};

class Renderer2D {
public:
    // Draws into `dc` (its transform is put back afterwards). The caller clips to the viewport.
    void Render(ID2D1DeviceContext* dc, const RenderFrame2D& f);

private:
    void DrawLines(ID2D1DeviceContext* dc, const RenderFrame2D& f, std::span<const Line2D> lines);
    ID2D1SolidColorBrush* Brush(ID2D1DeviceContext* dc, Color c);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1Effect> tint_;  // ColorMatrix: multiplies an image by a colour
    ID2D1DeviceContext* owner_ = nullptr;       // the context brush_ and tint_ were made for
};

// An image file (PNG / JPEG / BMP / GIF / TIFF ... what WIC reads) as a Direct2D bitmap; null and
// `error` (Japanese) when it cannot be read.
Microsoft::WRL::ComPtr<ID2D1Bitmap> LoadImageFile(platform::Gfx& gfx, ID2D1DeviceContext* dc, const std::filesystem::path& file,
                                                  std::string* error);

}  // namespace rm::space2d
