#include "rm_engine/space3d/editor/Thumbnail.h"

#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "editor/assets/Thumbnails.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/Texture.h"
#include "rm_engine/space3d/runtime/View.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <vector>

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

namespace {

// What the worker made: the shape, and each material's pictures decoded (or not there).
struct Prepared {
    MeshData mesh;
    struct Picture {
        TexturePixels pixels;  // with its mipmaps (made here, off the UI thread)
        bool sharp = false;
    };
    std::vector<Picture> pictures;                                         // each picture once per way it is read
    std::vector<std::array<int, kPictureSlots>> slots;                     // per material: into `pictures` (-1: none)
};

}  // namespace

std::shared_ptr<void> PrepareModelThumbnail(const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                                            std::string* error) {
    auto mesh = LoadMeshFile(file, error);
    if (!mesh) return nullptr;
    auto p = std::make_shared<Prepared>();
    p->mesh = std::move(*mesh);
    IWICImagingFactory* wic = platform::ThreadImagingFactory();
    // Pictures a little bigger than the thumbnail are enough (and quick to decode).
    const uint32_t most = std::clamp<uint32_t>(size * 2, 64, 1024);
    std::vector<int> made(p->mesh.images.size() * 2, -2);  // -2: not tried; -1: could not be read
    for (const MeshMaterial& m : p->mesh.materials) {
        std::array<int, kPictureSlots> slots;
        const auto pictures = PictureSlots(m);
        for (size_t k = 0; k < kPictureSlots; ++k) {
            const PictureSlot ps = pictures[k];
            slots[k] = -1;
            if (ps.image < 0 || static_cast<size_t>(ps.image) >= p->mesh.images.size()) continue;
            int& at = made[static_cast<size_t>(ps.image) * 2 + (ps.srgb ? 1 : 0)];
            if (at == -2) {
                const MeshImage& im = p->mesh.images[static_cast<size_t>(ps.image)];
                std::optional<platform::Image> image;
                bool sharp = false;
                std::string ignored;  // a picture that cannot be read: drawn without it (the scene view tells why)
                if (!im.file.empty()) {
                    const std::filesystem::path at2 = RelatedFile(file, im.file, root);
                    if (!at2.empty()) {
                        const auto imp = asset::ReadImport<asset::ImageImport>(at2);
                        sharp = imp.sharp;
                        image = platform::LoadImageFile(wic, at2, platform::PixelOrder::Rgba, {most, most}, &ignored);
                    }
                } else {
                    image = platform::DecodeImage(wic, im.bytes, platform::PixelOrder::Rgba, {most, most}, &ignored);
                }
                at = -1;
                if (image) {
                    at = static_cast<int>(p->pictures.size());
                    p->pictures.push_back({MakeTexturePixels(std::move(*image), ps.srgb), sharp});
                }
            }
            slots[k] = at;
        }
        p->slots.push_back(slots);
    }
    return p;
}

bool DrawModelThumbnail(platform::Gfx& gfx, const void* prepared, uint32_t size, platform::Image& out, std::string* error) {
    const Prepared& p = *static_cast<const Prepared*>(prepared);
    bool ready = false;
    Renderer& renderer = ThumbnailRenderer(gfx.D3D(), error, &ready);
    if (!ready) return false;
    const MeshData& mesh = p.mesh;
    const auto gpu = renderer.Upload(mesh, error);
    if (!gpu) return false;
    std::vector<std::shared_ptr<const GpuTexture>> textures;
    for (const Prepared::Picture& pic : p.pictures) {
        std::string ignored;
        textures.push_back(renderer.UploadTexture(pic.pixels, &ignored, pic.sharp));
    }
    std::vector<DrawMaterial> materials;
    for (size_t i = 0; i < mesh.materials.size(); ++i) {
        DrawMaterial d = MaterialNumbers(mesh.materials[i]);
        for (size_t k = 0; k < kPictureSlots; ++k) {
            const int t = p.slots[i][k];
            SetPicture(d, k, t >= 0 ? textures[static_cast<size_t>(t)].get() : nullptr);
        }
        materials.push_back(d);
    }

    // Framed by its bounds: the ball around them fits the narrow view.
    const Vec3 center = (mesh.min + mesh.max) * 0.5f;
    const float radius = std::max(Length(mesh.max - mesh.min) * 0.5f, 1e-4f);
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

bool MakeModelThumbnail(platform::Gfx& gfx, const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                        platform::Image& out, std::string* error) {
    const auto prepared = PrepareModelThumbnail(file, root, size, error);
    return prepared && DrawModelThumbnail(gfx, prepared.get(), size, out, error);
}

void RegisterEditor(editor::PluginRegistry& registry) {
    // The アセット panel shows a 3D model as a small picture of itself (read on the thumbnails' worker thread).
    static const editor::ThumbnailMaker kModel{asset::Kind::Model, &PrepareModelThumbnail, &DrawModelThumbnail};
    registry.Contribute(editor::kThumbnailPoint, kModel);
}

}  // namespace rm::space3d
