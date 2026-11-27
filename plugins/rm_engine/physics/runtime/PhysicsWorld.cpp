// 物理 - 物理ワールド (Jolt Physics)。Jolt の型を見るのはこのファイルだけ (設計 9-f: 壁は薄く、
// 1 か所に)。

#include "rm_engine/physics/runtime/PhysicsWorld.h"

#include "base/Log.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_production/runtime/sdk/RmAbi.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/physics/runtime/Shapes.h"
#include "rm_engine/space2d/runtime/Transform2D.h"
#include "rm_engine/space3d/runtime/MeshBake.h"
#include "rm_engine/space3d/runtime/MeshFile.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/space3d/runtime/View.h"

// Jolt のヘッダーは /W4 で警告を 0 に保つ対象ではない。
#pragma warning(push, 0)
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/TaperedCylinderShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#pragma warning(pop)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace rm::physics {

namespace {

using game_production::Component;
using game_production::HookCall;
using game_production::Scene;
using game_production::SceneEntity;

// --- Jolt はプログラム全体で 1 度だけ (2 度はしない: Play / ■ を何度繰り返しても作るのはワールドだけ) ----------
// ジョブのスレッドとステップの作業メモリは、すべてのワールドで共有する (同時に進めるのは 1 つ:
// ゲームのスレッド)。プログラムが終わるまで生かしておく (スレッドとの終了順の問題を避ける)。
struct Jolt {
    JPH::TempAllocatorImpl* temp = nullptr;
    JPH::JobSystemThreadPool* jobs = nullptr;
};
Jolt& JoltOnce() {
    static Jolt j;
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        // Jolt の文 (と Debug での Jolt の確認の失敗) をログへ。前は何も書かずにブレークで止まっていた。
        JPH::Trace = [](const char* fmt, ...) {
            char text[512];
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(text, sizeof text, fmt, args);
            va_end(args);
            log::Info("物理 (Jolt): {}", text);
        };
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = [](const char* expression, const char* message, const char* file, JPH::uint line) {
            log::Error("物理 (Jolt) の確認に失敗しました: {} {} ({}:{})", expression, message ? message : "", file, line);
            return true;  // (デバッガーで止める)
        };)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        if (!JPH::VerifyJoltVersionID()) log::Error("物理: Jolt Physics のライブラリとヘッダーの設定が合っていません (RM.Common.props の JPH_*)");
        j.temp = new JPH::TempAllocatorImpl(32 * 1024 * 1024);
        const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
        j.jobs = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(std::min(hw - 1, 4u)));
    });
    return j;
}

// --- レイヤー: 動かないものは、動かないものと出会わない ---------------------------------
namespace Layer {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
}  // namespace Layer

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() == 0 ? "static" : "moving"; }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override { return layer == Layer::kMoving || bp.GetValue() == Layer::kMoving; }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == Layer::kMoving || b == Layer::kMoving; }
};

JPH::Vec3 J(Vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
Vec3 R(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }

JPH::Quat QuatOf(const space3d::Mat4& turn) {
    const JPH::Mat44 m(JPH::Vec4(turn.m[0][0], turn.m[0][1], turn.m[0][2], 0), JPH::Vec4(turn.m[1][0], turn.m[1][1], turn.m[1][2], 0),
                       JPH::Vec4(turn.m[2][0], turn.m[2][1], turn.m[2][2], 0), JPH::Vec4(0, 0, 0, 1));
    return m.GetQuaternion().Normalized();
}

space3d::Mat4 TurnOf(JPH::QuatArg q) {
    const JPH::Mat44 m = JPH::Mat44::sRotation(q);
    space3d::Mat4 out;
    for (int c = 0; c < 3; ++c) {
        const JPH::Vec3 col = m.GetColumn3(c);
        out.m[c][0] = col.GetX();
        out.m[c][1] = col.GetY();
        out.m[c][2] = col.GetZ();
        out.m[c][3] = 0;
    }
    return out;
}

// 数値の指紋 (FNV-1a): 設定の指紋が変わると、物体を作り直す。
struct Fingerprint {
    uint64_t h = 14695981039346656037ull;
    Fingerprint& Add(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
        return *this;
    }
    Fingerprint& Add(float v) {
        // 少し丸める: float のノイズの範囲で同じ拡縮は、同じものとする。
        const int64_t q = static_cast<int64_t>(std::llround(static_cast<double>(v) * 10000.0));
        return Add(&q, sizeof(q));
    }
    Fingerprint& Add(Vec3 v) { return Add(v.x).Add(v.y).Add(v.z); }
    Fingerprint& Add(bool v) { return Add(&v, 1); }
    Fingerprint& Add(int v) { return Add(&v, sizeof(v)); }
    Fingerprint& Add(const std::string& s) { return Add(s.data(), s.size()); }
};

uint32_t Key(JPH::BodyID id) { return id.GetIndexAndSequenceNumber(); }

// 1 ステップの間に何が触れ、何が離れたか (Jolt がそのジョブのスレッドから呼ぶ)。
class Contacts final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override {
        Count(a.GetID(), b.GetID(), +1);
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override { Count(pair.GetBody1ID(), pair.GetBody2ID(), -1); }

    size_t Touching() {
        std::lock_guard lock(mu_);
        return touching_.size();
    }
    // 前に呼んでから触れ始めた / 触れなくなった (物体ごとの) 組。
    void Take(std::vector<std::pair<uint32_t, uint32_t>>& began, std::vector<std::pair<uint32_t, uint32_t>>& ended) {
        std::lock_guard lock(mu_);
        began.assign(began_.begin(), began_.end());
        ended.assign(ended_.begin(), ended_.end());
        began_.clear();
        ended_.clear();
    }
    // ワールドから消えた物体: それが触れていたものは忘れる (その分の終了は出さない)。
    void Forget(uint32_t body) {
        std::lock_guard lock(mu_);
        std::erase_if(touching_, [&](const auto& kv) { return kv.first.first == body || kv.first.second == body; });
        std::erase_if(began_, [&](const auto& p) { return p.first == body || p.second == body; });
        std::erase_if(ended_, [&](const auto& p) { return p.first == body || p.second == body; });
    }

private:
    // 接触はサブ形状 (メッシュの三角形など) の組ごとに来る: 物体の組は、どれかが触れている間は触れている。
    void Count(JPH::BodyID x, JPH::BodyID y, int d) {
        uint32_t a = Key(x), b = Key(y);
        if (a > b) std::swap(a, b);
        std::lock_guard lock(mu_);
        // (知らない組への離れた知らせ - その物体はもう無く、Jolt が後から知らせる - は項目を作らない:
        // 前は 0 の項目がずっと残っていた)
        if (d < 0 && !touching_.contains({a, b})) return;
        int& n = touching_[{a, b}];
        const int before = n;
        n = std::max(0, n + d);
        if (before == 0 && n > 0) {
            if (!ended_.erase({a, b})) began_.insert({a, b});  // (1 ステップの中で離れてまた触れた: 何もしない)
        } else if (before > 0 && n == 0) {
            if (!began_.erase({a, b})) ended_.insert({a, b});
            touching_.erase({a, b});
        }
    }
    std::mutex mu_;
    std::map<std::pair<uint32_t, uint32_t>, int> touching_;
    std::set<std::pair<uint32_t, uint32_t>> began_, ended_;
};

}  // namespace

