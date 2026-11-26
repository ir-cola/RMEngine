#include "rm_engine/game_x_navigation/runtime/NavScene.h"

#include "rm_engine/navigation/runtime/Bake.h"

#include "base/Json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

namespace rm::game_x_navigation {

namespace {

using game_production::Component;
using game_production::Scene;
using game_production::SceneEntity;
using navigation::NavBounds;

template <class T>
const T* Find(const SceneEntity& e) {
    const Component* c = e.FindComponent(reflect::TypeOf<T>().name);
    return c && c->Known() ? static_cast<const T*>(c->value.Get()) : nullptr;
}

std::optional<int> ParseId(std::string_view s) {
    int v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

space3d::Mat4 WorldOf(const Worlds& worlds, int id) {
    const auto it = worlds.find(id);
    return it != worlds.end() ? it->second : space3d::Mat4{};
}

void Grow(NavBounds& b, Vec3 p) {
    b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
    b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
}
NavBounds Empty() {
    constexpr float big = std::numeric_limits<float>::max();
    return {{big, big, big}, {-big, -big, -big}};
}

// 真上から見た凸包 (x, z)。単調な鎖 (Andrew)。点が 3 つ未満なら空。
std::vector<Vec2> Hull(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return {};
    auto cross = [](Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
    std::vector<Vec2> h(pts.size() * 2);
    size_t k = 0;
    for (const Vec2& p : pts) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], p) <= 0) --k;
        h[k++] = p;
    }
    for (size_t i = pts.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    return h.size() >= 3 ? h : std::vector<Vec2>{};
}

// 部品「ナビのエリア」-> エリアの柱 (形の角をワールドへ。真上から見た凸包と、高さの範囲)。
std::optional<navigation::NavAreaVolume> AreaVolumeOf(const NavArea& a, const space3d::Mat4& world) {
    std::vector<Vec3> local;
    const Vec3 h{std::fabs(a.size.x) * 0.5f, std::fabs(a.size.y) * 0.5f, std::fabs(a.size.z) * 0.5f};
    if (a.shape == NavAreaShape::Cylinder) {
        constexpr int kSides = 24;
        for (int i = 0; i < kSides; ++i) {
            const float t = 2 * std::numbers::pi_v<float> * static_cast<float>(i) / kSides;
            for (const float y : {-h.y, h.y}) local.push_back({std::cos(t) * h.x, y, std::sin(t) * h.x});
        }
    } else {
        for (const float x : {-h.x, h.x}) {
            for (const float y : {-h.y, h.y}) {
                for (const float z : {-h.z, h.z}) local.push_back({x, y, z});
            }
        }
    }
    navigation::NavAreaVolume v;
    v.minY = std::numeric_limits<float>::max(), v.maxY = -v.minY;
    std::vector<Vec2> flat;
    for (const Vec3& p : local) {
        const Vec3 w = space3d::TransformPoint(world, p);
        if (!std::isfinite(w.x + w.y + w.z)) return std::nullopt;
        flat.push_back({w.x, w.z});
        v.minY = std::min(v.minY, w.y), v.maxY = std::max(v.maxY, w.y);
    }
    v.outline = Hull(std::move(flat));
    v.area = AreaCodeOf(a.area);
    if (v.outline.empty() || !(v.maxY > v.minY)) return std::nullopt;
    return v;
}

bool SourceOn(const NavSourceInfo& s, const NavMeshVolume& v) {
    const std::string_view id = s.id ? s.id : "";
    if (id == "meshes") return v.meshes;
    if (id == "colliders") return v.colliders;
    return true;
}

// エンティティごとの、焼くときの扱い (親から受け継ぐ物も)。
struct Rule {
    bool moving = false;  // 自分か親が動く (子に受け継ぐ)
    bool skip = false;    // 焼かない: 切ってある・動く・「入れない」
    std::optional<NavGeometryMode> mode;  // ナビの形が決めた扱い (自分か、「中の物にも」の親)
    uint8_t code = navigation::kAreaNormal;  // その床のエリア (壁だけ: 0)
    bool inherit = false;  // 子にも同じ扱い
};

std::unordered_map<int, Rule> RulesOf(const Scene& scene, std::span<const NavSourceInfo* const> sources) {
    std::unordered_map<int, Rule> rules;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const SceneEntity& e = *t.entity;
        Rule r;
        if (const auto up = rules.find(e.data.parent); up != rules.end()) {
            r.moving = up->second.moving;
            if (up->second.inherit) r.mode = up->second.mode, r.code = up->second.code, r.inherit = true;
        }
        for (const NavSourceInfo* s : sources) {
            if (s->moves && s->moves(e)) r.moving = true;
        }
        if (const NavGeometry* g = Find<NavGeometry>(e)) {
            r.mode = g->mode;
            r.code = g->mode == NavGeometryMode::Area ? AreaCodeOf(g->area) : navigation::kAreaNone;
            r.inherit = g->children;
        }
        r.skip = !t.activeInHierarchy || r.moving || r.mode == NavGeometryMode::Exclude;
        rules[e.data.id] = r;
    }
    return rules;
}

