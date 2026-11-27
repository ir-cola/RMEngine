#include "rm_engine/game_x_navigation/runtime/NavWorld.h"

#include "base/Log.h"
#include "platform/Shell.h"
#include "rm_engine/game_x_navigation/runtime/NavPorts.h"
#include "rm_engine/game_x_navigation/runtime/NavScene.h"
#include "rm_engine/navigation/runtime/NavFile.h"
#include "rm_engine/space3d/runtime/Transform.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace rm::game_x_navigation {

namespace {

using game_production::Component;
using game_production::Scene;
using game_production::SceneEntity;

constexpr char kWorldKey[] = "rm_engine.game_x_navigation.world";
constexpr float kMovedFar = 0.05f;  // これより Transform がずれていたら、ほかの誰かが動かした (置き直す)

float Dist(Vec3 a, Vec3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}
bool Inside(const navigation::NavBounds& b, Vec3 p, float margin) {
    return p.x >= b.min.x - margin && p.x <= b.max.x + margin && p.y >= b.min.y - margin && p.y <= b.max.y + margin && p.z >= b.min.z - margin &&
           p.z <= b.max.z + margin;
}
int IdOf(std::string_view s) {
    int v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : 0;
}

space3d::Transform* TransformOf(SceneEntity& e) {
    Component* c = e.FindComponent(reflect::TypeOf<space3d::Transform>().name);
    return c && c->Known() ? static_cast<space3d::Transform*>(c->value.Get()) : nullptr;
}

std::optional<std::string> ReadBaked(const game_production::AssetAccess* assets, const AssetRef& ref, std::string* why) {
    if (!assets) return std::nullopt;
    if (assets->bytes) {
        if (auto b = assets->bytes(ref)) return b;
    }
    if (assets->file) {
        if (const auto f = assets->file(ref); !f.empty()) return platform::ReadFileBytes(f, why);
    }
    return std::nullopt;
}

}  // namespace

float TurnToward(float yaw, Vec3 toward, float maxDegrees) {
    if (toward.x * toward.x + toward.z * toward.z < 1e-8f || !std::isfinite(yaw)) return yaw;
    const float want = std::atan2(toward.x, toward.z) * 180.f / std::numbers::pi_v<float>;
    float d = std::fmod(want - yaw, 360.f);
    if (d > 180.f) d -= 360.f;
    if (d < -180.f) d += 360.f;
    const float step = std::max(0.f, maxDegrees);
    return yaw + std::clamp(d, -step, step);
}

NavWorld& NavWorldOf(Scene& scene) { return scene.PlayState<NavWorld>(kWorldKey); }

NavWorld* FindNavWorld(Scene& scene) {
    const std::shared_ptr<void>& slot = scene.PlayStatesOf().Slot(kWorldKey);
    return slot ? static_cast<NavWorld*>(slot.get()) : nullptr;
}

void RunNavigation(game_production::SystemContext& ctx) { NavWorldOf(ctx.scene).Step(ctx); }

void NavWorld::Load(Scene& scene, const game_production::AssetAccess* assets) {
    loaded_ = true;
    meshes_.clear();
    const NavSettings* settings = scene.Setting<NavSettings>();
    const int maxAgents = settings ? std::clamp(settings->maxAgents, 1, 1000) : 128;
    const Worlds worlds = WorldsOf(scene);
    for (const NavVolume& v : Volumes(scene, worlds)) {
        if (!v.active) continue;
        if (v.volume.baked.Empty()) {
            log::Warn("ナビ: ナビメッシュ「{}」はまだ焼いていません (「ナビ」タブの「焼く」)", v.name);
            continue;
        }
        std::string why;
        const auto bytes = ReadBaked(assets, v.volume.baked, &why);
        if (!bytes) {
            log::Warn("ナビ: ナビメッシュ「{}」の焼いた物がありません{}", v.name, why.empty() ? std::string() : ": " + why);
            continue;
        }
        navigation::NavFileHead head;
        const auto data = navigation::ReadNavFile(*bytes, &head, &why);
        if (!data) {
            log::Warn("ナビ: ナビメッシュ「{}」を読めません: {}", v.name, why);
            continue;
        }
        Mesh m;
        m.volume = v.entity;
        m.name = v.name;
        m.agentType = head.agentType;
        m.bounds = data->bounds;
        m.mesh = std::make_unique<navigation::NavMesh>();
        if (!m.mesh->Load(*data, &why)) {
            log::Warn("ナビ: ナビメッシュ「{}」を読めません: {}", v.name, why);
            continue;
        }
        m.crowd = std::make_unique<navigation::NavCrowd>();
        if (!m.mesh->Empty() && !m.crowd->Init(*m.mesh, maxAgents, std::max(data->agent.radius, 0.05f))) m.crowd.reset();
        log::Info("ナビ: ナビメッシュ「{}」を読みました (区画 {}・多角形 {})", v.name, m.mesh->Tiles(), m.mesh->Polygons());
        meshes_.push_back(std::move(m));
    }
}

