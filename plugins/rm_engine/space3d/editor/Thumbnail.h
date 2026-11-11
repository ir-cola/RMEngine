#pragma once
// 3D 空間 - the editor part that is not only a library: a 3D model drawn small for the アセット
// panel's thumbnails (offered at the core's editor::kThumbnailPoint - the core knows nothing of 3D).
//
// The model is read like the scene view reads it (shape, materials, pictures), drawn with the same
// Renderer into a picture of its own - seen from the front, a little from the right and above,
// framed by its bounds, see-through around it - and read back from the GPU.

#include "editor/plugin/Software.h"
#include "platform/Image.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace rm::space3d {

void RegisterEditor(editor::PluginRegistry& registry);

// `file` drawn into size x size pixels (premultiplied BGRA). Pictures only from inside `root`.
bool MakeModelThumbnail(platform::Gfx& gfx, const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                        platform::Image& out, std::string* error);

}  // namespace rm::space3d