bool Touches(const NavBounds& b, Vec3 a, Vec3 c, Vec3 d) {
    const float x0 = std::min({a.x, c.x, d.x}), x1 = std::max({a.x, c.x, d.x});
    const float y0 = std::min({a.y, c.y, d.y}), y1 = std::max({a.y, c.y, d.y});
    const float z0 = std::min({a.z, c.z, d.z}), z1 = std::max({a.z, c.z, d.z});
    return x1 >= b.min.x && x0 <= b.max.x && y1 >= b.min.y && y0 <= b.max.y && z1 >= b.min.z && z0 <= b.max.z;
}

}  // namespace

std::vector<NavAgentType> DefaultAgentTypes() { return {NavAgentType{}}; }

std::vector<NavAreaKind> DefaultAreaKinds() {
    std::vector<NavAreaKind> out(3);
    out[0].id = 0, out[0].name = "ふつう", out[0].color = AreaKindColor(0);
    out[1].id = 1, out[1].name = "沼", out[1].color = AreaKindColor(1), out[1].cost = 3.f;
    out[2].id = 2, out[2].name = "立入禁止", out[2].color = AreaKindColor(2), out[2].blocked = true;
    return out;
}

Color AreaKindColor(size_t n) {
    // (データの色: ユーザーが変える。テーマの色ではない)
    static constexpr Color kColors[] = {{0.36f, 0.72f, 0.52f, 1}, {0.55f, 0.45f, 0.25f, 1}, {0.85f, 0.32f, 0.30f, 1}, {0.30f, 0.55f, 0.85f, 1},
                                        {0.80f, 0.65f, 0.20f, 1}, {0.60f, 0.40f, 0.80f, 1}, {0.25f, 0.70f, 0.75f, 1}, {0.85f, 0.50f, 0.65f, 1}};
    return kColors[n % std::size(kColors)];
}

Worlds WorldsOf(const Scene& scene) {
    const std::string t3 = reflect::TypeOf<space3d::Transform>().name;
    Worlds worlds;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const SceneEntity& e = *t.entity;
        space3d::Mat4 world = WorldOf(worlds, e.data.parent);
        if (const Component* c = e.FindComponent(t3); c && c->Known()) world = space3d::WorldMatrix(world, *static_cast<const space3d::Transform*>(c->value.Get()));
        worlds[e.data.id] = world;
    }
    return worlds;
}

const NavAgentType* FindAgentType(const NavSettings& s, std::string_view id) {
    if (const auto n = ParseId(id)) {
        for (const NavAgentType& t : s.agentTypes) {
            if (t.id == *n) return &t;
        }
    }
    return s.agentTypes.empty() ? nullptr : &s.agentTypes.front();
}

