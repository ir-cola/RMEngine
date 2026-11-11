#pragma once
// 3D 空間 - the pictures models are painted with, on the GPU.
//
//   TextureCache   a picture file by its full path: read (WIC) and put on the GPU the first time it
//                  is asked for, shared by every model that uses it, looked at again now and then
//                  and read again when the file changed (painted in a paint tool and saved)
//   ModelPaint     one model's materials as Renderer::Render takes them (DrawMaterial): the pictures
//                  written inside the model (.glb / data: URI) are put on the GPU with the model; the
//                  pictures in files next to it come from the cache
//
// A picture that cannot be read is told once in the console (Japanese, with why) and the material
// is drawn with its colour only. Pictures bigger than kMaxTextureSize are made smaller.

#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"

#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct IWICImagingFactory;

namespace rm::space3d {

constexpr uint32_t kMaxTextureSize = 4096;

// Decodes a picture's bytes and puts it on the GPU (srgb: a colour picture - see UploadTexture); null
// (and *error, Japanese) when it cannot.
std::shared_ptr<const GpuTexture> LoadTexture(Renderer& renderer, IWICImagingFactory* wic, std::span<const uint8_t> bytes, bool srgb,
                                              std::string* error);

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

class TextureCache {
public:
    static constexpr double kCheckSeconds = 1.0;  // how often a file is looked at again

    // The picture of `file` (a full path) as colours (srgb) or numbers - kept apart; null when it
    // cannot be read (told once - again only after the file changed). `now`: seconds, any clock that
    // goes forward.
    const GpuTexture* Get(Renderer& renderer, IWICImagingFactory* wic, const std::filesystem::path& file, bool srgb, double now);
    // Pictures nobody asked for in `seconds` leave the GPU.
    void ForgetUnused(double now, double seconds);
    size_t Size() const { return entries_.size(); }

private:
    struct Entry {
        std::shared_ptr<const GpuTexture> gpu;
        std::filesystem::file_time_type time{};
        bool loaded = false;  // read (or tried) as it is at `time`
        bool told = false;    // that it is missing was written to the console
        double checkedAt = 0, usedAt = 0;
    };
    std::unordered_map<std::wstring, Entry> entries_;
};

class ModelPaint {
public:
    // The model's materials: pictures inside it go to the GPU now; pictures in files are looked for
    // next to `modelFile`, only inside `root` (the project's Assets/). What could not be used is
    // written to the console (`modelName`: for the message).
    void Load(Renderer& renderer, IWICImagingFactory* wic, const MeshData& mesh, const std::filesystem::path& modelFile,
              const std::filesystem::path& root, const std::string& modelName);
    // The materials as Render takes them (by MeshPart::material); the files' pictures from `cache`.
    std::span<const DrawMaterial> Materials(Renderer& renderer, IWICImagingFactory* wic, TextureCache& cache, double now);
    size_t Pictures() const;  // how many pictures the materials use (for the console)

private:
    // One picture of a material: written inside the model (own) or a file (from the cache).
    struct Slot {
        std::shared_ptr<const GpuTexture> own;
        std::filesystem::path file;
        bool srgb = true;
        bool Used() const { return own || !file.empty(); }
    };
    std::vector<DrawMaterial> draw_;
    std::vector<std::array<Slot, kPictureSlots>> slots_;  // per material
};

}  // namespace rm::space3d