NavWorld::Mesh* NavWorld::MeshAt(Vec3 at, int agentType) {
    Mesh* any = nullptr;
    for (Mesh& m : meshes_) {
        if (agentType != 0 && m.agentType != agentType) continue;
        if (Inside(m.bounds, at, 1.f)) return &m;
        if (!any) any = &m;
    }
    if (any) return any;
    return meshes_.empty() ? nullptr : &meshes_.front();
}

navigation::NavFilter NavWorld::Filter(const NavSettings& s) const {
    NavSettings copy = s;
    for (NavAreaKind& a : copy.areas) {
        if (const auto it = costs_.find(a.id); it != costs_.end()) a.cost = it->second;
    }
    return FilterOf(copy);
}

std::bitset<64> NavWorld::Blocked(const NavSettings& s) const {
    NavSettings copy = s;
    for (NavAreaKind& a : copy.areas) {
        if (const auto it = open_.find(a.id); it != open_.end()) a.blocked = !it->second;
    }
    return BlockedOf(copy);
}

void NavWorld::Apply(Scene& scene) {
    const NavSettings* s = scene.Setting<NavSettings>();
    if (!s) return;
    const navigation::NavFilter filter = Filter(*s);
    const std::bitset<64> blocked = Blocked(*s);
    for (Mesh& m : meshes_) {
        m.mesh->SetBlockedAreas(blocked);
        if (m.crowd) m.crowd->SetFilter(filter);
    }
}

void NavWorld::Place(Agent& a, int entity, Vec3 at, const navigation::AgentParams& params, const NavAgent& c, std::string_view name) {
    if (a.mesh >= 0 && a.index >= 0 && meshes_[static_cast<size_t>(a.mesh)].crowd) {
        meshes_[static_cast<size_t>(a.mesh)].crowd->Remove(a.index);
        --meshes_[static_cast<size_t>(a.mesh)].agents;
    }
    a.mesh = a.index = -1;
    Mesh* m = MeshAt(at, IdOf(c.agentType));
    if (m && m->crowd) {
        a.index = m->crowd->Add(at, params);
        if (a.index >= 0) {
            a.mesh = static_cast<int>(m - meshes_.data());
            ++m->agents;
            a.written = at;
            a.pending = a.hasTarget && !a.stopped;  // (行き先はもう一度頼む)
            a.warnedOff = false;
            return;
        }
    }
    if (!a.warnedOff) {
        a.warnedOff = true;
        if (!m) {
            log::Warn("ナビ: 「{}」が歩くナビメッシュがありません (焼いたナビメッシュの範囲の中に置いてください)", name);
        } else if (m->agents >= 1 && m->crowd) {
            log::Warn("ナビ: 「{}」はナビメッシュの外か、動く者の上限を超えています (ナビメッシュ「{}」)", name, m->name);
        } else {
            log::Warn("ナビ: 「{}」はナビメッシュ「{}」の外にいます", name, m->name);
        }
    }
    (void)entity;
}