const NavAreaKind* FindAreaKind(const NavSettings& s, int id) {
    for (const NavAreaKind& a : s.areas) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

navigation::NavAgent AgentOf(const NavAgentType& t) {
    navigation::NavAgent a;
    a.radius = t.radius;
    a.height = t.height;
    a.maxSlope = t.maxSlope;
    a.maxClimb = t.stepHeight;
    const float divisor = t.detail == NavDetail::Coarse ? 2.f : t.detail == NavDetail::Fine ? 5.f : 3.f;
    a.cellSize = std::max(0.02f, (std::isfinite(t.radius) ? t.radius : 0.3f) / divisor);
    a.cellHeight = std::max(0.01f, a.cellSize * 0.5f);
    return navigation::Sanitized(a);
}

uint8_t AreaCodeOf(std::string_view areaId) {
    const auto n = ParseId(areaId);
    if (!n || *n <= 0 || *n > navigation::kMaxAreaKinds) return navigation::kAreaNormal;
    return static_cast<uint8_t>(*n);
}

int AreaIdOfCode(uint8_t code) { return code == navigation::kAreaNormal || code > navigation::kMaxAreaKinds ? 0 : code; }

navigation::NavFilter FilterOf(const NavSettings& s) {
    navigation::NavFilter f;
    for (const NavAreaKind& a : s.areas) {
        if (a.id < 0 || a.id > navigation::kMaxAreaKinds) continue;
        const uint8_t code = a.id == 0 ? navigation::kAreaNormal : static_cast<uint8_t>(a.id);
        f.costs[code] = std::isfinite(a.cost) ? std::clamp(a.cost, 1.f, 1000.f) : 1.f;
    }
    return f;
}

std::bitset<64> BlockedOf(const NavSettings& s) {
    std::bitset<64> b;
    for (const NavAreaKind& a : s.areas) {
        if (a.id < 0 || a.id > navigation::kMaxAreaKinds || !a.blocked) continue;
        b.set(a.id == 0 ? navigation::kAreaNormal : static_cast<size_t>(a.id));
    }
    return b;
}

int NewAgentTypeId(const NavSettings& s) {
    int id = 1;
    for (const NavAgentType& t : s.agentTypes) id = std::max(id, t.id + 1);
    return id;
}

int NewAreaKindId(const NavSettings& s) {
    for (int id = 1; id <= navigation::kMaxAreaKinds; ++id) {
        if (!FindAreaKind(s, id)) return id;
    }
    return 0;
}

std::vector<NavVolume> Volumes(const Scene& scene, const Worlds& worlds) {
    std::vector<NavVolume> out;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        const SceneEntity& e = *t.entity;
        const NavMeshVolume* v = Find<NavMeshVolume>(e);
        if (!v) continue;
        NavVolume nv;
        nv.entity = e.data.id;
        nv.name = e.data.name;
        nv.active = t.activeInHierarchy;
        nv.volume = *v;
        nv.bounds = Empty();
        const space3d::Mat4 world = WorldOf(worlds, e.data.id);
        const Vec3 h{std::fabs(v->size.x) * 0.5f, std::fabs(v->size.y) * 0.5f, std::fabs(v->size.z) * 0.5f};
        for (const float x : {-h.x, h.x}) {
            for (const float y : {-h.y, h.y}) {
                for (const float z : {-h.z, h.z}) Grow(nv.bounds, space3d::TransformPoint(world, {v->offset.x + x, v->offset.y + y, v->offset.z + z}));
            }
        }
        if (!std::isfinite(nv.bounds.min.x + nv.bounds.min.y + nv.bounds.min.z + nv.bounds.max.x + nv.bounds.max.y + nv.bounds.max.z)) nv.bounds = {};
        out.push_back(std::move(nv));
    }
    return out;
}

std::optional<NavVolume> FindVolume(const Scene& scene, const Worlds& worlds, int entity) {
    for (NavVolume& v : Volumes(scene, worlds)) {
        if (v.entity == entity) return std::move(v);
    }
    return std::nullopt;
}

