#include "rm_engine/space3d/runtime/MeshFile.h"

#include "base/Utf.h"

#include <format>
#include <fstream>

namespace rm::space3d {

namespace {

// More than this is not a model the scene view should try to read whole.
constexpr uintmax_t kMaxFileBytes = 1ull << 30;

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

}  // namespace

std::optional<MeshData> LoadMeshFile(const std::filesystem::path& file, std::string* error) {
    const std::string name = PathToUtf8(file.filename());
    auto bytes = ReadAll(file);
    if (!bytes) {
        if (error) *error = std::format("「{}」を読めませんでした (ファイルが無いか、開けません)", name);
        return std::nullopt;
    }
    const std::filesystem::path folder = file.parent_path();
    const ReadRelated related = [&](std::string_view uri) -> std::optional<std::vector<uint8_t>> {
        const std::filesystem::path rel = PathFromUtf8(uri);
        if (rel.empty() || rel.has_root_name() || rel.has_root_directory()) return std::nullopt;
        for (const auto& part : rel) {
            if (part == L"..") return std::nullopt;  // stays inside the model's folder
        }
        return ReadAll(folder / rel);
    };
    std::string why;
    auto mesh = ParseMesh(PathToUtf8(file.extension()), *bytes, related, &why);
    if (!mesh && error) *error = std::format("「{}」: {}", name, why);
    return mesh;
}

}  // namespace rm::space3d