// ------------------------------------------------------------------------------------------------
struct PhysicsWorld::Impl {
    BroadPhaseLayers bpLayers;
    ObjectVsBroadPhase objectVsBp;
    ObjectPairs pairs;
    Contacts contacts;
    JPH::PhysicsSystem system;
    const game_production::AssetAccess* assets = nullptr;
    Vec3 gravityNow{0, 0, 0};  // システムが持っている重力 (違っていれば Step で設定する)
    double stepMs = 0;         // 直近の Update にかかった時間

    struct BodyRec {
        JPH::BodyID id;
        uint64_t settings = 0;           // 何として作ったか (違うと: 作り直す)
        uint64_t shape = 0, numbers = 0;  // その形と数値 (違うと: その場で変える)
        Motion motion = Motion::Static;
        bool flat = false, trigger = false;
        Vec3 wrotePosition{0, 0, 0};  // Transform に最後に書いた (または読んだ) 姿勢
        JPH::Quat wroteTurn = JPH::Quat::sIdentity();
        bool seen = false;
    };
    struct CharRec {
        JPH::Ref<JPH::CharacterVirtual> ch;
        uint64_t settings = 0;
        Character values;
        Vec3 walk{0, 0, 0};    // Move: 欲しい水平方向の速さ
        float jump = 0;        // Jump: 次のステップに頼んだ (0: 無し)
        Vec3 wrotePosition{0, 0, 0};
        bool seen = false;
    };
    std::unordered_map<int, BodyRec> bodies;   // エンティティごと
    std::unordered_map<int, CharRec> chars;    // エンティティごと
    std::unordered_map<uint32_t, int> owner;   // 物体 (Key) -> エンティティ (キャラクターの内側の物体も)
    std::unordered_set<uint32_t> triggers;     // トリガーである物体
    struct MeshEntry {
        JPH::RefConst<JPH::Shape> shape;  // null: 読めない (1 度だけ知らせる)
    };
    std::unordered_map<std::string, MeshEntry> meshes;  // メッシュコライダーのモデル、アセット ID ごと
    std::unordered_set<int> told;                       // 知らせたエンティティ (1 つにつき 1 度)

    Impl() {
        JoltOnce();
        system.Init(65536, 0, 65536, 20480, bpLayers, objectVsBp, pairs);
        system.SetContactListener(&contacts);
        system.SetGravity(JPH::Vec3::sZero());  // `gravityNow` の通り (Step がゲームのものに設定する)
    }
    ~Impl() {
        JPH::BodyInterface& bi = system.GetBodyInterface();
        for (auto& [e, b] : bodies) {
            bi.RemoveBody(b.id);
            bi.DestroyBody(b.id);
        }
        chars.clear();  // (その内側の物体も一緒に消える)
    }

    void TellOnce(int entity, const std::string& text) {
        if (told.insert(entity).second) log::Warn("物理: {}", text);
    }

