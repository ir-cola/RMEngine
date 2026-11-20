#include "rm_engine/space3d/runtime/MeshFile.h"

#include "base/Utf.h"
#include "core/asset/ImportSettings.h"

#include <format>
#include <fstream>

namespace rm::space3d {

namespace {

// これより大きいものは、シーンビューが丸ごと読もうとするモデルではない。
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

std::optional<MeshData> Load(const std::filesystem::path& file, const std::string& name, std::string* error, ReadBudget& budget) {
    // 読んでいる間、ファイルそのものはメモリに載っている。
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(file, ec);
    std::string why;
    if (!ec && !budget.Take(0, size, &why)) {
        if (error) *error = std::format("「{}」: {}", name, why);
        return std::nullopt;
    }
    auto bytes = ReadAll(file);
    if (!bytes) {
        if (error) *error = std::format("「{}」を読めませんでした (ファイルが無いか、開けません)", name);
        return std::nullopt;
    }
    const ReadRelated related = [&](std::string_view uri) -> std::optional<std::vector<uint8_t>> {
        const std::filesystem::path at = RelatedFile(file, uri, file.parent_path());  // モデルのフォルダの中に収まる
        if (at.empty()) return std::nullopt;
        return ReadAll(at);
    };
    const auto imp = asset::ReadImport<asset::ModelImport>(file);
    auto mesh = ParseMesh(PathToUtf8(file.extension()), *bytes, related, &why, &budget, imp.axes == asset::ModelAxes::Auto,
                          imp.unit == asset::ModelUnit::Auto);
    if (!mesh && error) *error = std::format("「{}」: {}", name, why);
    if (mesh) ApplyModelImport(*mesh, BasisOf(imp), imp.turnAround, UnitOf(imp.unit) * imp.scale);
    return mesh;
}

}  // namespace

ModelBasis BasisOf(const asset::ModelImport& imp) {
    using asset::ModelAxes;
    switch (imp.axes) {
        case ModelAxes::Auto:  // (glTF・OBJ は Y が上・右手系、FBX は読むときにファイルの軸で Y が上・右手系にしてある)
        case ModelAxes::YUpRight: return {{0, 1, 0}, {0, 0, -1}, false};
        case ModelAxes::ZUpRight: return {{0, 0, 1}, {0, 1, 0}, false};
        case ModelAxes::ZUpLeft: return {{0, 0, 1}, {1, 0, 0}, true};
        case ModelAxes::YUpLeft: return {{0, 1, 0}, {0, 0, 1}, true};
        case ModelAxes::Custom: {
            auto dir = [](asset::AxisDir a) -> Vec3 {
                switch (a) {
                    case asset::AxisDir::PosX: return {1, 0, 0};
                    case asset::AxisDir::NegX: return {-1, 0, 0};
                    case asset::AxisDir::PosY: return {0, 1, 0};
                    case asset::AxisDir::NegY: return {0, -1, 0};
                    case asset::AxisDir::PosZ: return {0, 0, 1};
                    case asset::AxisDir::NegZ: return {0, 0, -1};
                }
                return {0, 1, 0};
            };
            return {dir(imp.upAxis), dir(imp.depthAxis), imp.leftHanded};
        }
    }
    return {};
}

float UnitOf(asset::ModelUnit unit) {
    switch (unit) {
        case asset::ModelUnit::Centimeter: return 0.01f;
        case asset::ModelUnit::Millimeter: return 0.001f;
        default: return 1.f;  // (自動: FBX は読むときにファイルの単位で m にしてある)
    }
}

std::optional<MeshData> LoadMeshFile(const std::filesystem::path& file, std::string* error, ReadBudget* budget) {
    const std::string name = PathToUtf8(file.filename());
    ReadBudget own;
    try {
        return Load(file, name, error, budget ? *budget : own);
    } catch (const std::bad_alloc&) {
        if (error) *error = std::format("「{}」: メモリーが足りないので読めません", name);
    } catch (const std::exception& e) {
        if (error) *error = std::format("「{}」: 読み込み中に問題が起きました ({})", name, e.what());
    }
    return std::nullopt;
}

std::filesystem::path RelatedFile(const std::filesystem::path& model, std::string_view relative, const std::filesystem::path& root) {
    const std::filesystem::path rel = PathFromUtf8(relative);
    if (rel.empty() || rel.has_root_name() || rel.has_root_directory()) return {};
    const std::filesystem::path at = (model.parent_path() / rel).lexically_normal();
    const std::filesystem::path inside = at.lexically_relative(root.lexically_normal());
    if (inside.empty() || *inside.begin() == L".." || inside == L".") return {};
    return at;
}

}  // namespace rm::space3d