bool CollectInput(const Scene& scene, const Worlds& worlds, const NavVolume& volume, std::span<const NavSourceInfo* const> sources,
                  const game_production::AssetAccess* assets, std::stop_token stop, navigation::NavInput& out) {
    out = {};
    const std::unordered_map<int, Rule> rules = RulesOf(scene, sources);
    // 範囲のすぐ外の壁も、区画のふちの計算に要る。
    NavBounds near = volume.bounds;
    constexpr float kMargin = 2.f;
    near.min = {near.min.x - kMargin, near.min.y - kMargin, near.min.z - kMargin};
    near.max = {near.max.x + kMargin, near.max.y + kMargin, near.max.z + kMargin};
    NavSourceContext ctx{scene, worlds, near, assets, stop};
    std::vector<NavShape> shapes;
    for (const NavSourceInfo* s : sources) {
        if (stop.stop_requested()) return false;
        if (!s->collect || !SourceOn(*s, volume.volume) || (s->space && std::string_view(s->space) != "3d")) continue;
        shapes.clear();
        s->collect(ctx, shapes);
        for (const NavShape& shape : shapes) {
            const auto r = rules.find(shape.entity);
            if (r != rules.end() && r->second.skip) continue;
            if (shape.entity == volume.entity) continue;  // (ナビメッシュ自身)
            const uint8_t code = r != rules.end() && r->second.mode ? r->second.code : navigation::kAreaNormal;
            for (size_t i = 0; i + 2 < shape.indices.size(); i += 3) {
                const uint32_t a = shape.indices[i], b = shape.indices[i + 1], c = shape.indices[i + 2];
                if (a >= shape.points.size() || b >= shape.points.size() || c >= shape.points.size()) continue;
                if (!Touches(near, shape.points[a], shape.points[b], shape.points[c])) continue;
                out.AddTriangle(shape.points[a], shape.points[b], shape.points[c], code);
            }
        }
    }
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        if (!t.activeInHierarchy) continue;
        const NavArea* a = Find<NavArea>(*t.entity);
        if (!a) continue;
        if (auto v = AreaVolumeOf(*a, WorldOf(worlds, t.entity->data.id))) out.volumes.push_back(std::move(*v));
    }
    return !stop.stop_requested();
}

void AddMeshTo(NavShape& out, std::span<const Vec3> local, std::span<const uint32_t> indices, const space3d::Mat4& world) {
    const auto& m = world.m;
    const float det = m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) - m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) +
                      m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
    const uint32_t base = static_cast<uint32_t>(out.points.size());
    for (const Vec3& p : local) out.points.push_back(space3d::TransformPoint(world, p));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
        if (a >= local.size() || b >= local.size() || c >= local.size()) continue;
        out.indices.push_back(base + a);
        out.indices.push_back(base + (det < 0 ? c : b));
        out.indices.push_back(base + (det < 0 ? b : c));
    }
}

void HashComponent(navigation::Hasher& h, const SceneEntity& e, const Component& c, const Worlds& worlds) {
    h.Int(e.data.id);
    const space3d::Mat4 m = WorldOf(worlds, e.data.id);
    for (const auto& col : m.m) {
        for (const float f : col) h.Float(f);
    }
    h.Text(json::Write(Scene::SaveComponent(c), false));
}

uint64_t Fingerprint(const Scene& scene, const Worlds& worlds, const NavVolume& volume, const NavAgentType& type,
                     std::span<const NavSourceInfo* const> sources, const game_production::AssetAccess* assets) {
    navigation::Hasher h;
    const navigation::NavAgent a = AgentOf(type);
    for (const float f : {a.radius, a.height, a.maxSlope, a.maxClimb, a.cellSize, a.cellHeight}) h.Float(f);
    for (const float f : {volume.bounds.min.x, volume.bounds.min.y, volume.bounds.min.z, volume.bounds.max.x, volume.bounds.max.y, volume.bounds.max.z}) h.Float(f);
    for (const NavSourceInfo* s : sources) {
        if (!SourceOn(*s, volume.volume)) continue;
        h.Text(s->id ? s->id : "");
        if (s->fingerprint) s->fingerprint(scene, worlds, assets, h);
    }
    const std::string geometry = reflect::TypeOf<NavGeometry>().name, area = reflect::TypeOf<NavArea>().name;
    for (const game_production::TreeEntry& t : scene.TreeOrder()) {
        h.Int(t.entity->data.id);
        h.Int(t.activeInHierarchy ? 1 : 0);
        for (const Component& c : t.entity->components) {
            if (c.type == geometry || c.type == area) HashComponent(h, *t.entity, c, worlds);
        }
    }
    return h.Value();
}

}  // namespace rm::game_x_navigation
