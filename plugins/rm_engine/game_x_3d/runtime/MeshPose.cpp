#include "rm_engine/game_x_3d/runtime/MeshPose.h"

#include <algorithm>
#include <utility>

namespace rm::game_x_3d {

SkinnedModel TakeSkinned(space3d::MeshData& mesh) {
    SkinnedModel m;
    if (mesh.skin) {
        mesh.skin->morphs.clear();
        for (const space3d::MorphTarget& t : mesh.morphs) mesh.skin->morphs.push_back(t.name);
        mesh.skin->morphDefaults = mesh.morphDefaults;
        mesh.skin->morphDefaults.resize(mesh.skin->morphs.size(), 0.f);
        m.skin = std::make_shared<const space3d::SkinData>(std::move(*mesh.skin));
        m.clips = std::make_shared<const std::vector<space3d::Clip>>(std::move(mesh.clips));
    }
    mesh.skin.reset();
    mesh.clips.clear();
    return m;
}

std::vector<std::unique_ptr<MeshPoser>> MakePosers(const RuntimeRegistry* registry) {
    std::vector<std::unique_ptr<MeshPoser>> out;
    if (!registry) return out;
    for (const auto& p : registry->Contributions<MeshPoseInfo>(kMeshPosePoint)) {
        if (p.item->create) out.push_back(p.item->create());
    }
    return out;
}

bool ApplyPoses(std::span<const std::unique_ptr<MeshPoser>> posers, const MeshPoseFrame& f, std::span<const PoseOwner> owners,
                std::span<space3d::MeshDraw> meshes) {
    if (posers.empty()) return false;
    for (const auto& p : posers) p->Begin(f);
    const size_t n = std::min(owners.size(), meshes.size());
    for (size_t i = 0; i < n; ++i) {
        const PoseOwner& o = owners[i];
        space3d::MeshDraw& d = meshes[i];
        if (!o.model || !o.model->skin || !d.mesh || !d.mesh->skinned) continue;
        for (const auto& p : posers) {
            const MeshPoseAnswer a = p->Pose(o.entity, *o.model);
            if (a.bones.empty()) continue;
            d.bones = a.bones;
            d.morphWeights = a.morphWeights;
            d.poseVersion = a.poseVersion;
            d.moving = d.moving || a.moving;
            break;
        }
    }
    bool animating = false;
    for (const auto& p : posers) animating = animating || p->Animating();
    return animating;
}

}  // namespace rm::game_x_3d
