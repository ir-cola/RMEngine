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

constexpr float kFovDegrees = 30.f;  // 狭く: 小さい画面の端で歪みが少ない

// `size` のサムネイルに読む絵の大きさの上限: サムネイルより少し大きい画像で足りる (読み込みも速い)。
uint32_t PictureMost(uint32_t size) { return std::clamp<uint32_t>(size * 2, 64, 1024); }

// レンダラーが描いた画面を、GPU から取り出したもの (行は上から、隙間なし)。
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

Renderer* ThumbnailRenderer(platform::Gfx& gfx, std::string* error) {
    static Renderer renderer;
    static uint64_t madeIn = 0;  // 作ったときの GPU のデバイス (それらが作り直されたら作り直す: Gfx::Generation)
    if (madeIn != gfx.Generation()) {
        renderer = Renderer{};
        madeIn = gfx.Generation();
        renderer.Init(gfx.D3D(), error);
    }
    return renderer.Ready() ? &renderer : nullptr;
}

bool RenderThumbnail(platform::Gfx& gfx, Renderer& renderer, const GpuMesh& mesh, std::span<const DrawMaterial> materials, uint32_t size,
                     platform::Image& out, std::string* error) {
    // 範囲で枠に収める: それを囲む球が、狭いビューに収まる。
    const Vec3 center = (mesh.min + mesh.max) * 0.5f;
    const float radius = std::max(Length(mesh.max - mesh.min) * 0.5f, 1e-4f);
    const float distance = radius / std::sin(kFovDegrees * 0.5f * 3.14159265f / 180.f) * 1.02f;
    const Vec3 from = Normalize({0.75f, 0.6f, -1.f});  // 正面 (-Z: 左手系で見る人の側)、少し右と上から
    RenderFrame f;
    f.width = f.height = size;
    f.view = LookAt(center + from * distance, center, {0, 1, 0});
    f.projection = Perspective(kFovDegrees, 1.f, std::max(distance - radius * 1.5f, distance * 0.01f), distance + radius * 1.5f);
    f.clear = {0, 0, 0, 0};  // モデルのまわりは透ける
    const MeshDraw draw{&mesh, Mat4{}, {1, 1, 1, 1}, false, materials, {0, 0, 0, 0}};
    f.meshes = std::span<const MeshDraw>(&draw, 1);
    ID3D11Texture2D* picture = renderer.Render(gfx.D3DContext(), f);
    if (!picture || !ReadBack(gfx.D3D(), gfx.D3DContext(), picture, out)) {
        if (error) *error = "3D の絵を作れませんでした";
        return false;
    }
    return true;
}

std::optional<ThumbnailPicture> LoadThumbnailPicture(const std::filesystem::path& file, bool srgb, uint32_t size) {
    const uint32_t most = PictureMost(size);
    const auto imp = asset::ReadImport<asset::ImageImport>(file);
    TextureSampling sampling = SamplingOf(imp);
    sampling.streamed = false;  // (サムネイル: 1 回だけ、全部を描く)
    std::string ignored;  // 読めない画像: それ無しで描く (理由はシーンビューが出す)
    auto image = platform::LoadImageFile(platform::ThreadImagingFactory(), file, platform::PixelOrder::Rgba, {most, most}, &ignored);
    if (!image) return std::nullopt;
    return ThumbnailPicture{MakeTexturePixels(std::move(*image), srgb, MipOptionsOf(imp)), sampling};
}

namespace {

// ワーカーが作ったもの: 形と、各マテリアルの画像 (読み込んだもの、または無いもの)。
struct Prepared {
    MeshData mesh;
    using Picture = ThumbnailPicture;  // ミップマップ込み (ここで作る。UI スレッドの外)
    std::vector<Picture> pictures;                                         // 画像は、読み方ごとに 1 つ
    std::vector<std::array<int, kPictureSlots>> slots;                     // マテリアルごと: `pictures` への添字 (-1: 無し)
};

}  // namespace

std::shared_ptr<void> PrepareModelThumbnail(const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                                            std::string* error) {
    auto mesh = LoadMeshFile(file, error);
    if (!mesh) return nullptr;
    auto p = std::make_shared<Prepared>();
    p->mesh = std::move(*mesh);
    IWICImagingFactory* wic = platform::ThreadImagingFactory();
    const uint32_t most = PictureMost(size);
    std::vector<int> made(p->mesh.images.size() * 2, -2);  // -2: 試していない。-1: 読めなかった
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
                std::optional<ThumbnailPicture> picture;
                if (!im.file.empty()) {
                    const std::filesystem::path at2 = RelatedFile(file, im.file, root);
                    if (!at2.empty()) picture = LoadThumbnailPicture(at2, ps.srgb, size);
                } else {
                    std::string ignored;  // 読めない画像: それ無しで描く (理由はシーンビューが出す)
                    auto image = platform::DecodeImage(wic, im.bytes, platform::PixelOrder::Rgba, {most, most}, &ignored);
                    if (image) picture = ThumbnailPicture{MakeTexturePixels(std::move(*image), ps.srgb, {}), {}};
                }
                at = -1;
                if (picture) {
                    at = static_cast<int>(p->pictures.size());
                    p->pictures.push_back(std::move(*picture));
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
    Renderer* r = ThumbnailRenderer(gfx, error);
    if (!r) return false;
    Renderer& renderer = *r;
    const MeshData& mesh = p.mesh;
    const auto gpu = renderer.Upload(mesh, error);
    if (!gpu) return false;
    std::vector<std::shared_ptr<const GpuTexture>> textures;
    for (const Prepared::Picture& pic : p.pictures) {
        std::string ignored;
        textures.push_back(renderer.UploadTexture(pic.pixels, &ignored, pic.sampling));
    }
    std::vector<DrawMaterial> materials;
    for (size_t i = 0; i < mesh.materials.size(); ++i) {
        DrawMaterial d = MaterialNumbers(mesh.materials[i]);
        for (size_t k = 0; k < kPictureSlots; ++k) {
            const int t = p.slots[i][k];
            SetPicture(d, k, t >= 0 ? textures[static_cast<size_t>(t)] : nullptr);
        }
        materials.push_back(d);
    }
    return RenderThumbnail(gfx, renderer, *gpu, materials, size, out, error);
}

bool MakeModelThumbnail(platform::Gfx& gfx, const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                        platform::Image& out, std::string* error) {
    const auto prepared = PrepareModelThumbnail(file, root, size, error);
    return prepared && DrawModelThumbnail(gfx, prepared.get(), size, out, error);
}

void RegisterEditor(editor::PluginRegistry& registry) {
    // アセット パネルは、3D モデルを自分の小さな絵として見せる (サムネイルのワーカースレッドで読む)。
    static const editor::ThumbnailMaker kModel{asset::Kind::Model, &PrepareModelThumbnail, &DrawModelThumbnail};
    registry.Contribute(editor::kThumbnailPoint, kModel);
}

}  // namespace rm::space3d
