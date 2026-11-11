#pragma once
// 3D 空間 - drawing: 3D lines, lit boxes and models with Direct3D 11 into a picture of its own (a
// texture), lit by up to 8 lights (Lighting.h) with the shadows of one directional light. The editor shows that picture in a panel (the
// scene view); the exported game will draw the same way to its window. Knows nothing of games or
// editors. (The space and its drawing are one plugin: one is never used without the other -
// decided 2026-11-05.)
//
//   space3d::Renderer r;   r.Init(device, &error);
//   auto robot = r.Upload(meshData, &error);          // once: the shape stays on the GPU
//   space3d::RenderFrame f{width, height, view, projection, clearColor, lines, boxes};
//   f.meshes = draws;   f.shadows = true;             // MeshDraw{robot.get(), world, color, ..., materials}
//   ID3D11Texture2D* picture = r.Render(context, f);
//
// Colour: draw's colour x the part's material colour x its picture (UV, repeating, mipmapped) x the
// light. A model's parts (MeshPart) are drawn one by one with their own material (DrawMaterial:
// the colour and the picture on the GPU - UploadTexture). The selected thing is shown by a glow at
// its edges (MeshDraw::highlight), which keeps the material's colours.
//
// The shaders are written in Renderer3D.cpp in HLSL and compiled when the renderer starts
// (D3DCompile, which every Windows 10 / 11 has). Drawn with 4x multisampling when the GPU can.
//
// Shadows: a shadow map - everything that casts a shadow is first drawn from the light into a depth
// picture (orthographic, fitted to what casts); each pixel then looks there to see whether something
// is between it and the light, at 3 x 3 places with the GPU's comparing filter (PCF: soft edges).
// The floor (y = 0) is an invisible plane that only shows the shadows falling on it.

#include "base/Math.h"
#include "platform/Image.h"
#include "rm_engine/space3d/runtime/Lighting.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rm::space3d {

struct LineVertex {
    Vec3 position;
    Color color;  // alpha blends (a fading grid)
};

// The unit cube (-0.5 .. 0.5) placed by `world`, lit from one direction.
struct Box {
    Mat4 world;
    Color color;
    bool castShadow = true;
};

// A model's shape on the GPU (Renderer::Upload). Whoever draws it keeps it; it lives as long as
// that (the renderer does not need it between frames).
class GpuMesh {
public:
    Vec3 min{0, 0, 0}, max{0, 0, 0};  // the bounds in the model's own space
    uint32_t indexCount = 0;
    std::vector<MeshPart> parts;      // one draw each, by material (at least one)

private:
    friend class Renderer;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertices, indices;
};

// A picture on the GPU (Renderer::UploadTexture), with its smaller copies (mipmaps: no shimmer far away).
class GpuTexture {
public:
    uint32_t width = 0, height = 0;

private:
    friend class Renderer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
};

// How one material of a model is painted.
struct DrawMaterial {
    Color color{1, 1, 1, 1};
    const GpuTexture* texture = nullptr;  // null: the colour only
    AlphaMode alpha = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;  // Mask: less than this is not drawn
};

struct MeshDraw {
    const GpuMesh* mesh = nullptr;
    Mat4 world;
    Color color{1, 1, 1, 1};  // times every material (white: as the model is)
    bool castShadow = true;
    std::span<const DrawMaterial> materials;  // by MeshPart::material; a part without one here is white
    Color highlight{0, 0, 0, 0};              // a glow at the edges (selected): its colour, alpha = how strong
};

struct RenderFrame {
    uint32_t width = 0, height = 0;  // pixels
    Mat4 view, projection;
    Color clear{0, 0, 0, 1};
    std::span<const LineVertex> lines;  // pairs: 0-1, 2-3, ...
    std::span<const Box> boxes;
    std::span<const MeshDraw> meshes;
    // The lights (Lighting.h: at most kMaxLights). None: one white directional light going
    // `lightDirection` (from above, a little from the front).
    std::span<const Light> lights;
    Vec3 lightDirection{-0.45f, -1.f, -0.3f};
    bool shadows = false;  // of the first directional light that casts them
    Color shadowColor{0, 0, 0, 0.4f};  // the shadows on the floor (alpha: how dark)
};

class Renderer {
public:
    static constexpr uint32_t kShadowMapSize = 2048;

    bool Init(ID3D11Device* device, std::string* error);
    bool Ready() const { return meshVs_ != nullptr; }
    // The shape goes to the GPU once; null (and *error, Japanese) when it cannot.
    std::shared_ptr<const GpuMesh> Upload(const MeshData& mesh, std::string* error);
    // A picture (PixelOrder::Rgba) goes to the GPU once, with its mipmaps; null (and *error) when it cannot.
    std::shared_ptr<const GpuTexture> UploadTexture(const platform::Image& rgba, std::string* error);
    // Draws the frame and returns the finished picture (B8G8R8A8, usable by Direct2D); null on failure.
    // The picture is kept until the next Render (its size follows the frame's).
    ID3D11Texture2D* Render(ID3D11DeviceContext* ctx, const RenderFrame& f);

private:
    struct Item {
        const GpuMesh* mesh;
        const Mat4* world;
        Color color;
        bool castShadow;
        std::span<const DrawMaterial> materials;
        Color highlight;
    };

    bool Resize(uint32_t width, uint32_t height);
    bool LineCapacity(size_t vertices);
    bool MakeShadowMap();

    template <class T>
    using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11VertexShader> lineVs_, meshVs_, shadowVs_;
    Ptr<ID3D11PixelShader> linePs_, meshPs_, floorPs_;
    Ptr<ID3D11InputLayout> lineLayout_, meshLayout_;
    Ptr<ID3D11Buffer> lineVb_, constants_, lights_;
    std::shared_ptr<const GpuMesh> cube_, floor_;
    size_t lineCapacity_ = 0;
    Ptr<ID3D11RasterizerState> raster_, shadowRaster_;
    Ptr<ID3D11DepthStencilState> depth_, depthNoWrite_;
    Ptr<ID3D11BlendState> blend_;
    Ptr<ID3D11SamplerState> textureSampler_;  // pictures: repeat, smooth, mipmapped
    std::vector<Item> items_;

    // The shadow map (depth seen from the light).
    Ptr<ID3D11Texture2D> shadowTex_;
    Ptr<ID3D11DepthStencilView> shadowDsv_;
    Ptr<ID3D11ShaderResourceView> shadowSrv_;
    Ptr<ID3D11SamplerState> shadowSampler_;

    UINT samples_ = 1;
    uint32_t width_ = 0, height_ = 0;
    Ptr<ID3D11Texture2D> picture_;      // what is returned (resolved)
    Ptr<ID3D11Texture2D> multisample_;  // drawn into when samples_ > 1
    Ptr<ID3D11RenderTargetView> rtv_;
    Ptr<ID3D11Texture2D> depthTex_;
    Ptr<ID3D11DepthStencilView> dsv_;
};

}  // namespace rm::space3d
