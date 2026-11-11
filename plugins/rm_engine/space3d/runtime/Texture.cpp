#include "rm_engine/space3d/runtime/Texture.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "platform/Image.h"
#include "rm_engine/space3d/runtime/MeshFile.h"

namespace rm::space3d {

std::shared_ptr<const GpuTexture> LoadTexture(Renderer& renderer, IWICImagingFactory* wic, std::span<const uint8_t> bytes, std::string* error) {
    const auto image = platform::DecodeImage(wic, bytes, platform::PixelOrder::Rgba, {kMaxTextureSize, kMaxTextureSize}, error);
    return image ? renderer.UploadTexture(*image, error) : nullptr;
}

const GpuTexture* TextureCache::Get(Renderer& renderer, IWICImagingFactory* wic, const std::filesystem::path& file, double now) {
    Entry& e = entries_[file.native()];
    e.usedAt = now;
    if (e.loaded && now - e.checkedAt < kCheckSeconds) return e.gpu.get();
    e.checkedAt = now;
    const std::string name = PathToUtf8(file.filename());
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(file, ec);
    if (ec) {
        e.gpu.reset();
        e.loaded = true;
        e.time = {};
        if (!e.told) log::Warn("テクスチャ「{}」が見つからないので、材質の色だけで描きます", name);
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
    const auto image = platform::LoadImageFile(wic, file, platform::PixelOrder::Rgba, {kMaxTextureSize, kMaxTextureSize}, &error);
    e.gpu = image ? renderer.UploadTexture(*image, &error) : nullptr;
    if (e.gpu) {
        log::Info("テクスチャ「{}」を{}読みました ({} x {})", name, again ? "もう一度" : "", e.gpu->width, e.gpu->height);
    } else {
        log::Warn("テクスチャ「{}」を読めないので、材質の色だけで描きます: {}", name, error);
    }
    return e.gpu.get();
}

void TextureCache::ForgetUnused(double now, double seconds) {
    std::erase_if(entries_, [&](const auto& kv) { return now - kv.second.usedAt > seconds; });
}

void ModelPaint::Load(Renderer& renderer, IWICImagingFactory* wic, const MeshData& mesh, const std::filesystem::path& modelFile,
                      const std::filesystem::path& root, const std::string& modelName) {
    draw_.clear();
    own_.clear();
    files_.clear();
    // Each picture inside the model goes to the GPU once, however many materials use it.
    std::vector<std::shared_ptr<const GpuTexture>> inside(mesh.images.size());
    std::vector<bool> tried(mesh.images.size(), false);
    for (const MeshMaterial& m : mesh.materials) {
        DrawMaterial d;
        d.color = m.baseColor;
        d.alpha = m.alpha;
        d.alphaCutoff = m.alphaCutoff;
        draw_.push_back(d);
        own_.emplace_back();
        files_.emplace_back();
        if (m.image < 0 || static_cast<size_t>(m.image) >= mesh.images.size()) continue;
        const size_t k = static_cast<size_t>(m.image);
        const MeshImage& im = mesh.images[k];
        if (!im.file.empty()) {
            files_.back() = RelatedFile(modelFile, im.file, root);
            if (files_.back().empty()) {
                log::Warn("モデル「{}」: テクスチャ「{}」はプロジェクトの Assets の外にあるので読みません (材質の色だけで描きます)", modelName, im.file);
            }
            continue;
        }
        if (!tried[k]) {
            tried[k] = true;
            std::string error;
            inside[k] = LoadTexture(renderer, wic, im.bytes, &error);
            if (!inside[k]) log::Warn("モデル「{}」: 中の画像「{}」を読めないので、材質の色だけで描きます: {}", modelName, im.name, error);
        }
        own_.back() = inside[k];
    }
}

std::span<const DrawMaterial> ModelPaint::Materials(Renderer& renderer, IWICImagingFactory* wic, TextureCache& cache, double now) {
    for (size_t i = 0; i < draw_.size(); ++i) {
        draw_[i].texture = own_[i] ? own_[i].get() : files_[i].empty() ? nullptr : cache.Get(renderer, wic, files_[i], now);
    }
    return draw_;
}

size_t ModelPaint::Pictures() const {
    size_t n = 0;
    for (size_t i = 0; i < draw_.size(); ++i) n += (own_[i] || !files_[i].empty()) ? 1 : 0;
    return n;
}

}  // namespace rm::space3d
