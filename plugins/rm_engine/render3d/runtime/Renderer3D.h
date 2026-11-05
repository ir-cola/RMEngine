#pragma once
// 3D 描画: draws 3D lines and lit boxes with Direct3D 11 into a picture of its own (a texture).
// The editor shows that picture in a panel (the scene view); the exported game will draw the same
// way to its window. It knows only 3D 空間 (matrices) - nothing of games or editors.
//
//   render3d::Renderer r;   r.Init(device, &error);
//   render3d::Frame f{width, height, view, projection, clearColor, lines, boxes};
//   ID3D11Texture2D* picture = r.Render(context, f);
//
// The shaders are written below in HLSL and compiled when the renderer starts (D3DCompile, which
// every Windows 10 / 11 has). Lines and boxes are drawn with 4x multisampling when the GPU can.

#include "base/Math.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <span>
#include <string>

namespace rm::render3d {

struct LineVertex {
    Vec3 position;
    Color color;  // alpha blends (a fading grid)
};

// The unit cube (-0.5 .. 0.5) placed by `world`, lit from one direction.
struct Box {
    space3d::Mat4 world;
    Color color;
};

struct Frame {
    uint32_t width = 0, height = 0;  // pixels
    space3d::Mat4 view, projection;
    Color clear{0, 0, 0, 1};
    std::span<const LineVertex> lines;  // pairs: 0-1, 2-3, ...
    std::span<const Box> boxes;
    Vec3 lightDirection{-0.45f, -1.f, -0.3f};  // where the light goes (from above, a little from the front)
};

class Renderer {
public:
    bool Init(ID3D11Device* device, std::string* error);
    bool Ready() const { return boxVs_ != nullptr; }
    // Draws the frame and returns the finished picture (B8G8R8A8, usable by Direct2D); null on failure.
    // The picture is kept until the next Render (its size follows the frame's).
    ID3D11Texture2D* Render(ID3D11DeviceContext* ctx, const Frame& f);

private:
    bool Resize(uint32_t width, uint32_t height);
    bool LineCapacity(size_t vertices);

    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11VertexShader> lineVs_, boxVs_;
    Ptr<ID3D11PixelShader> linePs_, boxPs_;
    Ptr<ID3D11InputLayout> lineLayout_, boxLayout_;
    Ptr<ID3D11Buffer> cubeVb_, cubeIb_, lineVb_, constants_;
    size_t lineCapacity_ = 0;
    Ptr<ID3D11RasterizerState> raster_;
    Ptr<ID3D11DepthStencilState> depth_;
    Ptr<ID3D11BlendState> blend_;

    UINT samples_ = 1;
    uint32_t width_ = 0, height_ = 0;
    Ptr<ID3D11Texture2D> picture_;      // what is returned (resolved)
    Ptr<ID3D11Texture2D> multisample_;  // drawn into when samples_ > 1
    Ptr<ID3D11RenderTargetView> rtv_;
    Ptr<ID3D11Texture2D> depthTex_;
    Ptr<ID3D11DepthStencilView> dsv_;
};

}  // namespace rm::render3d