void NavWorld::Step(game_production::SystemContext& ctx) {
    Scene& scene = ctx.scene;
    if (!loaded_) Load(scene, ctx.assets);
    Apply(scene);
    const NavSettings* settings = scene.Setting<NavSettings>();
    Worlds worlds = WorldsOf(scene);
    std::vector<const NavMoverInfo*> movers;
    if (ctx.registry) {
        for (const auto& m : ctx.registry->Contributions<NavMoverInfo>(kNavMoverPoint)) movers.push_back(m.item);
    }

    // 3: 動く者を群れに (今の位置・頼まれたこと・速さ)。
    std::unordered_set<int> seen;
    const std::string type = reflect::TypeOf<NavAgent>().name;
    game_production::ForEachComponent(scene, type, [&](SceneEntity& e, Component& comp) {
        const int id = e.data.id;
        if (seen.count(id)) return;  // (1 つの物に 1 つ)
        seen.insert(id);
        const NavAgent& c = *static_cast<const NavAgent*>(comp.value.Get());
        const auto w = worlds.find(id);
        const Vec3 at = w != worlds.end() ? Vec3{w->second.m[3][0], w->second.m[3][1], w->second.m[3][2]} : Vec3{};
        Agent& a = agents_[id];
        if (!a.warnedMover) {
            for (const NavMoverInfo* m : movers) {
                if (m->owns && m->owns(scene, id)) {
                    a.warnedMover = true;
                    log::Warn("ナビ: 「{}」にはキャラクターがあります。今は Transform を直に動かします (キャラクターで歩くのは、これから: 段階 3)", e.data.name);
                    break;
                }
            }
        }
        const NavAgentType* t = settings ? FindAgentType(*settings, c.agentType) : nullptr;
        navigation::AgentParams p;
        if (t) p.radius = t->radius, p.height = t->height;
        const float speed = a.speed >= 0 ? a.speed : c.speed;
        p.maxSpeed = std::max(0.f, std::isfinite(speed) ? speed : 0.f);
        p.maxAcceleration = std::max(0.f, std::isfinite(c.acceleration) ? c.acceleration : 0.f);
        // 着く前にゆるめる: 止まれる距離 (v² / 2a) より近くなったら、残りに比べて速さを落とす。
        if (c.slowDown && a.hasTarget && !a.stopped && a.remaining > 0 && p.maxAcceleration > 0) {
            const float brake = std::max(p.maxSpeed * p.maxSpeed / (2.f * p.maxAcceleration), p.radius * 2.f);
            if (a.remaining < brake) p.maxSpeed *= std::max(0.15f, std::sqrt(a.remaining / brake));
        }
        if (a.index < 0 || (a.mesh >= 0 && Dist(at, a.written) > kMovedFar)) {
            Place(a, id, at, p, c, e.data.name);
        } else if (meshes_[static_cast<size_t>(a.mesh)].crowd) {
            meshes_[static_cast<size_t>(a.mesh)].crowd->SetParams(a.index, p);
        }
        if (a.index < 0) return;
        navigation::NavCrowd& crowd = *meshes_[static_cast<size_t>(a.mesh)].crowd;
        if (a.pending) {
            a.pending = false;
            if (a.hasTarget && !a.stopped) {
                if (!crowd.SetTarget(a.index, a.target)) log::Warn("ナビ: 「{}」の行き先の近くにナビメッシュがありません", e.data.name);
                a.arrived = false;
            } else {
                crowd.ClearTarget(a.index);
            }
        }
    });
    // (部品が無くなった・切った物は群れから外す)
    for (auto it = agents_.begin(); it != agents_.end();) {
        if (seen.count(it->first)) {
            ++it;
            continue;
        }
        if (it->second.mesh >= 0 && it->second.index >= 0 && meshes_[static_cast<size_t>(it->second.mesh)].crowd) {
            meshes_[static_cast<size_t>(it->second.mesh)].crowd->Remove(it->second.index);
            --meshes_[static_cast<size_t>(it->second.mesh)].agents;
        }
        it = agents_.erase(it);
    }

    // 4: 群れを進める。
    for (Mesh& m : meshes_) {
        if (m.crowd && m.agents > 0) m.crowd->Update(ctx.dt);
    }

    // Transform に書く・着いた。
    for (auto& [id, a] : agents_) {
        if (a.index < 0) continue;
        navigation::NavCrowd& crowd = *meshes_[static_cast<size_t>(a.mesh)].crowd;
        a.view = crowd.View(a.index);
        a.remaining = a.hasTarget ? crowd.Remaining(a.index) : 0;
        SceneEntity* e = scene.Find(id);
        if (!e) continue;
        const Component* comp = e->FindComponent(type);
        const NavAgent* c = comp && comp->Known() ? static_cast<const NavAgent*>(comp->value.Get()) : nullptr;
        if (space3d::Transform* t = TransformOf(*e); t && a.view.onMesh) {
            const auto up = worlds.find(e->data.parent);
            const space3d::Mat4 parent = up != worlds.end() ? up->second : space3d::Mat4{};
            const auto self = worlds.find(id);
            space3d::WorldPlacement want = space3d::Decompose(self != worlds.end() ? self->second : space3d::Mat4{}, t->rotation);
            want.position = a.view.position;
            const Vec3 v = a.view.velocity;
            if (c && c->turnSpeed > 0 && v.x * v.x + v.z * v.z > 0.0025f) want.rotation.y = TurnToward(want.rotation.y, v, c->turnSpeed * ctx.dt);
            *t = space3d::FromWorld(*t, parent, want);
            a.written = a.view.position;
            worlds[id] = space3d::WorldMatrix(parent, *t);
        }
        // 5: 着いた (残りが止まる距離より短い)。
        const float stop = c ? std::max(c->stoppingDistance, 0.01f) : 0.1f;
        if (a.hasTarget && !a.stopped && !a.arrived && a.view.target == navigation::AgentView::Target::Moving && a.remaining <= stop) {
            a.arrived = true;
            a.hasTarget = false;
            crowd.ClearTarget(a.index);
            if (ctx.events) ctx.events->push_back({id, kNavArrivedEvent, 0.0, std::string(), id});
        }
    }
}

