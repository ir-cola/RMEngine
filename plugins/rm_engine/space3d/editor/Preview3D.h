#pragma once
// 3D 空間 - a small 3D picture of its own inside a panel (the パーティクル tab's preview; later the
// animator's and the assets'): a floor grid, a camera the mouse turns (left drag: turn, right / middle
// drag: move, wheel: nearer / farther), and what the caller puts in it (models, ExtraDraw).
//
// It has its own renderer and picture (made again when the GPU's devices are: platform::Gfx::Generation).

#include "platform/Gfx.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"
#include "ui/Context.h"

#include <d2d1_1.h>
#include <wrl/client.h>

#include <span>
#include <string_view>
#include <vector>

namespace rm::space3d {

class Preview3D {
public:
    Preview3D();

    struct Content {
        std::span<const MeshDraw> meshes;
        std::span<const ExtraDraw> extras;
    };
    // Draws into r (handling the mouse over it: `id` keeps its drags apart). False when 3D cannot be drawn.
    bool Draw(ui::Context& c, const ui::Rect& r, platform::Gfx& gfx, std::string_view id, const Content& content);
    OrbitView& Camera() { return camera_; }

private:
    Renderer renderer_;
    bool tried_ = false;
    uint64_t generation_ = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap_;
    ID3D11Texture2D* texture_ = nullptr;
    ID2D1DeviceContext* dc_ = nullptr;
    OrbitView camera_;
    bool panning_ = false;
    std::vector<LineVertex> grid_;
    bool gridLight_ = false;  // the theme the grid's colours were taken from
};

}  // namespace rm::space3d
