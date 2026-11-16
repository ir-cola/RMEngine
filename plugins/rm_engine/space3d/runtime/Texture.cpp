#include "rm_engine/space3d/runtime/Texture.h"

#include "base/Log.h"
#include "base/Utf.h"
#include "core/asset/ImportSettings.h"
#include "core/reflect/Data.h"
#include "platform/Image.h"
#include "platform/Shell.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/TextureFile.h"

#include <algorithm>
#include <exception>
#include <format>
#include <fstream>
#include <new>
#include <string>

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
    ++*waiting_;
    job->counter_ = waiting_;
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
            // (The wait also returns true when stopped with jobs still queued: the picture being made is
            // finished, the rest are dropped - not all made while the view that closed waits.)
            if (stop.stop_requested()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        if (job.use_count() > 1) {  // somebody still wants it
            // Nothing thrown may leave the thread (that would end the program): a broken picture
            // that makes the decoding run out of memory is just "cannot be read".
            try {
                Make(*job);
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
        // (no longer waiting when it is taken or let go: Job::Taken)
    }
}

namespace {

constexpr uintmax_t kMaxFileBytes = 512ull << 20;

std::optional<std::vector<uint8_t>> ReadAll(const std::filesystem::path& file) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec || size > kMaxFileBytes) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return std::nullopt;
    return bytes;
}

// The asset id in `file`'s .meta ("" when it has none yet).
std::string GuidOf(const std::filesystem::path& file) {
    std::filesystem::path meta = file;
    meta += L".meta";
    const auto bytes = ReadAll(meta);
    if (!bytes) return {};
    return asset::GuidInMeta({reinterpret_cast<const char*>(bytes->data()), bytes->size()}).value_or(std::string());
}

uint32_t Most(int setting) { return std::min(kMaxTextureSize, static_cast<uint32_t>(std::max(setting, 1))); }

// Into the cache folder: written beside and then put in place (a half-written file is never read).
void WriteCache(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return;
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

const char* FormatName(TextureFormat f) {
    switch (f) {
        case TextureFormat::BC1: return "BC1";
        case TextureFormat::BC3: return "BC3";
        case TextureFormat::BC4: return "BC4";
        case TextureFormat::BC5: return "BC5";
        case TextureFormat::BC7: return "BC7";
        case TextureFormat::Rgba8: break;
    }
    return "RGBA";
}

std::wstring EntryKey(TextureUse use, bool srgb, const std::filesystem::path& file) {
    std::wstring key = std::to_wstring(static_cast<int>(use));
    key += srgb ? L"c:" : L"n:";
    key += file.native();
    return key;
}

}  // namespace

void PictureDecoder::Make(Job& job) {
    const Source& s = job.source_;
    const double start = platform::NowSeconds();
    // The bytes, whose they are, and how they are to be read (the import settings in the .meta).
    std::optional<std::vector<uint8_t>> owned;
    std::span<const uint8_t> data;
    asset::TextureCompression compression = asset::TextureCompression::Auto;
    uint32_t most = kMaxTextureSize;
    std::string settings, id;
    if (s.bytes) {
        data = *s.bytes;
        const auto imp = asset::ReadImport<asset::ModelImport>(s.file);
        compression = imp.textureCompression;
        most = Most(imp.textureMaxSize);
        settings = json::Write(reflect::Save(imp));
        id = GuidOf(s.file) + "#" + std::to_string(s.image);
    } else {
        owned = ReadAll(s.file);
        if (!owned) {
            job.error = std::format("「{}」を開けません (ファイルが無いか、使われているか、大きすぎます)", PathToUtf8(s.file.filename()));
            return;
        }
        data = *owned;
        const auto imp = asset::ReadImport<asset::ImageImport>(s.file);
        compression = imp.compression;
        most = Most(imp.maxSize);
        job.sharp = imp.sharp;
        settings = json::Write(reflect::Save(imp));
        id = GuidOf(s.file);
    }
    // Made before (the same bytes, settings and use): the cache's .dds, as the GPU takes it.
    std::filesystem::path cached;
    if (!s.cacheFolder.empty()) {
        cached = s.cacheFolder / PathFromUtf8(TextureCacheName(id, HashBytes(data), settings, s.use, s.srgb, s.canBC7));
        if (const auto bytes = ReadAll(cached)) {
            std::string ignored;  // a broken cache file is just made again
            if (auto p = ReadDds(*bytes, &ignored)) {
                job.pixels = std::move(*p);
                job.fromCache = true;
                job.seconds = platform::NowSeconds() - start;
                return;
            }
        }
    }
    auto image = platform::DecodeImage(platform::ThreadImagingFactory(), data, platform::PixelOrder::Rgba, {most, most}, &job.error);
    if (!image) return;
    const TextureFormat format = ChooseFormat(s.use, compression, TraitsOf(*image), s.canBC7, image->width, image->height);
    TexturePixels pixels = Compress(MakeTexturePixels(std::move(*image), s.srgb), format);
    if (!cached.empty() && format != TextureFormat::Rgba8) WriteCache(cached, WriteDds(pixels));
    job.pixels = std::move(pixels);
    job.seconds = platform::NowSeconds() - start;
}

