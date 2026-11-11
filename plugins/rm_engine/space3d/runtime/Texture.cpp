#include "rm_engine/space3d/runtime/Texture.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "platform/Image.h"
#include "rm_engine/space3d/runtime/MeshFile.h"

#include <algorithm>
#include <exception>
#include <format>
#include <new>

namespace rm::space3d {

// --- PictureDecoder ------------------------------------------------------------------------------

PictureDecoder::~PictureDecoder() {
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
    }
}

std::shared_ptr<PictureDecoder::Job> PictureDecoder::Request(Source source) {
    auto job = std::make_shared<Job>();
    job->source_ = std::move(source);
    ++waiting_;
    {
        std::lock_guard lock(mu_);
        queue_.push_back(job);
    }
    if (!worker_.joinable()) worker_ = std::jthread([this](std::stop_token stop) { Run(stop); });
    wake_.notify_one();
    return job;
}

void PictureDecoder::Run(std::stop_token stop) {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock lock(mu_);
            if (!wake_.wait(lock, stop, [&] { return !queue_.empty(); })) return;  // stopped
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        if (job.use_count() > 1) {  // somebody still wants it
            // Nothing thrown may leave the thread (that would end the program): a broken picture
            // that makes the decoding run out of memory is just "cannot be read".
            try {
                const Source& s = job->source_;
                IWICImagingFactory* wic = platform::ThreadImagingFactory();
                std::optional<platform::Image> image =
                    s.bytes ? platform::DecodeImage(wic, *s.bytes, platform::PixelOrder::Rgba, {s.maxSize, s.maxSize}, &job->error)
                            : platform::LoadImageFile(wic, s.file, platform::PixelOrder::Rgba, {s.maxSize, s.maxSize}, &job->error);
                if (image) job->pixels = MakeTexturePixels(std::move(*image), s.srgb);
            } catch (const std::bad_alloc&) {
                job->pixels.reset();
                job->error = "メモリーが足りなくなりました (壊れているか、大きすぎる画像)";
            } catch (const std::exception& ex) {
                job->pixels.reset();
                job->error = std::format("読んでいる途中で止まりました ({})", ex.what());
            } catch (...) {
                job->pixels.reset();
                job->error = "読んでいる途中で止まりました";
            }
            job->done_.store(true, std::memory_order_release);
        }
        --waiting_;
    }
}

// --- TextureCache --------------------------------------------------------------------------------

namespace {

size_t BytesOf(const TexturePixels& p) {
    size_t n = 0;
    for (const platform::Image& l : p.levels) n += l.pixels.size();
    return n;
}

}  // namespace

bool TextureCache::MayUpload(size_t bytes, double now) {
    if (now != frame_) {
        frame_ = now;
        uploaded_ = 0;
        deferredBefore_ = deferred_;
        deferred_ = 0;
    }
    if (uploaded_ > 0 && uploaded_ + bytes > kUploadBytesPerFrame) {
        ++deferred_;
        return false;
    }
    uploaded_ += bytes;
    return true;
}

TextureRef TextureCache::Get(Renderer& renderer, const std::filesystem::path& file, bool srgb, double now) {
    Entry& e = entries_[(srgb ? L"c:" : L"n:") + file.native()];
    e.usedAt = now;
    const std::string name = PathToUtf8(file.filename());
    if (e.gpu && e.gpu->madeBy != renderer.Id()) {
        // Made by another renderer (the GPU was made again): never used here - read again.
        e.gpu.reset();
        e.job.reset();
        e.loaded = false;
    }
    if (e.job && e.job->Done() && (!e.job->pixels || MayUpload(BytesOf(*e.job->pixels), now))) {
        // Decoded: only the copy to the GPU is left (put off to a later frame when this one has copied enough).
        std::string error = e.job->error;
        TextureRef gpu = e.job->pixels ? renderer.UploadTexture(*e.job->pixels, &error, e.sharp) : nullptr;
        const bool again = e.gpu != nullptr;
        e.job.reset();
        if (gpu) {
            e.gpu = std::move(gpu);
            log::Info("テクスチャ「{}」を{}読みました ({} x {})", name, again ? "もう一度" : "", e.gpu->width, e.gpu->height);
        } else {
            e.gpu.reset();
            log::Warn("テクスチャ「{}」を読めないので、その絵なしで描きます: {}", name, error);
        }
    }
    if (e.loaded && now - e.checkedAt < kCheckSeconds) return e.gpu;
    e.checkedAt = now;
    std::error_code ec;
    const auto time = asset::ChangedAt(file, ec);  // the picture or its import settings (.meta)
    if (ec) {
        e.gpu.reset();
        e.job.reset();
        e.loaded = true;
        e.time = {};
        if (!e.told) log::Warn("テクスチャ「{}」が見つからないので、その絵なしで描きます", name);
        e.told = true;
        return nullptr;
    }
    if (e.loaded && time == e.time) return e.gpu;  // not changed (ready, being read, or could not be read)
    e.loaded = true;
    e.time = time;
    e.told = false;
    // To the decoder; the old picture stays until the new one is ready.
    const auto imp = asset::ReadImport<asset::ImageImport>(file);
    e.sharp = imp.sharp;
    const uint32_t most = std::min(kMaxTextureSize, static_cast<uint32_t>(std::max(imp.maxSize, 1)));
    e.job = decoder_.Request({nullptr, file, srgb, most});
    return e.gpu;
}

