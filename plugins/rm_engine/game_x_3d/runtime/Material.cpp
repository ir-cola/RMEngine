#include "rm_engine/game_x_3d/runtime/Material.h"

#include "base/Json.h"

#include <algorithm>
#include <format>

namespace rm::game_x_3d {

std::optional<Material> ReadMaterial(std::string_view bytes, std::string* error, reflect::Unknowns* unknowns) {
    json::ParseError pe;
    const auto v = json::Parse(bytes, &pe);
    if (!v) {
        if (error) *error = std::format("JSON として読めません ({} 行目: {})", pe.line, pe.message);
        return std::nullopt;
    }
    if (!v->IsObject() || v->Get("kind").AsString() != kMaterialKind) {
        if (error) *error = "マテリアルのファイルではありません";
        return std::nullopt;
    }
    Material m;
    reflect::Unknowns u;
    reflect::Load(m, *v, &u);
    if (unknowns) *unknowns = std::move(u);
    return m;
}

std::string WriteMaterial(const Material& m, const reflect::Unknowns* unknowns) {
    Material out = m;
    out.kind = kMaterialKind;
    return json::Write(reflect::Save(out, unknowns));
}

std::array<MaterialPicture, space3d::kPictureSlots> MaterialPictures(const Material& m) {
    const AssetRef* images[space3d::kPictureSlots] = {&m.colorImage, &m.metalRoughImage, &m.normalImage, &m.emissionImage};
    std::array<MaterialPicture, space3d::kPictureSlots> out;
    for (size_t k = 0; k < space3d::kPictureSlots; ++k) {
        const space3d::PictureSlot kind = space3d::SlotKind(k);
        out[k] = {*images[k], kind.srgb, kind.use};
    }
    return out;
}

space3d::DrawMaterial MaterialLook(const Material& m, const MaterialPictureSource& picture) {
    using space3d::ScreenToLinear;
    space3d::DrawMaterial d;
    d.color = {ScreenToLinear(m.color.r), ScreenToLinear(m.color.g), ScreenToLinear(m.color.b), m.color.a};
    const float k = m.emissionStrength;
    d.emissive = {ScreenToLinear(m.emission.r) * k, ScreenToLinear(m.emission.g) * k, ScreenToLinear(m.emission.b) * k, 1};
    d.alpha = m.alpha == MaterialAlpha::Cutout ? space3d::AlphaMode::Mask : m.alpha == MaterialAlpha::Blend ? space3d::AlphaMode::Blend : space3d::AlphaMode::Opaque;
    d.alphaCutoff = m.alphaCutoff;
    d.metallic = m.metallic;
    d.roughness = m.roughness;
    d.normalScale = m.normalScale;
    d.uvScale = m.tiling;
    d.uvOffset = m.offset;
    if (picture) {
        const auto pictures = MaterialPictures(m);
        for (size_t slot = 0; slot < pictures.size(); ++slot) {
            if (pictures[slot].image.Empty()) continue;
            space3d::SetPicture(d, slot, picture(pictures[slot], slot));
        }
    }
    return d;
}

std::span<const space3d::DrawMaterial> MeshMaterials(bool shape, std::span<const space3d::DrawMaterial> own,
                                                     std::span<const space3d::DrawMaterial* const> slots,
                                                     std::vector<space3d::DrawMaterial>& store) {
    if (shape) {
        if (slots.empty() || !slots[0]) return {};
        store.assign(1, *slots[0]);
        return store;
    }
    const size_t n = std::max<size_t>(own.size(), 1);  // (材質の無いモデルも 0 番は使える)
    bool any = false;
    for (size_t i = 0; i < slots.size() && i < n; ++i) any = any || slots[i] != nullptr;
    if (!any) return own;
    store.clear();
    store.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (i < slots.size() && slots[i]) {
            store.push_back(*slots[i]);
        } else {
            store.push_back(i < own.size() ? own[i] : space3d::DrawMaterial{});
        }
    }
    return store;
}

void UseSlotZeroForBareModel(space3d::MeshData& mesh) {
    if (!mesh.materials.empty()) return;
    if (mesh.parts.empty()) mesh.parts.push_back({0, static_cast<uint32_t>(mesh.indices.size()), -1});
    for (space3d::MeshPart& p : mesh.parts) p.material = 0;
}

std::string WriteBakedMaterial(std::string_view bytes, const std::vector<BakedMaterialPicture>& pictures) {
    auto v = json::Parse(bytes);
    json::Value doc = v && v->IsObject() ? std::move(*v) : json::Value::MakeObject();
    BakedMaterialPictures baked;
    baked.baked = pictures;
    doc.Set("baked", reflect::Save(baked).Get("baked"));
    return json::Write(doc);
}

std::vector<BakedMaterialPicture> ReadBakedPictures(std::string_view bytes) {
    const auto v = json::Parse(bytes);
    if (!v || !v->IsObject()) return {};
    BakedMaterialPictures baked;
    reflect::Load(baked, *v);  // (ほかのキー - マテリアルそのもの - は見ない)
    return std::move(baked.baked);
}

}  // namespace rm::game_x_3d