// --- TextureCache --------------------------------------------------------------------------------

bool TextureCache::Upload(Renderer& renderer, PictureDecoder::Job& job, double now, TextureRef& out, std::string* error) {
    if (!job.Done()) return false;
    if (!job.pixels) {
        out = nullptr;
        if (error) *error = job.error;
        job.Taken();
        return true;
    }
    if (!slice_.May(now)) return false;  // this frame has copied for long enough: the next one
    const double start = platform::NowSeconds();
    out = renderer.UploadTexture(*job.pixels, error, job.sharp);
    slice_.Spent(platform::NowSeconds() - start);
    job.Taken();
    return true;
}

TextureRef TextureCache::Get(Renderer& renderer, const std::filesystem::path& file, TextureUse use, bool srgb, double now) {
    Entry& e = entries_[EntryKey(use, srgb, file)];
    e.usedAt = now;
    const std::string name = PathToUtf8(file.filename());
    if (e.gpu && e.gpu->madeBy != renderer.Id()) {
        // Made by another renderer (the GPU was made again): never used here - read again.
        e.gpu.reset();
        e.job.reset();
        e.loaded = false;
    }
    TextureRef made;
    std::string error;
    if (e.job && Upload(renderer, *e.job, now, made, &error)) {
        // Made: on the GPU now (or it could not be).
        const bool again = e.gpu != nullptr;
        const bool cached = e.job->fromCache;
        const double seconds = e.job->seconds;
        e.job.reset();
        if (made) {
            e.gpu = std::move(made);
            log::Info("テクスチャ「{}」を{}読みました ({} x {}・{}{}・{:.0f} ms)", name, again ? "もう一度" : "", e.gpu->width, e.gpu->height,
                      FormatName(e.gpu->format), cached ? "・キャッシュから" : "", seconds * 1000);
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
    if (e.loaded && time == e.time) return e.gpu;  // not changed (ready, being made, or could not be read)
    e.loaded = true;
    e.time = time;
    e.told = false;
    // To the thread; the old picture stays until the new one is ready.
    e.job = decoder_.Request({nullptr, file, -1, use, srgb, cacheFolder_, renderer.SupportsBC7()});
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
    return {PictureSlot{m.image, true, TextureUse::Color}, PictureSlot{m.metallicRoughnessImage, false, TextureUse::Data},
            PictureSlot{m.normalImage, false, TextureUse::Normal}, PictureSlot{m.emissiveImage, true, TextureUse::Color}};
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
    modelFile_ = modelFile;
    inside_.clear();
    draw_.clear();
    slots_.clear();
    // Each picture inside the model is made once per use, however many materials use it.
    constexpr size_t kUses = 3;
    std::vector<int> insideOf(mesh.images.size() * kUses, -1);
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> bytes(mesh.images.size());
    auto slot = [&](PictureSlot ps) {
        Slot out;
        out.srgb = ps.srgb;
        out.use = ps.use;
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
        int& at = insideOf[i * kUses + static_cast<size_t>(ps.use)];
        if (at < 0) {
            if (!bytes[i]) bytes[i] = std::make_shared<const std::vector<uint8_t>>(im.bytes);
            at = static_cast<int>(inside_.size());
            inside_.push_back({bytes[i], im.name, ps.image, ps.use, ps.srgb, nullptr, nullptr, false});
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
    // The pictures inside the model: asked for once, put on the GPU when made (and the frame has time).
    for (Inside& in : inside_) {
        if (in.gpu && in.gpu->madeBy != renderer.Id()) in.gpu.reset();  // the GPU was made again: read again
        if (in.gpu || in.failed) continue;
        if (!in.job) {
            in.job = cache.Decoder().Request({in.bytes, modelFile_, in.image, in.use, in.srgb, cache.CacheFolder(), renderer.SupportsBC7()});
            continue;
        }
        std::string error;
        if (!cache.Upload(renderer, *in.job, now, in.gpu, &error)) continue;
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
                                             : cache.Get(renderer, sl.file, sl.use, sl.srgb, now);
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
