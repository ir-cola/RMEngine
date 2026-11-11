#include "rm_engine/space3d/editor/Thumbnail.h"

#include "base/Utf.h"
#include "editor/assets/Thumbnails.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Texture.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rm::space3d {

namespace {

constexpr float kFovDegrees = 30.f;  // narrow: little bending at the edges of a small picture

// One renderer for every thumbnail (made again when the GPU changes).
Renderer& ThumbnailRenderer(ID3D11Device* device, std::string* error, bool* ready) {
    static Renderer renderer;
    static ID3D11Device* madeFor = nullptr;
    if (madeFor != device) {
        renderer = Renderer{};
        madeFor = device;
        renderer.Init(device, error);
    }
    *ready = renderer.Ready();
    return renderer;
}

// The picture the renderer drew, copied out of the GPU (rows from the top, no gaps).
bool ReadBack(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* picture, platform::Image& out) {
    D3D11_TEXTURE2D_DESC td{};
    picture->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return false;
    ctx->CopyResource(staging.Get(), picture);
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    out.width = td.Width;
    out.height = td.Height;
    out.pixels.resize(static_cast<size_t>(td.Width) * td.Height * 4);
    for (UINT y = 0; y < td.Height; ++y) {
        std::memcpy(out.pixels.data() + static_cast<size_t>(y) * td.Width * 4, static_cast<const uint8_t*>(map.pData) + static_cast<size_t>(y) * map.RowPitch,
                    static_cast<size_t>(td.Width) * 4);
    }
    ctx->Unmap(staging.Get(), 0);
    return true;
}

}  // namespace

bool MakeModelThumbnail(platform::Gfx& gfx, const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                        platform::Image& out, std::string* error) {
    bool ready = false;
    Renderer& renderer = ThumbnailRenderer(gfx.D3D(), error, &ready);
    if (!ready) return false;
    const auto mesh = LoadMeshFile(file, error);
    if (!mesh) return false;
    const auto gpu = renderer.Upload(*mesh, error);
    if (!gpu) return false;
    ModelPaint paint;
    paint.Load(renderer, gfx.Wic(), *mesh, file, root, PathToUtf8(file.filename()));
    TextureCache textures;  // only for this picture
    const std::span<const DrawMaterial> materials = paint.Materials(renderer, gfx.Wic(), textures, 0);

    // Framed by its bounds: the ball around them fits the narrow view.
    const Vec3 center = (mesh->min + mesh->max) * 0.5f;
    const float radius = std::max(Length(mesh->max - mesh->min) * 0.5f, 1e-4f);
    const float distance = radius / std::sin(kFovDegrees * 0.5f * 3.14159265f / 180.f) * 1.02f;
    const Vec3 from = Normalize({0.75f, 0.6f, 1.f});  // the front (+Z), a little from the right and above
    RenderFrame f;
    f.width = f.height = size;
    f.view = LookAt(center + from * distance, center, {0, 1, 0});
    f.projection = Perspective(kFovDegrees, 1.f, std::max(distance - radius * 1.5f, distance * 0.01f), distance + radius * 1.5f);
    f.clear = {0, 0, 0, 0};  // see-through around the model
    const MeshDraw draw{gpu.get(), Mat4{}, {1, 1, 1, 1}, false, materials, {0, 0, 0, 0}};
    f.meshes = std::span<const MeshDraw>(&draw, 1);
    ID3D11Texture2D* picture = renderer.Render(gfx.D3DContext(), f);
    if (!picture || !ReadBack(gfx.D3D(), gfx.D3DContext(), picture, out)) {
        if (error) *error = "3D の絵を作れませんでした";
        return false;
    }
    return true;
}

void RegisterEditor(editor::PluginRegistry& registry) {
    // The アセット panel shows a 3D model as a small picture of itself.
    static const editor::ThumbnailMaker kModel{asset::Kind::Model, &MakeModelThumbnail};
    registry.Contribute(editor::kThumbnailPoint, kModel);
}

}  // namespace rm::space3d
