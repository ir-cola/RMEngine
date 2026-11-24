// 材質の数と絵の場所 (Texture.h の MaterialNumbers / PictureSlot / SlotKind / PictureSlots / SetPicture)。
// GPU もスレッドも使わない純粋な部分なので、Texture.cpp から分けた (テストがこれだけを組み込める)。

#include "rm_engine/space3d/runtime/Texture.h"

namespace rm::space3d {

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

PictureSlot SlotKind(size_t slot) {
    switch (slot) {
        case 0: return {-1, true, TextureUse::Color};
        case 1: return {-1, false, TextureUse::Data};
        case 2: return {-1, false, TextureUse::Normal};
        default: return {-1, true, TextureUse::Color};
    }
}

std::array<PictureSlot, kPictureSlots> PictureSlots(const MeshMaterial& m) {
    const int images[kPictureSlots] = {m.image, m.metallicRoughnessImage, m.normalImage, m.emissiveImage};
    std::array<PictureSlot, kPictureSlots> out;
    for (size_t k = 0; k < kPictureSlots; ++k) {
        out[k] = SlotKind(k);
        out[k].image = images[k];
    }
    return out;
}

void SetPicture(DrawMaterial& d, size_t slot, TextureRef texture) {
    switch (slot) {
        case 0: d.texture = std::move(texture); break;
        case 1: d.metalRough = std::move(texture); break;
        case 2: d.normal = std::move(texture); break;
        default: d.emission = std::move(texture); break;
    }
}

}  // namespace rm::space3d
