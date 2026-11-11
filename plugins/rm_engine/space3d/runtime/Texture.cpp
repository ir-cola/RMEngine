#include "rm_engine/space3d/runtime/Texture.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "platform/Image.h"
#include "rm_engine/space3d/runtime/MeshFile.h"

#include <algorithm>

namespace rm::space3d {

std::shared_ptr<const GpuTexture> LoadTexture(Renderer& renderer, IWICImagingFactory* wic, std::span<const uint8_t> bytes, bool srgb,
                                              std::string* error) {
    const auto image = platform::DecodeImage(wic, bytes, platform::PixelOrder::Rgba, {kMaxTextureSize, kMaxTextureSize}, error);
    return image ? renderer.UploadTexture(*image, srgb, error) : nullptr;
}

const GpuTexture* TextureCache::Get(Renderer& renderer, IWICImagingFactory* wic, const std::filesystem::path& file, bool srgb, double now) {
    Entry& e = entries_[(srgb ? L"c:" : L"n:") + file.native()];
    e.usedAt = now;
    if (e.loaded && now - e.checkedAt < kCheckSeconds) return e.gpu.get();
    e.checkedAt = now;
    const std::string name = PathToUtf8(file.filename());
    std::error_code ec;
    const auto time = asset::ChangedAt(file, ec);  // the picture or its import settings (.meta)
    if (ec) {
        e.gpu.reset();
        e.loaded = true;
        e.time = {};
        if (!e.told) log::Warn("テクスチャ「{}」が見つからないので、その絵なしで描きます", name);
        e.told = true;
        return nullptr;
    }
    if (e.loaded && e.gpu && time == e.time) return e.gpu.get();  // not changed
    if (e.loaded && !e.gpu && time == e.time) return nullptr;     // could not be read, and still the same
    const bool again = e.gpu != nullptr;
    e.loaded = true;
    e.time = time;
    e.told = false;
    std::string error;
    const auto imp = asset::ReadImport<asset::ImageImport>(file);
    const uint32_t most = std::min(kMaxTextureSize, static_cast<uint32_t>(std::max(imp.maxSize, 1)));
    const auto image = platform::LoadImageFile(wic, file, platform::PixelOrder::Rgba, {most, most}, &error);
    e.gpu = image ? renderer.UploadTexture(*image, srgb, &error, imp.sharp) : nullptr;
    if (e.gpu) {
        log::Info("テクスチャ「{}」を{}読みました ({} x {})", name, again ? "もう一度" : "", e.gpu->width, e.gpu->height);
    } else {
        log::Warn("テクスチャ「{}」を読めないので、その絵なしで描きます: {}", name, error);
    }
    return e.gpu.get();
}

void TextureCache::ForgetUnused(double now, double seconds) {
    std::erase_if(entries_, [&](const auto& kv) { return now - kv.second.usedAt > seconds; });
}

void ModelPaint::Load(Renderer& renderer, IWICImagingFactory* wic, const MeshData& mesh, const std::filesystem::path& modelFile,
                      const std::filesystem::path& root, const std::string& modelName) {
    draw_.clear();
    slots_.clear();
    // Each picture inside the model goes to the GPU once per way it is read, however many materials use it.
    std::vector<std::shared_ptr<const GpuTexture>> inside(mesh.images.size() * 2);
    std::vector<bool> tried(mesh.images.size() * 2, false);
    auto slot = [&](int image, bool srgb) {
        Slot out;
        out.srgb = srgb;
        if (image < 0 || static_cast<size_t>(image) >= mesh.images.size()) return out;
        const MeshImage& im = mesh.images[static_cast<size_t>(image)];
        if (!im.file.empty()) {
            out.file = RelatedFile(modelFile, im.file, root);
            if (out.file.empty()) {
                log::Warn("モデル「{}」: テクスチャ「{}」はプロジェクトの Assets の外にあるので読みません (その絵なしで描きます)", modelName, im.file);
            }
            return out;
        }
        const size_t k = static_cast<size_t>(image) * 2 + (srgb ? 1 : 0);
        if (!tried[k]) {
            tried[k] = true;
            std::string error;
            inside[k] = LoadTexture(renderer, wic, im.bytes, srgb, &error);
            if (!inside[k]) log::Warn("モデル「{}」: 中の画像「{}」を読めないので、その絵なしで描きます: {}", modelName, im.name, error);
        }
        out.own = inside[k];
        return out;
    };
    for (const MeshMaterial& m : mesh.materials) {
        DrawMaterial d;
        d.color = m.baseColor;
        d.alpha = m.alpha;
        d.alphaCutoff = m.alphaCutoff;
        d.metallic = m.metallic;
        d.roughness = m.roughness;
        d.normalScale = m.normalScale;
        d.emissive = m.emissive;
        draw_.push_back(d);
        slots_.push_back({slot(m.image, true), slot(m.metallicRoughnessImage, false), slot(m.normalImage, false), slot(m.emissiveImage, true)});
    }
}

std::span<const DrawMaterial> ModelPaint::Materials(Renderer& renderer, IWICImagingFactory* wic, TextureCache& cache, double now) {
    for (size_t i = 0; i < draw_.size(); ++i) {
        auto pick = [&](const Slot& sl) -> const GpuTexture* {
            return sl.own ? sl.own.get() : sl.file.empty() ? nullptr : cache.Get(renderer, wic, sl.file, sl.srgb, now);
        };
        draw_[i].texture = pick(slots_[i][kColour]);
        draw_[i].metalRough = pick(slots_[i][kMetalRough]);
        draw_[i].normal = pick(slots_[i][kNormal]);
        draw_[i].emission = pick(slots_[i][kEmission]);
    }
    return draw_;
}

size_t ModelPaint::Pictures() const {
    size_t n = 0;
    for (const auto& slots : slots_) {
        for (const Slot& sl : slots) n += sl.Used() ? 1 : 0;
    }
    return n;
}

}  // namespace rm::space3d
