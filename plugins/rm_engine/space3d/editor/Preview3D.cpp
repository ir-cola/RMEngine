#include "rm_engine/space3d/editor/Preview3D.h"

#include "base/Log.h"
#include "editor/widgets/Theme.h"

#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace rm::space3d {

namespace theme = editor::theme;

namespace {

constexpr int kGridHalf = 5;  // 床の格子は -5 から 5 まで (1 本ごとに 1)

}  // namespace

Preview3D::Preview3D() {
    camera_.pivot = {0, 1, 0};
    camera_.yaw = 30;
    camera_.pitch = 15;
    camera_.distance = 6;
}

bool Preview3D::Draw(ui::Context& c, const ui::Rect& r, platform::Gfx& gfx, std::string_view id, const Content& content) {
    if (r.w < 8 || r.h < 8) return true;
    if (gfx.Generation() != generation_) {
        // 新しいデバイス (GPU が失われた): 古いデバイスで作ったものは全部捨てる。
        renderer_ = Renderer{};
        tried_ = false;
        bitmap_.Reset();
        texture_ = nullptr;
        dc_ = nullptr;
        generation_ = gfx.Generation();
    }
    if (!tried_) {
        tried_ = true;
        std::string err;
        if (!renderer_.Init(gfx.D3D(), &err)) log::Error("プレビュー: {}", err);
    }
    if (!renderer_.Ready()) return false;

    // マウス: 左ドラッグで回し、右 / 中ドラッグで動かし、ホイールで近づく。
    const ui::Context::ButtonResult b = c.ButtonBehavior(c.MakeId(id), r);
    const ui::Input& in = c.In();
    if (b.held) {
        c.LockMouse();
        camera_.yaw += in.mouseDX * 0.4f;
        camera_.pitch = std::clamp(camera_.pitch + in.mouseDY * 0.4f, -89.f, 89.f);
    }
    if (c.IsHovered(r) && (c.MousePressed(ui::Mouse::Right) || c.MousePressed(ui::Mouse::Middle))) panning_ = true;
    if (panning_ && (in.down[1] || in.down[2])) {
        c.LockMouse();
        const float s = camera_.distance * 0.0025f;
        camera_.pivot = camera_.pivot - camera_.Right() * (in.mouseDX * s) + camera_.Up() * (in.mouseDY * s);
    } else {
        panning_ = false;
    }
    if (c.IsHovered(r) && in.wheel != 0) camera_.distance = std::clamp(camera_.distance * std::pow(0.85f, in.wheel), 0.3f, 500.f);

    if (grid_.empty() || gridLight_ != theme::IsLight) {
        grid_.clear();
        gridLight_ = theme::IsLight;
        const float h = static_cast<float>(kGridHalf);
        for (int i = -kGridHalf; i <= kGridHalf; ++i) {
            const float v = static_cast<float>(i);
            const ui::Color col = i == 0 ? theme::SceneGridMajor() : theme::SceneGrid();
            grid_.push_back({{v, 0, -h}, col});
            grid_.push_back({{v, 0, h}, col});
            grid_.push_back({{-h, 0, v}, col});
            grid_.push_back({{h, 0, v}, col});
        }
    }

    ID2D1DeviceContext* dc = c.P().DC();
    float dpiX = 96, dpiY = 96;
    dc->GetDpi(&dpiX, &dpiY);
    const float scale = dpiX / 96.f;
    RenderFrame f;
    f.width = static_cast<uint32_t>(std::max(1.f, std::round(r.w * scale)));
    f.height = static_cast<uint32_t>(std::max(1.f, std::round(r.h * scale)));
    f.view = camera_.ViewMatrix();
    f.projection = camera_.ProjectionMatrix(r.w / r.h);
    f.clear = theme::SceneBackground();
    f.lines = grid_;
    f.meshes = content.meshes;
    f.extras = content.extras;
    // Direct2D のここまでの描画を先に GPU へ送る。そのあと Direct3D が自分の画面に描く。
    dc->Flush();
    ID3D11Texture2D* picture = renderer_.Render(gfx.D3DContext(), f);
    if (!picture) return false;
    if (picture != texture_ || dc != dc_ || !bitmap_) {
        bitmap_.Reset();
        Microsoft::WRL::ComPtr<IDXGISurface> surface;
        if (FAILED(picture->QueryInterface(IID_PPV_ARGS(&surface)))) return false;
        const D2D1_BITMAP_PROPERTIES1 props =
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpiX, dpiY);
        if (FAILED(dc->CreateBitmapFromDxgiSurface(surface.Get(), &props, &bitmap_))) return false;
        texture_ = picture;
        dc_ = dc;
    }
    c.P().Image(bitmap_.Get(), r);
    return true;
}

}  // namespace rm::space3d
