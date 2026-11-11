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

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

struct IWICImagingFactory;

namespace rm::space3d {

constexpr uint32_t kMaxTextureSize = 4096;

// Decodes a picture's bytes and puts it on the GPU; null (and *error, Japanese) when it cannot.
std::shared_ptr<const GpuTexture> LoadTexture(Renderer& renderer, IWICImagingFactory* wic, std::span<const uint8_t> bytes, std::string* error);

class TextureCache {
public:
    static constexpr double kCheckSeconds = 1.0;  // how often a file is looked at again

    // The picture of `file` (a full path); null when it cannot be read (told once - again only after
    // the file changed). `now`: seconds, any clock that goes forward.
    const GpuTexture* Get(Renderer& renderer, IWICImagingFactory* wic, const std::filesystem::path& file, double now);
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
    size_t Pictures() const;  // how many materials have a picture (for the console)

private:
    std::vector<DrawMaterial> draw_;
    std::vector<std::shared_ptr<const GpuTexture>> own_;  // per material: a picture written inside the model
    std::vector<std::filesystem::path> files_;            // per material: a picture file (empty: none)
};

}  // namespace rm::space3d
