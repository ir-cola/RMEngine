#pragma once
// 3D 空間 - the pictures models are painted with, on the GPU.
//
//   PictureDecoder a thread of its own that does the slow part of a picture: reads the file, looks
//                  in the disk cache (TextureFile.h: <project>/Cache/Textures/*.dds); when it is not
//                  there, decodes it (WIC), makes its mipmaps (MakeTexturePixels), compresses them
//                  (BlockCompress.h: BC1 / BC3 / BC4 / BC5 / BC7 by its use and the import
//                  settings) and writes the cache file. Only the copy to the GPU (UploadTexture) is
//                  on the drawing thread, so a model with big pictures never stops the screen
//   TextureCache   a picture file by its full path: read and put on the GPU the first time it is
//                  asked for, shared by every model that uses it, looked at again now and then and
//                  read again when the file (or its .meta) changed - the old picture stays until the
//                  new one is ready
//   ModelPaint     one model's materials as Renderer::Render takes them (DrawMaterial): the pictures
//                  written inside the model (.glb / data: URI) are decoded by the cache's decoder (the
//                  model's import settings: 中の絵の圧縮 / 大きさの上限); the pictures in files next to it
//                  come from the cache
//
// Until a picture is ready its material is drawn without it (the drawer asks for more frames while
// TextureCache::Waiting). The copies to the GPU of one frame stop when they have taken
// kUploadSecondsPerFrame (measured with QueryPerformanceCounter; the first copy of a frame always
// goes): the rest go on the next frames. A picture made by another renderer (the GPU was made again)
// is never used: it is read again. A picture that cannot be read is told once in the console
// (Japanese, with why) and the material is drawn without it. Pictures bigger than kMaxTextureSize (or
// the import setting) are made smaller.

#include "rm_engine/space3d/runtime/BlockCompress.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/UploadSlice.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rm::space3d {

constexpr uint32_t kMaxTextureSize = 4096;
// How long one frame may spend copying pictures to the GPU (the one place this number is written).
inline constexpr double kUploadSecondsPerFrame = 0.002;

// A material's numbers as Render takes them (its pictures not yet: SetPicture).
DrawMaterial MaterialNumbers(const MeshMaterial& m);
// Its pictures: which of MeshData::images (-1: none) goes into which slot, whether it is a colour
// picture (sRGB) and what it is used for. Slots: 0 colour, 1 metal / rough, 2 normal, 3 emission.
struct PictureSlot {
    int image = -1;
    bool srgb = true;
    TextureUse use = TextureUse::Color;
};
constexpr size_t kPictureSlots = 4;
std::array<PictureSlot, kPictureSlots> PictureSlots(const MeshMaterial& m);
void SetPicture(DrawMaterial& d, size_t slot, TextureRef texture);

class PictureDecoder {
public:
    // What to make: a picture file, or bytes (a picture written inside a model - `file` is then the
    // model: its .meta gives the id and the import settings, `image` which of its pictures).
    struct Source {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        std::filesystem::path file;
        int image = -1;
        TextureUse use = TextureUse::Color;
        bool srgb = true;
        std::filesystem::path cacheFolder;  // empty: nothing kept on disk
        bool canBC7 = false;
    };
    // One picture asked for. Shared with the thread: once Done(), the rest is the answer.
    // Dropped by whoever asked before it was started: never made.
    class Job {
    public:
        bool Done() const { return done_.load(std::memory_order_acquire); }
        std::optional<TexturePixels> pixels;  // nullopt: `error` says why (Japanese)
        std::string error;
        bool sharp = false;      // the image's import setting (くっきり)
        bool fromCache = false;  // read from the cache's .dds (not decoded and compressed again)
        double seconds = 0;      // how long the thread took

    private:
        friend class PictureDecoder;
        Source source_;
        std::atomic<bool> done_{false};
    };

    PictureDecoder() = default;
    ~PictureDecoder();  // the picture being made is finished; the rest are dropped
    PictureDecoder(const PictureDecoder&) = delete;
    PictureDecoder& operator=(const PictureDecoder&) = delete;