void TextureCache::ForgetUnused(double now, double seconds) {
    std::erase_if(entries_, [&](const auto& kv) { return now - kv.second.usedAt > seconds; });
}

// --- Materials -----------------------------------------------------------------------------------

DrawMaterial MaterialNumbers(const MeshMaterial& m) {
    DrawMaterial d;
    d.color = m.baseColor;
    d.alpha = m.alpha;
    d.alphaCutoff = m.alphaCutoff;
    d.metallic = m.metallic;
    d.roughness = m.roughness;
    d.normalScale = m.normalScale;
    d.emissive = m.emissive;
    return d;
}

std::array<PictureSlot, kPictureSlots> PictureSlots(const MeshMaterial& m) {
    return {PictureSlot{m.image, true}, PictureSlot{m.metallicRoughnessImage, false}, PictureSlot{m.normalImage, false},
            PictureSlot{m.emissiveImage, true}};
}

void SetPicture(DrawMaterial& d, size_t slot, TextureRef texture) {
    switch (slot) {
        case 0: d.texture = std::move(texture); break;
        case 1: d.metalRough = std::move(texture); break;
        case 2: d.normal = std::move(texture); break;
        default: d.emission = std::move(texture); break;
    }
}

void ModelPaint::Load(const MeshData& mesh, const std::filesystem::path& modelFile, const std::filesystem::path& root,
                      const std::string& modelName) {
    name_ = modelName;
    inside_.clear();
    draw_.clear();
    slots_.clear();
    // Each picture inside the model is decoded once per way it is read, however many materials use it.
    std::vector<int> insideOf(mesh.images.size() * 2, -1);
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> bytes(mesh.images.size());
    auto slot = [&](PictureSlot ps) {
        Slot out;
        out.srgb = ps.srgb;
        if (ps.image < 0 || static_cast<size_t>(ps.image) >= mesh.images.size()) return out;
        const size_t i = static_cast<size_t>(ps.image);
        const MeshImage& im = mesh.images[i];
        if (!im.file.empty()) {
            out.file = RelatedFile(modelFile, im.file, root);
            if (out.file.empty()) {
                log::Warn("モデル「{}」: テクスチャ「{}」はプロジェクトの Assets の外にあるので読みません (その絵なしで描きます)", modelName, im.file);
            }
            return out;
        }
        int& at = insideOf[i * 2 + (ps.srgb ? 1 : 0)];
        if (at < 0) {
            if (!bytes[i]) bytes[i] = std::make_shared<const std::vector<uint8_t>>(im.bytes);
            at = static_cast<int>(inside_.size());
            inside_.push_back({bytes[i], im.name, ps.srgb, nullptr, nullptr, false});
        }
        out.inside = at;
        return out;
    };
    for (const MeshMaterial& m : mesh.materials) {
        draw_.push_back(MaterialNumbers(m));
        std::array<Slot, kPictureSlots> slots;
        const auto pictures = PictureSlots(m);
        for (size_t k = 0; k < kPictureSlots; ++k) slots[k] = slot(pictures[k]);
        slots_.push_back(std::move(slots));
    }
}

std::span<const DrawMaterial> ModelPaint::Materials(Renderer& renderer, TextureCache& cache, double now) {
    // The pictures inside the model: asked for once, put on the GPU when decoded (and the frame has room).
    for (Inside& in : inside_) {
        if (in.gpu && in.gpu->madeBy != renderer.Id()) in.gpu.reset();  // the GPU was made again: read again
        if (in.gpu || in.failed) continue;
        if (!in.job) {
            in.job = cache.Decoder().Request({in.bytes, {}, in.srgb, kMaxTextureSize});
            continue;
        }
        if (!in.job->Done() || (in.job->pixels && !cache.MayUpload(BytesOf(*in.job->pixels), now))) continue;
        std::string error = in.job->error;
        if (in.job->pixels) in.gpu = renderer.UploadTexture(*in.job->pixels, &error);
        in.job.reset();
        if (!in.gpu) {
            in.failed = true;
            log::Warn("モデル「{}」: 中の画像「{}」を読めないので、その絵なしで描きます: {}", name_, in.name, error);
        }
    }
    for (size_t i = 0; i < draw_.size(); ++i) {
        for (size_t k = 0; k < kPictureSlots; ++k) {
            const Slot& sl = slots_[i][k];
            TextureRef t = sl.inside >= 0 ? inside_[static_cast<size_t>(sl.inside)].gpu
                           : sl.file.empty() ? nullptr
                                             : cache.Get(renderer, sl.file, sl.srgb, now);
            SetPicture(draw_[i], k, std::move(t));
        }
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
