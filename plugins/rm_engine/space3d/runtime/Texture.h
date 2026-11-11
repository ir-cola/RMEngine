#pragma once
// 3D 空間 - the pictures models are painted with, on the GPU.
//
//   PictureDecoder a thread of its own that decodes pictures (WIC) and makes their mipmaps
//                  (MakeTexturePixels): the slow part. Only the copy to the GPU (UploadTexture) is on
//                  the drawing thread, so a model with big pictures never stops the screen
//   TextureCache   a picture file by its full path: read and put on the GPU the first time it is
//                  asked for, shared by every model that uses it, looked at again now and then and
//                  read again when the file (or its .meta) changed - the old picture stays until the
//                  new one is ready
//   ModelPaint     one model's materials as Renderer::Render takes them (DrawMaterial): the pictures
//                  written inside the model (.glb / data: URI) are decoded by the cache's decoder;
//                  the pictures in files next to it come from the cache
//
// Until a picture is ready its material is drawn without it (the drawer asks for more frames while
// TextureCache::Waiting). A picture that cannot be read is told once in the console (Japanese, with
// why) and the material is drawn without it. Pictures bigger than kMaxTextureSize (or the image's
// import setting) are made smaller.

#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"

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

// A material's numbers as Render takes them (its pictures not yet: SetPicture).
DrawMaterial MaterialNumbers(const MeshMaterial& m);
// Its pictures: which of MeshData::images (-1: none) goes into which slot, and whether it is a colour
// picture (sRGB). Slots: 0 colour, 1 metal / rough, 2 normal, 3 emission.
struct PictureSlot {
    int image = -1;
    bool srgb = true;
};
constexpr size_t kPictureSlots = 4;
std::array<PictureSlot, kPictureSlots> PictureSlots(const MeshMaterial& m);
void SetPicture(DrawMaterial& d, size_t slot, const GpuTexture* texture);

class PictureDecoder {
public:
    // What to decode: bytes (a picture written inside a model) or a file.
    struct Source {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        std::filesystem::path file;
        bool srgb = true;
        uint32_t maxSize = kMaxTextureSize;
    };
    // One picture asked for. Shared with the thread: once Done(), `pixels` / `error` are the answer.
    // Dropped by whoever asked before it was started: never decoded.
    class Job {
    public:
        bool Done() const { return done_.load(std::memory_order_acquire); }
        std::optional<TexturePixels> pixels;  // nullopt: `error` says why (Japanese)
        std::string error;

    private:
        friend class PictureDecoder;
        Source source_;
        std::atomic<bool> done_{false};
    };

    PictureDecoder() = default;
    ~PictureDecoder();  // the picture being decoded is finished; the rest are dropped
    PictureDecoder(const PictureDecoder&) = delete;
    PictureDecoder& operator=(const PictureDecoder&) = delete;

    std::shared_ptr<Job> Request(Source source);  // the thread starts with the first one
    size_t Waiting() const { return waiting_.load(); }  // asked for and not done

private:
    void Run(std::stop_token stop);

    std::mutex mu_;
    std::condition_variable_any wake_;
    std::deque<std::shared_ptr<Job>> queue_;
    std::atomic<size_t> waiting_{0};
    std::jthread worker_;  // last: stopped and joined first
};

class TextureCache {
public:
    static constexpr double kCheckSeconds = 1.0;  // how often a file is looked at again

    // The picture of `file` (a full path) as colours (srgb) or numbers - kept apart; null when it is
    // not ready yet or cannot be read (told once - again only after the file changed). `now`:
    // seconds, any clock that goes forward. Call it every frame the picture is used.
    const GpuTexture* Get(Renderer& renderer, const std::filesystem::path& file, bool srgb, double now);
    // Pictures nobody asked for in `seconds` leave the GPU.
    void ForgetUnused(double now, double seconds);
    // Everything goes (the GPU's devices were made again: read again when asked).
    void Clear() { entries_.clear(); }
    size_t Size() const { return entries_.size(); }
    // Pictures being decoded (draw again until 0).
    size_t Waiting() const { return decoder_.Waiting(); }
    PictureDecoder& Decoder() { return decoder_; }

private:
    struct Entry {
        std::shared_ptr<const GpuTexture> gpu;
        std::shared_ptr<PictureDecoder::Job> job;  // being decoded
        bool sharp = false;
        std::filesystem::file_time_type time{};
        bool loaded = false;  // read (or asked for) as it is at `time`
        bool told = false;    // that it is missing was written to the console
        double checkedAt = 0, usedAt = 0;
    };
    std::unordered_map<std::wstring, Entry> entries_;
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
    // A picture written inside the model, once per way it is read.
    struct Inside {
        std::shared_ptr<const std::vector<uint8_t>> bytes;
        std::string name;
        bool srgb = true;
        std::shared_ptr<PictureDecoder::Job> job;
        std::shared_ptr<const GpuTexture> gpu;
        bool failed = false;
    };
    // One picture of a material: inside the model (an index into inside_) or a file (from the cache).
    struct Slot {
        int inside = -1;
        std::filesystem::path file;
        bool srgb = true;
        bool Used() const { return inside >= 0 || !file.empty(); }
    };
    std::string name_;
    std::vector<Inside> inside_;
    std::vector<DrawMaterial> draw_;
    std::vector<std::array<Slot, kPictureSlots>> slots_;  // per material
};

}  // namespace rm::space3d