    std::shared_ptr<Job> Request(Source source);  // the thread starts with the first one
    size_t Waiting() const { return waiting_.load(); }  // asked for and not done

private:
    void Run(std::stop_token stop);
    static void Make(Job& job);

    std::mutex mu_;
    std::condition_variable_any wake_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::atomic<size_t> waiting_{0};
    std::jthread worker_;  // last: stopped and joined first
};

class TextureCache {
public:
    static constexpr double kCheckSeconds = 1.0;  // how often a file is looked at again

    // Where compressed pictures are kept (the project's Cache/Textures; empty: not kept).
    void SetCacheFolder(std::filesystem::path folder) { cacheFolder_ = std::move(folder); }
    const std::filesystem::path& CacheFolder() const { return cacheFolder_; }

    // The picture of `file` (a full path) for `use` (colours are read as sRGB) - kept apart by use;
    // null when it is not ready yet or cannot be read (told once - again only after the file changed).
    // `now`: seconds, any clock that goes forward - a new value is a new frame. Call it every frame
    // the picture is used.
    TextureRef Get(Renderer& renderer, const std::filesystem::path& file, TextureUse use, bool srgb, double now);
    // Puts a finished job on the GPU if the frame `now` still has time; true when it was taken (made,
    // or failed - `out` null and *error why).
    bool Upload(Renderer& renderer, PictureDecoder::Job& job, double now, TextureRef& out, std::string* error);
    // Pictures nobody asked for in `seconds` leave the GPU.
    void ForgetUnused(double now, double seconds);
    // Everything goes (the GPU's devices were made again: read again when asked).
    void Clear() { entries_.clear(); }
    size_t Size() const { return entries_.size(); }
    // Pictures being made or waiting for their copy to the GPU (draw again until 0).
    size_t Waiting() const { return decoder_.Waiting() + slice_.Deferred(); }
    PictureDecoder& Decoder() { return decoder_; }

private:
    struct Entry {
        std::shared_ptr<const GpuTexture> gpu;
        std::shared_ptr<PictureDecoder::Job> job;  // being made
        std::filesystem::file_time_type time{};
        bool loaded = false;  // read (or asked for) as it is at `time`
        bool told = false;    // that it is missing was written to the console
        double checkedAt = 0, usedAt = 0;
    };
    std::unordered_map<std::wstring, Entry> entries_;
    std::filesystem::path cacheFolder_;
    UploadSlice slice_{kUploadSecondsPerFrame};
    PictureDecoder decoder_;  // last: its thread stops first
};

class ModelPaint {
public:
    // The model's materials: pictures inside it are kept to be decoded (by the cache's decoder, the
    // first time Materials is called); pictures in files are looked for next to `modelFile`, only
    // inside `root` (the project's Assets/). What could not be used is written to the console
    // (`modelName`: for the message).
    void Load(const MeshData& mesh, const std::filesystem::path& modelFile, const std::filesystem::path& root, const std::string& modelName);
    // The materials as Render takes them (by MeshPart::material); pictures as they get ready.
    std::span<const DrawMaterial> Materials(Renderer& renderer, TextureCache& cache, double now);
    size_t Pictures() const;  // how many pictures the materials use (for the console)

private:
    // A picture written inside the model, once per way it is read (its bytes kept: read again if the
    // GPU is made again).
    struct Inside {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        std::string name;
        int image = -1;
        TextureUse use = TextureUse::Color;
        bool srgb = true;
        std::shared_ptr<PictureDecoder::Job> job;
        TextureRef gpu;
        bool failed = false;
    };
    // One picture of a material: inside the model (an index into inside_) or a file (from the cache).
    struct Slot {
        int inside = -1;
        std::filesystem::path file;
        TextureUse use = TextureUse::Color;
        bool srgb = true;
        bool Used() const { return inside >= 0 || !file.empty(); }
    };
    std::string name_;
    std::filesystem::path modelFile_;
    std::vector<Inside> inside_;
    std::vector<DrawMaterial> draw_;
    std::vector<std::array<Slot, kPictureSlots>> slots_;  // per material
};

}  // namespace rm::space3d