    // --- 形 -------------------------------------------------------------------------------
    // エンティティのメッシュ (モデルか、ファイル無しの形: Shapes.h の MeshOf) の当たりの形。`hull`: それを包む凸の
    // 形 (凸包。動く物: Jolt の三角形のメッシュは動かない物だけ - 2026-11-26)。作ったものは覚えておく。
    JPH::RefConst<JPH::Shape> MeshShapeOf(const SceneEntity& e, bool hull) {
        const MeshSource src = MeshOf(e);
        if (!src.present || (!src.primitive && src.model.Empty())) return nullptr;
        const std::string what = src.primitive ? std::format("形 {}", static_cast<int>(*src.primitive)) : src.model.guid;
        auto [it, first] = meshes.try_emplace(what + (hull ? " (凸包)" : ""));
        if (!first) return it->second.shape;
        std::string err;
        std::optional<space3d::MeshData> mesh;
        if (src.primitive) {
            mesh = space3d::ShapeMesh(*src.primitive);
        } else {
            if (assets && assets->file) {
                if (const auto file = assets->file(src.model); !file.empty()) mesh = space3d::LoadMeshFile(file, &err);
            }
            if (!mesh && assets && assets->bytes) {
                if (const auto bytes = assets->bytes(src.model)) mesh = space3d::ReadBakedMesh(*bytes, &err);
            }
        }
        if (!mesh || mesh->indices.size() < 3) {
            log::Warn("物理: メッシュのコライダーのモデルを読めません ({}): {}", what, err.empty() ? std::string("モデルがありません") : err);
            return nullptr;
        }
        if (hull) {
            JPH::Array<JPH::Vec3> points;
            points.reserve(mesh->vertices.size());
            Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            for (const auto& v : mesh->vertices) {
                points.push_back(JPH::Vec3(v.position.x, v.position.y, v.position.z));
                lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
                hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
            }
            const float extent = std::min({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
            JPH::ConvexHullShapeSettings settings(points, std::min(JPH::cDefaultConvexRadius, std::max(extent, 0.f) * 0.25f));
            const JPH::ShapeSettings::ShapeResult made = settings.Create();
            if (!made.HasError()) {
                it->second.shape = made.Get();
                return it->second.shape;
            }
            // 平ら (平面) などで凸の形にできない: それを囲む箱で当てる (薄くても 0.02)。
            log::Info("物理: メッシュ ({}) を包む凸の形を作れないので、囲む箱で当てます: {}", what, made.GetError().c_str());
            const Vec3 half{std::max((hi.x - lo.x) * 0.5f, 0.01f), std::max((hi.y - lo.y) * 0.5f, 0.01f), std::max((hi.z - lo.z) * 0.5f, 0.01f)};
            const Vec3 center{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
            JPH::RefConst<JPH::Shape> box = new JPH::BoxShape(J(half), std::min({half.x, half.y, half.z, JPH::cDefaultConvexRadius}) * 0.5f);
            it->second.shape = new JPH::RotatedTranslatedShape(J(center), JPH::Quat::sIdentity(), box);
            return it->second.shape;
        }
        JPH::VertexList vertices;
        vertices.reserve(mesh->vertices.size());
        for (const auto& v : mesh->vertices) vertices.push_back(JPH::Float3(v.position.x, v.position.y, v.position.z));
        JPH::IndexedTriangleList triangles;
        for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
            triangles.push_back(JPH::IndexedTriangle(mesh->indices[i], mesh->indices[i + 1], mesh->indices[i + 2], 0));
        }
        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        const JPH::ShapeSettings::ShapeResult made = settings.Create();
        if (made.HasError()) {
            log::Warn("物理: メッシュのコライダーを作れません ({}): {}", what, made.GetError().c_str());
            return nullptr;
        }
        it->second.shape = made.Get();
        return it->second.shape;
    }

    JPH::RefConst<JPH::Shape> ShapeOf(const ShapeDims& d, const SceneEntity& e, bool hull) {
        JPH::RefConst<JPH::Shape> s;
        switch (d.kind) {
            case ShapeDims::Kind::Box: s = new JPH::BoxShape(J(d.half), std::min({d.half.x, d.half.y, d.half.z, JPH::cDefaultConvexRadius}) * 0.5f); break;
            case ShapeDims::Kind::Sphere: s = new JPH::SphereShape(d.radius); break;
            case ShapeDims::Kind::Capsule:
                s = d.halfHeight > 1e-4f ? JPH::RefConst<JPH::Shape>(new JPH::CapsuleShape(d.halfHeight, d.radius))
                                         : JPH::RefConst<JPH::Shape>(new JPH::SphereShape(d.radius));
                break;
            case ShapeDims::Kind::Cylinder:
                s = new JPH::CylinderShape(d.halfHeight, d.radius, std::min({d.halfHeight, d.radius, JPH::cDefaultConvexRadius}) * 0.5f);
                break;
            case ShapeDims::Kind::Cone: {
                // 先の半径が 0 の、先細りの円柱 (Jolt: 円錐そのものの式で当たる。転がりもなめらか)。
                const JPH::ShapeSettings::ShapeResult made = JPH::TaperedCylinderShapeSettings(d.halfHeight, 0.f, d.radius, 0.f).Create();
                if (made.HasError()) return nullptr;
                s = made.Get();
                break;
            }
            case ShapeDims::Kind::Pyramid: {
                // 5 つの角を包む凸の形 (凸包): 四角錐はちょうどその形。
                JPH::Array<JPH::Vec3> points;
                for (const Vec3& p : PyramidPoints(d)) points.push_back(JPH::Vec3(p.x, p.y, p.z));
                const float radius = std::min({d.half.x, d.half.y, d.half.z, JPH::cDefaultConvexRadius}) * 0.5f;
                const JPH::ShapeSettings::ShapeResult made = JPH::ConvexHullShapeSettings(points, radius).Create();
                if (made.HasError()) return nullptr;
                s = made.Get();
                break;
            }
            case ShapeDims::Kind::Mesh: {
                JPH::RefConst<JPH::Shape> mesh = MeshShapeOf(e, hull);
                if (!mesh) return nullptr;
                const Vec3 k = d.meshScale;
                s = std::fabs(k.x - 1) + std::fabs(k.y - 1) + std::fabs(k.z - 1) < 1e-5f ? mesh
                                                                                           : JPH::RefConst<JPH::Shape>(new JPH::ScaledShape(mesh, J(k)));
                break;
            }
        }
        if (d.offset.x != 0 || d.offset.y != 0 || d.offset.z != 0) s = new JPH::RotatedTranslatedShape(J(d.offset), JPH::Quat::sIdentity(), s);
        return s;
    }

    // --- 物体 --------------------------------------------------------------------------------
    void Remove(int entity) {
        const auto it = bodies.find(entity);
        if (it == bodies.end()) return;
        JPH::BodyInterface& bi = system.GetBodyInterface();
        bi.RemoveBody(it->second.id);
        bi.DestroyBody(it->second.id);
        contacts.Forget(Key(it->second.id));
        owner.erase(Key(it->second.id));
        triggers.erase(Key(it->second.id));
        bodies.erase(it);
    }

    void RemoveCharacter(int entity) {
        const auto it = chars.find(entity);
        if (it == chars.end()) return;
        const JPH::BodyID inner = it->second.ch->GetInnerBodyID();
        if (!inner.IsInvalid()) {
            contacts.Forget(Key(inner));
            owner.erase(Key(inner));
        }
        chars.erase(it);
    }

    // `e` の物体を、`place` に作る / 保つ / 動かす。`created`: 作った物体を数える (たくさん作ったら
    // ブロードフェーズを整える)。false: 物体が無い (コライダーが無い; まだ読めないメッシュ)。
    bool Sync(SceneEntity& e, const EntityPlace& place, float dt, int* created) {
        const int id = e.data.id;
        const Component* cc = e.FindComponent(reflect::TypeOf<Collider>().name);
        const Component* cc2 = e.FindComponent(reflect::TypeOf<Collider2D>().name);
        const bool flat = place.flat;
        const Collider* c3 = !flat && cc && cc->Known() ? static_cast<const Collider*>(cc->value.Get()) : nullptr;
        const Collider2D* c2 = flat && cc2 && cc2->Known() ? static_cast<const Collider2D*>(cc2->value.Get()) : nullptr;
        if (!c3 && !c2) {
            Remove(id);
            return false;
        }
        const Component* rbc = e.FindComponent(reflect::TypeOf<Rigidbody>().name);
        const Rigidbody* rb = rbc && rbc->Known() ? static_cast<const Rigidbody*>(rbc->value.Get()) : nullptr;
        const Pose pose = flat ? PoseOf(place.world2) : PoseOf(place.world3);
        const ShapeDims dims = c3 ? DimsOf(*c3, pose.scale) : DimsOf(*c2, pose.scale);
        const bool trigger = c3 ? c3->trigger : c2->trigger;
        const Motion motion = rb ? rb->motion : Motion::Static;
        // メッシュのコライダー: 動かない物は三角形のまま、動く物 (キネマティックも) はそれを包む凸の形 (2026-11-26。
        // 前は動かない物として扱っていた)。
        const bool hull = dims.kind == ShapeDims::Kind::Mesh && motion != Motion::Static;
        // 物体を作り直す条件 (別の種類の物体) / そのまま変える部分 - 形と数値 (2026-11-19: 前は、どんな変更でも
        // 作り直していた: OnHit / OnTriggerEnter が終了なしにまた来て、動く物体が止まった)。
        Fingerprint fp;
        fp.Add(flat).Add(trigger).Add(static_cast<int>(motion));
        Fingerprint shapeFp;
        shapeFp.Add(static_cast<int>(dims.kind)).Add(dims.half).Add(dims.radius).Add(dims.halfHeight).Add(dims.offset).Add(dims.meshScale);
        if (dims.kind == ShapeDims::Kind::Mesh) {
            const MeshSource src = MeshOf(e);
            shapeFp.Add(src.model.guid).Add(src.primitive ? static_cast<int>(*src.primitive) : -1).Add(hull);
        }
        Fingerprint numbersFp;
        if (rb) numbersFp.Add(rb->mass).Add(rb->friction).Add(rb->bounciness).Add(rb->gravityScale).Add(rb->lockRotationX).Add(rb->lockRotationY).Add(rb->lockRotationZ);
        // 平面では: X、Y と Z まわりの回転だけ。3D では: 固定していない回転。
        JPH::EAllowedDOFs dofs = flat ? (JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::RotationZ)
                                      : JPH::EAllowedDOFs::All;
        if (rb && rb->lockRotationX) dofs = dofs & ~JPH::EAllowedDOFs::RotationX;
        if (rb && rb->lockRotationY) dofs = dofs & ~JPH::EAllowedDOFs::RotationY;
        if (rb && rb->lockRotationZ) dofs = dofs & ~JPH::EAllowedDOFs::RotationZ;
        JPH::BodyInterface& bi = system.GetBodyInterface();
        const JPH::RVec3 position = J(pose.position);
        const JPH::Quat turn = QuatOf(pose.turn);
        auto found = bodies.find(id);
        if (found != bodies.end() && found->second.settings != fp.h) {
            Remove(id);
            found = bodies.end();
        }
        if (found == bodies.end()) {
            JPH::RefConst<JPH::Shape> shape = ShapeOf(dims, e, hull);
            if (!shape) return false;
            // 何も動かさないトリガーはキネマティックにする (動かないものも見える)。
            const JPH::EMotionType type = motion == Motion::Dynamic   ? JPH::EMotionType::Dynamic
                                          : motion == Motion::Kinematic || trigger ? JPH::EMotionType::Kinematic
                                                                                   : JPH::EMotionType::Static;
            JPH::BodyCreationSettings s(shape, position, turn, type, type == JPH::EMotionType::Static ? Layer::kStatic : Layer::kMoving);
            s.mUserData = static_cast<JPH::uint64>(id);
            s.mIsSensor = trigger;
            s.mCollideKinematicVsNonDynamic = trigger;
            s.mFriction = rb ? rb->friction : 0.5f;
            s.mRestitution = rb ? rb->bounciness : 0.f;
            s.mGravityFactor = rb ? rb->gravityScale : 1.f;
            if (type == JPH::EMotionType::Dynamic) {
                s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                s.mMassPropertiesOverride.mMass = std::max(rb->mass, 0.001f);
                // 速いもの (弾) は壁を抜けない (LinearCast)。ただし形の内側の半径が 0 の物 (円錐: 先の半径が 0、
                // 凸包にしないメッシュ、つぶれた凸包) は Jolt がこれを扱えない (Debug では Jolt の確認で止まっていた:
                // 2026-11-26) ので、ふつうの動き (Discrete) にする。
                s.mMotionQuality = shape->GetInnerRadius() > 0.f ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
            }
            if (type == JPH::EMotionType::Dynamic) s.mAllowedDOFs = dofs;
            const JPH::BodyID body = bi.CreateAndAddBody(s, type == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
            if (body.IsInvalid()) {
                TellOnce(id, "物の数が多すぎて体を作れません (65536 まで)");
                return false;
            }
            BodyRec r;
            r.id = body;
            r.settings = fp.h;
            r.shape = shapeFp.h;
            r.numbers = numbersFp.h;
            r.motion = motion;
            r.flat = flat;
            r.trigger = trigger;
            r.wrotePosition = pose.position;
            r.wroteTurn = turn;
            owner[Key(body)] = id;
            if (trigger) triggers.insert(Key(body));
            found = bodies.emplace(id, r).first;
            if (created) ++*created;
        }
        BodyRec& r = found->second;
        r.seen = true;
        // 形か数値が変わった (脈打つ大きさ、再生中に設定した摩擦): その場で変える - 触れているものと
        // 動き方はそのまま。
        if (r.shape != shapeFp.h) {
            JPH::RefConst<JPH::Shape> shape = ShapeOf(dims, e, hull);
            if (!shape) {
                Remove(id);
                return false;
            }
            bi.SetShape(r.id, shape, true, r.motion == Motion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
            r.shape = shapeFp.h;
            r.numbers = ~numbersFp.h;  // (質量はいまは形のもの: 下でもう一度設定する)
        }
        if (r.numbers != numbersFp.h) {
            bi.SetFriction(r.id, rb ? rb->friction : 0.5f);
            bi.SetRestitution(r.id, rb ? rb->bounciness : 0.f);
            bi.SetGravityFactor(r.id, rb ? rb->gravityScale : 1.f);
            if (rb && bi.GetMotionType(r.id) == JPH::EMotionType::Dynamic) {
                JPH::BodyLockWrite lock(system.GetBodyLockInterface(), r.id);
                if (lock.Succeeded()) {
                    JPH::Body& body = lock.GetBody();
                    JPH::MassProperties mass = body.GetShape()->GetMassProperties();
                    mass.ScaleToMass(std::max(rb->mass, 0.001f));
                    body.GetMotionProperties()->SetMassProperties(dofs, mass);
                }
            }
            r.numbers = numbersFp.h;
        }
        // Transform がいま言っている所: スクリプト (またはインスペクター) が動かした -> 物体をそこへ動かす。
        // (Transform は、角度を通る間の float のノイズの範囲で、書いたものをそのまま持つ)
        const bool moved = std::fabs(pose.position.x - r.wrotePosition.x) + std::fabs(pose.position.y - r.wrotePosition.y) +
                               std::fabs(pose.position.z - r.wrotePosition.z) > 1e-4f ||
                           std::fabs(turn.Dot(r.wroteTurn)) < 1 - 1e-5f;
        const bool kinematic = bi.GetMotionType(r.id) == JPH::EMotionType::Kinematic;
        if (kinematic && dt > 0) {
            bi.MoveKinematic(r.id, position, turn, dt);  // ステップの間に動かす (道をふさぐものを押す)
        } else if (moved) {
            bi.SetPositionAndRotation(r.id, position, turn, r.motion == Motion::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        }
        r.wrotePosition = pose.position;
        r.wroteTurn = turn;
        return true;
    }

    void SyncCharacter(SceneEntity& e, const EntityPlace& place) {
        const int id = e.data.id;
        const Component* cc = e.FindComponent(reflect::TypeOf<Character>().name);
        if (place.flat || !cc || !cc->Known()) {
            RemoveCharacter(id);
            return;
        }
        const Character& c = *static_cast<const Character*>(cc->value.Get());
        Fingerprint fp;
        fp.Add(c.radius).Add(c.height).Add(c.maxSlope).Add(c.stepHeight).Add(c.mass);
        const Pose pose = PoseOf(place.world3);
        auto found = chars.find(id);
        if (found != chars.end() && found->second.settings != fp.h) {
            RemoveCharacter(id);
            found = chars.end();
        }
        if (found == chars.end()) {
            const ShapeDims d = DimsOf(c);
            JPH::RefConst<JPH::Shape> capsule = new JPH::RotatedTranslatedShape(J(d.offset), JPH::Quat::sIdentity(), new JPH::CapsuleShape(d.halfHeight, d.radius));
            JPH::CharacterVirtualSettings s;
            s.mShape = capsule;
            s.mInnerBodyShape = capsule;  // 他のもの (弾、トリガー) が出会う物体
            s.mInnerBodyLayer = Layer::kMoving;
            s.mMaxSlopeAngle = JPH::DegreesToRadians(std::clamp(c.maxSlope, 0.f, 89.f));
            s.mMass = std::max(c.mass, 0.1f);
            s.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -d.radius);  // 下の球が立つ
            CharRec r;
            r.ch = new JPH::CharacterVirtual(&s, J(pose.position), JPH::Quat::sIdentity(), static_cast<JPH::uint64>(id), &system);
            r.settings = fp.h;
            r.wrotePosition = pose.position;
            const JPH::BodyID inner = r.ch->GetInnerBodyID();
            if (!inner.IsInvalid()) owner[Key(inner)] = id;
            found = chars.emplace(id, std::move(r)).first;
        }
        CharRec& r = found->second;
        r.values = c;
        r.seen = true;
        const Vec3 p = pose.position;
        if (std::fabs(p.x - r.wrotePosition.x) + std::fabs(p.y - r.wrotePosition.y) + std::fabs(p.z - r.wrotePosition.z) > 1e-4f) {
            r.ch->SetPosition(J(p));  // スクリプトがどこかに置いた
        }
    }

    // すべてのエンティティの位置 (親が先)、そして部品の通りに、すべての物体を作る / 保つ / 消す。
    void SyncAll(Scene& scene, float dt) {
        for (auto& [e, b] : bodies) b.seen = false;
        for (auto& [e, c] : chars) c.seen = false;
        std::unordered_map<int, EntityPlace> places;
        int created = 0;
        for (const game_production::TreeEntry& t : scene.TreeOrder()) {
            SceneEntity* e = scene.Find(t.entity->data.id);
            if (!e) continue;
            const auto parent = places.find(e->data.parent);
            EntityPlace p;
            const bool placed = PlaceUnder(*e, parent != places.end() ? &parent->second : nullptr, p);
            places[e->data.id] = p;
            if (!placed || !t.activeInHierarchy) continue;  // 位置が無い / 切ってある: 物体は無い (下で外す)
            Sync(*e, p, dt, &created);
            SyncCharacter(*e, p);
        }
        std::vector<int> gone;
        for (auto& [e, b] : bodies) {
            if (!b.seen) gone.push_back(e);
        }
        for (const int e : gone) Remove(e);
        gone.clear();
        for (auto& [e, c] : chars) {
            if (!c.seen) gone.push_back(e);
        }
        for (const int e : gone) RemoveCharacter(e);
        if (created >= 64) system.OptimizeBroadPhase();  // 一度にたくさん新しくなった: ブロードフェーズを整える
        placesCache = std::move(places);
    }
    std::unordered_map<int, EntityPlace> placesCache;  // 直近の SyncAll の分 (書き戻し用)

    // エンティティ 1 つだけ (ステップが物体を作る前に、スクリプトが頼んだ時)。
    bool SyncOne(Scene& scene, int id) {
        if (bodies.contains(id) || chars.contains(id)) return true;
        SceneEntity* e = scene.Find(id);
        if (!e) return false;
        // その親の位置、上から順に。
        std::vector<const SceneEntity*> chain;
        for (const SceneEntity* x = e; x && chain.size() <= scene.Entities().size(); x = scene.Find(x->data.parent)) chain.push_back(x);
        std::reverse(chain.begin(), chain.end());
        EntityPlace p;
        bool placed = false;
        for (const SceneEntity* x : chain) {
            if (!x->data.active) return false;  // 切ってある (それ自身か親が)
            const EntityPlace up = p;
            placed = PlaceUnder(*x, x == chain.front() ? nullptr : &up, p);
        }
        if (!placed) return false;
        placesCache[id] = p;
        SyncCharacter(*e, p);
        return Sync(*e, p, 0, nullptr) || chars.contains(id);
    }

    // --- Transform への書き戻し ----------------------------------------------------------------
    void WriteBack(Scene& scene) {
        const std::string t3 = reflect::TypeOf<space3d::Transform>().name, t2 = reflect::TypeOf<space2d::Transform2D>().name;
        const JPH::BodyInterface& bi = system.GetBodyInterface();
        for (auto& [id, r] : bodies) {
            if (r.motion != Motion::Dynamic || !bi.IsActive(r.id)) continue;  // 眠っている: 元の所のまま
            const auto place = placesCache.find(id);
            SceneEntity* e = scene.Find(id);
            if (place == placesCache.end() || !e) continue;
            JPH::RVec3 position;
            JPH::Quat turn;
            bi.GetPositionAndRotation(r.id, position, turn);
            WritePose(*e, place->second, R(position), turn, t3, t2);
            r.wrotePosition = R(position);
            r.wroteTurn = turn;
        }
        for (auto& [id, r] : chars) {
            const auto place = placesCache.find(id);
            SceneEntity* e = scene.Find(id);
            if (place == placesCache.end() || !e) continue;
            const Vec3 p = R(r.ch->GetPosition());
            const Pose now = PoseOf(place->second.world3);
            WritePose(*e, place->second, p, QuatOf(now.turn), t3, t2);
            r.wrotePosition = p;
        }
    }

    static void WritePose(SceneEntity& e, const EntityPlace& place, Vec3 position, JPH::QuatArg turn, const std::string& t3, const std::string& t2) {
        if (!place.flat) {
            Component* c = e.FindComponent(t3);
            if (!c || !c->Known()) return;
            auto& t = *static_cast<space3d::Transform*>(c->value.Get());
            const Pose was = PoseOf(place.world3);
            space3d::Mat4 world = TurnOf(turn);
            for (int k = 0; k < 3; ++k) {
                const float s = k == 0 ? was.scale.x : k == 1 ? was.scale.y : was.scale.z;
                for (int r = 0; r < 3; ++r) world.m[k][r] *= s;
            }
            world.m[3][0] = position.x, world.m[3][1] = position.y, world.m[3][2] = position.z;
            const space3d::WorldPlacement hint = space3d::Decompose(place.world3, t.rotation);
            const space3d::WorldPlacement want = space3d::Decompose(world, hint.rotation);
            t = space3d::FromWorld(t, place.parent3, want);
        } else {
            Component* c = e.FindComponent(t2);
            if (!c || !c->Known()) return;
            auto& t = *static_cast<space2d::Transform2D*>(c->value.Get());
            const Pose was = PoseOf(place.world2);
            const float angle = AngleOf({position, TurnOf(turn), was.scale});
            const space2d::Mat3 world = space2d::Translation({position.x, position.y}) * space2d::Rotation(angle) * space2d::Scaling({was.scale.x, was.scale.y});
            const space2d::WorldPlacement2D hint = space2d::Decompose(place.world2, t.rotation);
            space2d::WorldPlacement2D want = space2d::Decompose(world, hint.rotation);
            want.order = place.parentOrder + t.order;
            t = space2d::FromWorld(t, place.parent2, place.parentOrder, want);
        }
    }

    // --- キャラクターが歩く ----------------------------------------------------------------------------
    void StepCharacters(float dt, Vec3 gravity) {
        JoltOnce();
        for (auto& [id, r] : chars) {
            JPH::CharacterVirtual& ch = *r.ch;
            const JPH::Vec3 g = J(gravity) * r.values.gravityScale;
            ch.UpdateGroundVelocity();
            const bool ground = ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
            const JPH::Vec3 v = ch.GetLinearVelocity();
            // Jolt のやり方: 地面にいて、離れる向きに動いていない -> 地面の速さ; 空中 -> 自分の落下。
            JPH::Vec3 vertical = ground && (v - ch.GetGroundVelocity()).Dot(ch.GetUp()) < 0.1f ? ch.GetGroundVelocity() : JPH::Vec3(0, v.GetY(), 0);
            vertical += g * dt;
            if (r.jump > 0 && ground) vertical += ch.GetUp() * r.jump;
            r.jump = 0;
            const JPH::Vec3 walk(r.walk.x, 0, r.walk.z);
            ch.SetLinearVelocity(JPH::Vec3(walk.GetX(), vertical.GetY(), walk.GetZ()));
            JPH::CharacterVirtual::ExtendedUpdateSettings s;
            s.mWalkStairsStepUp = JPH::Vec3(0, std::max(r.values.stepHeight, 0.f), 0);
            const JPH::DefaultBroadPhaseLayerFilter bpFilter(objectVsBp, Layer::kMoving);
            const JPH::DefaultObjectLayerFilter layerFilter(pairs, Layer::kMoving);
            ch.ExtendedUpdate(dt, g, s, bpFilter, layerFilter, JPH::BodyFilter(), JPH::ShapeFilter(), *JoltOnce().temp);
        }
    }

    // --- 触れたもの -------------------------------------------------------------------------------
    void Tell(std::vector<HookCall>* hooks) {
        std::vector<std::pair<uint32_t, uint32_t>> began, ended;
        contacts.Take(began, ended);
        if (!hooks) return;
        struct Event {
            int kind, a, b;
        };
        std::vector<Event> events;
        auto add = [&](const std::pair<uint32_t, uint32_t>& p, bool begin) {
            const auto ea = owner.find(p.first), eb = owner.find(p.second);
            if (ea == owner.end() || eb == owner.end() || ea->second == eb->second) return;
            const bool trigger = triggers.contains(p.first) || triggers.contains(p.second);
            if (!trigger && !begin) return;  // (固いものどうしが離れた: フックは無い)
            const int kind = trigger ? (begin ? RM_HOOK_TRIGGER_ENTER : RM_HOOK_TRIGGER_EXIT) : RM_HOOK_HIT;
            events.push_back({kind, std::min(ea->second, eb->second), std::max(ea->second, eb->second)});
        };
        for (const auto& p : ended) add(p, false);
        for (const auto& p : began) add(p, true);
        // Jolt のスレッドが何をしても同じ順番にする: 終了が先、そのあとエンティティ順。
        std::sort(events.begin(), events.end(), [](const Event& x, const Event& y) {
            const int kx = x.kind == RM_HOOK_TRIGGER_EXIT ? 0 : 1, ky = y.kind == RM_HOOK_TRIGGER_EXIT ? 0 : 1;
            if (kx != ky) return kx < ky;
            if (x.a != y.a) return x.a < y.a;
            if (x.b != y.b) return x.b < y.b;
            return x.kind < y.kind;
        });
        events.erase(std::unique(events.begin(), events.end(), [](const Event& x, const Event& y) { return x.kind == y.kind && x.a == y.a && x.b == y.b; }),
                     events.end());
        for (const Event& ev : events) {
            hooks->push_back({ev.a, ev.kind, ev.b});
            hooks->push_back({ev.b, ev.kind, ev.a});
        }
    }
};

// ------------------------------------------------------------------------------------------------
PhysicsWorld::PhysicsWorld() : impl_(std::make_unique<Impl>()) {}
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::Step(Scene& scene, float dt, Vec3 gravity, std::vector<HookCall>* hooks, const game_production::AssetAccess* assets) {
    Impl& w = *impl_;
    if (assets) w.assets = assets;
    w.SyncAll(scene, dt);
    if (dt <= 0) return;
    if (gravity.x != w.gravityNow.x || gravity.y != w.gravityNow.y || gravity.z != w.gravityNow.z) {
        // 新しい重力 (シーンの 重力 が変わった): 止まっている物体は、それを感じるよう起きる。
        w.gravityNow = gravity;
        w.system.SetGravity(J(gravity));
        JPH::BodyInterface& bi = w.system.GetBodyInterface();
        for (const auto& [e, b] : w.bodies) {
            if (b.motion == Motion::Dynamic) bi.ActivateBody(b.id);
        }
    }
    w.StepCharacters(dt, gravity);
    Jolt& j = JoltOnce();
    const auto t0 = std::chrono::steady_clock::now();
    w.system.Update(dt, 1, j.temp, j.jobs);
    w.stepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    w.WriteBack(scene);
    w.Tell(hooks);
}

bool PhysicsWorld::AddForce(Scene& scene, int entity, Vec3 force, bool impulse) {
    Impl& w = *impl_;
    if (!w.SyncOne(scene, entity)) return false;
    const auto it = w.bodies.find(entity);
    if (it == w.bodies.end() || it->second.motion != Motion::Dynamic) return false;
    JPH::BodyInterface& bi = w.system.GetBodyInterface();
    if (impulse) {
        bi.AddImpulse(it->second.id, J(force));
    } else {
        bi.AddForce(it->second.id, J(force));
    }
    return true;
}

std::optional<Vec3> PhysicsWorld::Velocity(Scene& scene, int entity) {
    Impl& w = *impl_;
    if (!w.SyncOne(scene, entity)) return std::nullopt;
    if (const auto c = w.chars.find(entity); c != w.chars.end()) return R(c->second.ch->GetLinearVelocity());
    const auto it = w.bodies.find(entity);
    if (it == w.bodies.end()) return std::nullopt;
    return R(w.system.GetBodyInterface().GetLinearVelocity(it->second.id));
}

bool PhysicsWorld::SetVelocity(Scene& scene, int entity, Vec3 velocity) {
    Impl& w = *impl_;
    if (!w.SyncOne(scene, entity)) return false;
    if (const auto c = w.chars.find(entity); c != w.chars.end()) {
        c->second.walk = velocity;  // キャラクター: その歩き (落下はキャラクター自身のもの)
        return true;
    }
    const auto it = w.bodies.find(entity);
    if (it == w.bodies.end() || it->second.motion == Motion::Static) return false;
    w.system.GetBodyInterface().SetLinearVelocity(it->second.id, J(velocity));
    return true;
}

namespace {

// レイはトリガーと、除くエンティティ (自分のキャラクターの体) の物体を飛ばす。
class RayFilter final : public JPH::BodyFilter {
public:
    RayFilter(const std::unordered_map<uint32_t, int>& owner, int ignore) : owner_(owner), ignore_(ignore) {}
    bool ShouldCollide(const JPH::BodyID& id) const override {
        if (ignore_ == 0) return true;
        const auto o = owner_.find(Key(id));
        return o == owner_.end() || o->second != ignore_;
    }
    bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }

private:
    const std::unordered_map<uint32_t, int>& owner_;
    int ignore_;
};

}  // namespace

std::optional<PhysicsWorld::RayHit> PhysicsWorld::Raycast(Vec3 origin, Vec3 direction, float maxDistance, int ignoreEntity) const {
    const Impl& w = *impl_;
    const Vec3 d = space3d::Normalize(direction);
    if (space3d::Length(d) == 0 || !(maxDistance > 0)) return std::nullopt;
    const float reach = std::min(maxDistance, 1e6f);
    const JPH::RRayCast ray(J(origin), J(d) * reach);
    JPH::RayCastResult hit;
    const RayFilter filter(w.owner, ignoreEntity);
    if (!w.system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return std::nullopt;
    RayHit out;
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    out.point = R(point);
    out.distance = hit.mFraction * reach;
    const JPH::BodyLockRead lock(w.system.GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        out.normal = R(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point));
        out.entity = static_cast<int>(body.GetUserData());
    }
    if (const auto o = w.owner.find(Key(hit.mBodyID)); o != w.owner.end()) out.entity = o->second;
    return out;
}

bool PhysicsWorld::Move(Scene& scene, int entity, Vec3 velocity) {
    Impl& w = *impl_;
    w.SyncOne(scene, entity);
    const auto c = w.chars.find(entity);
    if (c == w.chars.end()) return false;
    c->second.walk = velocity;
    return true;
}

bool PhysicsWorld::Jump(Scene& scene, int entity, float speed) {
    Impl& w = *impl_;
    w.SyncOne(scene, entity);
    const auto c = w.chars.find(entity);
    if (c == w.chars.end()) return false;
    c->second.jump = std::max(speed, 0.f);
    return true;
}

bool PhysicsWorld::Grounded(int entity) const {
    const auto c = impl_->chars.find(entity);
    return c != impl_->chars.end() && c->second.ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

std::optional<Vec3> PhysicsWorld::GroundNormal(int entity) const {
    const auto c = impl_->chars.find(entity);
    if (c == impl_->chars.end() || !c->second.ch->IsSupported()) return std::nullopt;
    return R(c->second.ch->GetGroundNormal());
}

size_t PhysicsWorld::Bodies() const { return impl_->bodies.size(); }
size_t PhysicsWorld::Characters() const { return impl_->chars.size(); }
size_t PhysicsWorld::ActiveBodies() const { return impl_->system.GetNumActiveBodies(JPH::EBodyType::RigidBody); }
size_t PhysicsWorld::TouchingPairs() const { return impl_->contacts.Touching(); }
double PhysicsWorld::LastStepMs() const { return impl_->stepMs; }

PhysicsWorld* FindWorldOf(Scene& scene) { return static_cast<PhysicsWorld*>(scene.PlayStatesOf().Slot("rm_engine.physics.world").get()); }

PhysicsWorld& WorldOf(Scene& scene) { return scene.PlayState<PhysicsWorld>("rm_engine.physics.world"); }

void RunPhysics(game_production::SystemContext& ctx) {
    const PhysicsSettings* settings = ctx.scene.Setting<PhysicsSettings>();
    WorldOf(ctx.scene).Step(ctx.scene, ctx.dt, settings ? settings->gravity : PhysicsSettings{}.gravity, ctx.hooks, ctx.assets);
}

}  // namespace rm::physics