void NavWorld::SetDestination(int entity, Vec3 target) {
    Agent& a = agents_[entity];
    a.hasTarget = true;
    a.target = target;
    a.arrived = false;
    a.pending = true;
}

void NavWorld::Stop(int entity) {
    Agent& a = agents_[entity];
    a.stopped = true;
    a.pending = true;
}

void NavWorld::Resume(int entity) {
    Agent& a = agents_[entity];
    a.stopped = false;
    a.pending = true;
}

void NavWorld::SetSpeed(int entity, float speed) { agents_[entity].speed = std::isfinite(speed) ? std::max(0.f, speed) : -1.f; }

bool NavWorld::Warp(Scene& scene, int entity, Vec3 position) {
    Mesh* m = MeshAt(position);
    if (!m) return false;
    const auto p = m->mesh->ClosestPoint(position, {2, 4, 2});
    if (!p) return false;
    SceneEntity* e = scene.Find(entity);
    space3d::Transform* t = e ? TransformOf(*e) : nullptr;
    if (!t) return false;
    const Worlds worlds = WorldsOf(scene);
    const auto up = worlds.find(e->data.parent);
    const auto self = worlds.find(entity);
    space3d::WorldPlacement want = space3d::Decompose(self != worlds.end() ? self->second : space3d::Mat4{}, t->rotation);
    want.position = *p;
    *t = space3d::FromWorld(*t, up != worlds.end() ? up->second : space3d::Mat4{}, want);
    Agent& a = agents_[entity];
    a.hasTarget = false;
    a.arrived = false;
    a.pending = true;
    a.written = {1e30f, 1e30f, 1e30f};  // (次の刻みで置き直す)
    return true;
}

const NavWorld::Agent* NavWorld::FindAgent(int entity) const {
    const auto it = agents_.find(entity);
    return it != agents_.end() ? &it->second : nullptr;
}

bool NavWorld::SetAreaCost(const Scene& scene, std::string_view name, float cost) {
    if (const NavSettings* s = scene.Setting<NavSettings>()) {
        for (const NavAreaKind& a : s->areas) {
            if (a.name == name) {
                costs_[a.id] = std::isfinite(cost) ? std::clamp(cost, 1.f, 1000.f) : 1.f;
                return true;
            }
        }
    }
    if (std::find(warnedNames_.begin(), warnedNames_.end(), name) == warnedNames_.end()) {
        warnedNames_.emplace_back(name);
        log::Warn("ナビ: エリアの種類「{}」がありません", name);
    }
    return false;
}

bool NavWorld::SetAreaOpen(const Scene& scene, std::string_view name, bool open) {
    if (const NavSettings* s = scene.Setting<NavSettings>()) {
        for (const NavAreaKind& a : s->areas) {
            if (a.name == name) {
                open_[a.id] = open;
                return true;
            }
        }
    }
    if (std::find(warnedNames_.begin(), warnedNames_.end(), name) == warnedNames_.end()) {
        warnedNames_.emplace_back(name);
        log::Warn("ナビ: エリアの種類「{}」がありません", name);
    }
    return false;
}

}  // namespace rm::game_x_navigation
