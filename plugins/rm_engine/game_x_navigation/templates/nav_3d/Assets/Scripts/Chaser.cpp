#include "Chaser.h"

#include "RM3D.h"
#include "RMNav.h"

#include <format>
#include <vector>

void Chaser::BeginPlay(rm::Entity self) {
    (void)self;
    rm::Log::Info("敵: プレイヤーを追いかけます");
}

void Chaser::Tick(rm::Entity self, float dt) {
    const rm::Entity player = rm::FindWithTag("Player");
    if (!player.Valid()) return;
    const rm::Vec3 target = rm::Transform3D(player).WorldPosition();
    // 一度だけ、プレイヤーまでの道をコンソールに (ナビメッシュは最初の固定の刻みで読むので、それより後)。
    if (!told) {
        std::vector<rm::Vec3> path;
        if (rm::Nav::FindPath(rm::Transform3D(self).WorldPosition(), target, path)) {
            rm::Log::Info(std::format("敵: プレイヤーまでの道が見つかりました (角 {} 個)", path.size() > 2 ? path.size() - 2 : 0));
            told = true;
        }
    }
    timer -= dt;
    if (timer > 0) return;
    timer = interval;
    rm::NavAgent(self).SetDestination(target);
}

void Chaser::OnEvent(rm::Entity self, const rm::Event& e) {
    (void)self;
    if (e.Is("NavArrived")) rm::Log::Info("敵: プレイヤーに追いつきました");
}
