#include "rm_engine/game_x_2d/runtime/World2D.h"

#include "rm_engine/game_x_2d/runtime/Sprite.h"
#include "rm_engine/space2d/runtime/Atlas.h"
#include "rm_engine/space2d/runtime/Transform2D.h"

#include <algorithm>
#include <unordered_map>

namespace rm::game_x_2d {

std::vector<Placed2D> PlaceScene(const game_production::Scene& scene, const ImageLookup2D& images) {
    const std::string transformType = reflect::TypeOf<space2d::Transform2D>().name, spriteType = reflect::TypeOf<Sprite>().name,
                      cameraType = reflect::TypeOf<Camera2D>().name;
    std::vector<Placed2D> out;
    struct Parent {
        space2d::Mat3 world;
        int order = 0;
    };
    std::unordered_map<int, Parent> parents;  // 親は TreeOrder で子より先に来る
    size_t index = 0;
    for (const auto& t : scene.TreeOrder()) {
        const auto& e = *t.entity;
        const auto it = parents.find(e.data.parent);
        const Parent parent = it != parents.end() ? it->second : Parent{};
        const game_production::Component* comp = e.FindComponent(transformType);
        if (!comp || !comp->Known()) {
            parents[e.data.id] = parent;  // 自分の場所を持たない: 子は親についていく
            ++index;
            continue;
        }
        const auto& tr = *static_cast<const space2d::Transform2D*>(comp->value.Get());
        Placed2D p;
        p.id = e.data.id;
        p.parentWorld = parent.world;
        p.world = space2d::WorldMatrix(parent.world, tr);
        p.order = parent.order + tr.order;
        p.tree = index++;
        p.active = t.activeInHierarchy;
        parents[e.data.id] = {p.world, p.order};
        if (const game_production::Component* cc = e.FindComponent(cameraType); cc && cc->Known()) {
            p.camera = *static_cast<const Camera2D*>(cc->value.Get());
        }
        if (const game_production::Component* sc = e.FindComponent(spriteType); sc && sc->Known()) {
            const auto& sp = *static_cast<const Sprite*>(sc->value.Get());
            p.sprite = true;
            p.subOrder = sp.order;
            p.draw.world = p.world;
            p.draw.color = sp.color;
            p.draw.flipX = sp.flipX;
            p.draw.flipY = sp.flipY;
            const Image2D* img = images ? images(sp.image) : nullptr;
            p.draw.crisp = sp.crisp || (img && img->sharp);  // スプライト自身の設定、または画像の取り込みの設定
            if (img && img->bitmap) {
                p.draw.image = img->bitmap;
                // アトラスのセル (ビットマップのピクセル)。平面での大きさはファイルのピクセル数から決める
                // (縮小したビットマップでも、スプライトの大きさは変わらない)。
                const D2D1_SIZE_U px = img->bitmap->GetPixelSize();
                p.draw.part = space2d::CellOf(px.width, px.height, sp.columns, sp.rows, sp.cell);
                const space2d::PixelRect file = space2d::CellOf(img->width, img->height, sp.columns, sp.rows, sp.cell);
                const float ppu = std::max(sp.pixelsPerUnit, 0.01f);
                p.draw.size = {static_cast<float>(file.w) / ppu, static_cast<float>(file.h) / ppu};
            }
        }
        out.push_back(std::move(p));
    }
    return out;
}

const Placed2D* FindPlaced(std::span<const Placed2D> placed, int id) {
    for (const Placed2D& p : placed) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void WorldDrawers2D::Sync(const RuntimeRegistry& registry) {
    if (made_) return;
    made_ = true;
    for (const auto& d : registry.Contributions<WorldDraw2DInfo>(kWorldDraw2DPoint)) {
        if (d.item->create) {
            if (auto made = d.item->create()) drawers_.push_back(std::move(made));
        }
    }
}

void CollectDraws(const WorldDraw2DFrame& f, std::span<const std::unique_ptr<WorldDrawer2D>> drawers, std::vector<Drawn2D>& out) {
    std::vector<Drawn2D> all;
    for (const Placed2D& p : f.placed) {
        if (!p.sprite || (f.game && !p.active)) continue;  // オフ: ゲームには出ない
        Drawn2D d;
        d.draw = p.draw;
        if (!p.active) d.draw.color = d.draw.color.Alpha(d.draw.color.a * 0.35f);  // オフ: シーンビューでは薄く
        d.key = {p.order, p.subOrder, p.tree};
        d.entity = p.id;
        all.push_back(d);
    }
    for (const auto& drawer : drawers) drawer->Collect(f, all);
    std::vector<space2d::DrawKey> keys;
    keys.reserve(all.size());
    for (const Drawn2D& d : all) keys.push_back(d.key);
    out.clear();
    out.reserve(all.size());
    for (const size_t i : space2d::DrawOrder(keys)) out.push_back(all[i]);
}

}  // namespace rm::game_x_2d
