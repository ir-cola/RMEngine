#include "rm_engine/particles/runtime/BuiltinModules.h"

#include <map>
#include <mutex>
#include <string>

namespace rm::particles {

namespace {

// (Order: as offered in the パーティクル tab's ＋ menu - shapes, then what a particle starts with, then
// what moves it, then how it looks over its life.)
constexpr BuiltinModule kModules[] = {
    {"shape_cone", R"(module "形: 円すい"
param float angle = 25 [0, 90]; // 広がる角度 (度)
param float radius = 0.1 [0, 1000]; // 出る所の半径

void Spawn(inout Particle p, inout Random rnd) {
    float2 disc = RandInDisc(rnd) * radius;
    p.position = EmitterPoint(float3(disc.x, 0, disc.y));
    float a = radians(angle) * sqrt(Rand(rnd));
    float t = Rand(rnd) * 6.2831853;
    p.velocity = EmitterDirection(float3(sin(a) * cos(t), cos(a), sin(a) * sin(t)));
}
)"},
    {"shape_sphere", R"(module "形: 球"
param float radius = 0.5 [0, 1000]; // 半径
param bool surface = false; // 表面からだけ出す

void Spawn(inout Particle p, inout Random rnd) {
    float3 d = RandOnSphere(rnd);
    float r = surface ? radius : radius * pow(Rand(rnd), 1.0 / 3.0);
    p.position = EmitterPoint(d * r);
    p.velocity = EmitterDirection(d);
}
)"},
    {"shape_box", R"(module "形: 箱"
param float3 extent = (1, 1, 1) [0, 1000]; // 箱の大きさ

void Spawn(inout Particle p, inout Random rnd) {
    float3 q = (float3(Rand(rnd), Rand(rnd), Rand(rnd)) - 0.5) * extent;
    p.position = EmitterPoint(q);
    p.velocity = EmitterDirection(float3(0, 1, 0));
}
)"},
    {"initial", R"(module "はじめの値"
param float2 life = (1, 1.5) [0, 1000]; // 寿命 (秒。いちばん短い・長い)
param float2 speed = (1, 2) [-1000, 1000]; // 速さ (いちばん遅い・速い)
param float2 scale = (0.2, 0.4) [0, 1000]; // 大きさ (いちばん小さい・大きい)
param color tint = (1, 0.8, 0.4, 1); // 色
param float2 angle = (0, 360) [-100000, 100000]; // 回転 (度。いちばん小さい・大きい)
param float2 turn = (0, 0) [-100000, 100000]; // 回る速さ (度/秒。いちばん小さい・大きい)

void Spawn(inout Particle p, inout Random rnd) {
    p.lifetime = max(0.001, RandRange(rnd, life.x, life.y));
    float3 d = dot(p.velocity, p.velocity) > 1e-10 ? normalize(p.velocity) : EmitterDirection(float3(0, 1, 0));
    p.velocity = d * RandRange(rnd, speed.x, speed.y);
    p.size = RandRange(rnd, scale.x, scale.y);
    p.startSize = p.size;
    p.color = tint;
    p.startColor = tint;
    p.rotation = radians(RandRange(rnd, angle.x, angle.y));
    p.spin = radians(RandRange(rnd, turn.x, turn.y));
}
)"},
    {"force", R"(module "力"
param float3 acceleration = (0, -1, 0); // 加速 (重力・風。m/秒²)
param float drag = 0 [0, 100]; // 空気の抵抗

void Update(inout Particle p, float dt, inout Random rnd) {
    p.velocity += acceleration * dt;
    p.velocity *= exp(-drag * dt);
}
)"},
    {"noise", R"(module "ゆらぎ"
param float strength = 1 [0, 1000]; // ゆらぎの強さ
param float frequency = 1 [0, 1000]; // 細かさ
param float flow = 0.5 [0, 1000]; // 変わる速さ

void Update(inout Particle p, float dt, inout Random rnd) {
    float3 q = p.position * frequency;
    float t = Time * flow;
    float3 n = float3(sin(q.y * 1.7 + q.z * 2.3 + t), sin(q.z * 1.3 + q.x * 2.9 + t * 1.1), sin(q.x * 2.1 + q.y * 1.9 + t * 0.9));
    p.velocity += n * strength * dt;
}
)"},
    {"color_over_life", R"(module "色の変わり方"
param color first = (1, 1, 1, 1); // 始めの色 (はじめの値の色に掛ける)
param color last = (1, 0.3, 0.1, 0); // 終わりの色

void Update(inout Particle p, float dt, inout Random rnd) {
    p.color = p.startColor * lerp(first, last, LifeRatio(p));
}
)"},
    {"size_over_life", R"(module "大きさの変わり方"
param float first = 1 [0, 1000]; // 始めの倍率
param float last = 0 [0, 1000]; // 終わりの倍率

void Update(inout Particle p, float dt, inout Random rnd) {
    p.size = p.startSize * lerp(first, last, LifeRatio(p));
}
)"},
    {"floor", R"(module "床で跳ねる"
param float height = 0; // 床の高さ (Y)
param float bounce = 0.5 [0, 1]; // 跳ね返り
param float friction = 0.2 [0, 1]; // すべりにくさ
param bool vanish = false; // 当たったら消す

void Update(inout Particle p, float dt, inout Random rnd) {
    if (p.position.y + p.velocity.y * dt < height && p.velocity.y < 0) {
        if (vanish) {
            p.alive = 0;
            return;
        }
        p.position.y = height;
        p.velocity.y = -p.velocity.y * bounce;
        p.velocity.xz *= 1 - friction;
    }
}
)"},
};

}  // namespace

std::span<const BuiltinModule> BuiltinModules() { return kModules; }

const ModuleSource* FindBuiltin(std::string_view id) {
    static std::mutex mutex;
    static std::map<std::string, ModuleSource, std::less<>> parsed;
    std::lock_guard lock(mutex);
    if (const auto it = parsed.find(id); it != parsed.end()) return &it->second;
    for (const BuiltinModule& m : kModules) {
        if (m.id == id) return &parsed.emplace(std::string(id), ParseModule(m.text)).first->second;
    }
    return nullptr;
}

}  // namespace rm::particles
