#pragma once
// 3D 空間 - the editor part that is not only a library: a 3D model drawn small for the アセット
// panel's thumbnails (offered at the core's editor::kThumbnailPoint - the core knows nothing of 3D).
//
// The model is read like the scene view reads it (shape, materials, pictures) - on the thumbnails'
// worker thread (PrepareModelThumbnail: the file and its pictures decoded) - then drawn on the UI
// thread with the same Renderer into a picture of its own - seen from the front, a little from the
// right and above, framed by its bounds, see-through around it - and read back from the GPU
// (DrawModelThumbnail).

#include "editor/plugin/Software.h"
#include "platform/Image.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace rm::space3d {

void RegisterEditor(editor::PluginRegistry& registry);

// Any thread: `file` read and its pictures decoded (pictures in files only from inside `root`).
std::shared_ptr<void> PrepareModelThumbnail(const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                                            std::string* error);
// The UI thread: what Prepare made, drawn into size x size pixels (premultiplied BGRA).
bool DrawModelThumbnail(platform::Gfx& gfx, const void* prepared, uint32_t size, platform::Image& out, std::string* error);
// Both at once (on the UI thread).
bool MakeModelThumbnail(platform::Gfx& gfx, const std::filesystem::path& file, const std::filesystem::path& root, uint32_t size,
                        platform::Image& out, std::string* error);

}  // namespace rm::space3d
