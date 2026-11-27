#include "Wanderer.h"

#include "RM3D.h"
#include "RMNav.h"

void Wanderer::Tick(rm::Entity self, float dt) {
    (void)dt;
    const rm::NavAgent agent(self);
    if (!started) {
        home = rm::Transform3D(self).WorldPosition();
        started = true;
    }
    // 行き先が無いとき (始め・行けなかったとき): 次の所へ。
    if (!agent.HasPath()) agent.SetDestination(rm::Nav::RandomPoint(home, radius));
}

void Wanderer::OnEvent(rm::Entity self, const rm::Event& e) {
    if (e.Is("NavArrived")) rm::NavAgent(self).SetDestination(rm::Nav::RandomPoint(home, radius));
}
