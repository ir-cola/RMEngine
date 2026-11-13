// 物理 - the physics world (Jolt Physics). The only file that sees Jolt's types (design 9-f: the wall is
// thin and in one place).

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

// Jolt's headers are not ours to keep warning-free at /W4.
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
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#pragma warning(pop)

#include <algorithm>
#include <cmath>
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

// --- Jolt, once for the whole program (never twice: Play / ■ again and again only make worlds) ----------
// The job threads and the step's scratch memory are shared by every world (one steps at a time: the
// game's thread). Left alive until the program ends (no order-of-exit trouble with the threads).
struct Jolt {
    JPH::TempAllocatorImpl* temp = nullptr;
    JPH::JobSystemThreadPool* jobs = nullptr;
};
Jolt& JoltOnce() {
    static Jolt j;
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        if (!JPH::VerifyJoltVersionID()) log::Error("物理: Jolt Physics のライブラリとヘッダーの設定が合っていません (RM.Common.props の JPH_*)");
        j.temp = new JPH::TempAllocatorImpl(32 * 1024 * 1024);
        const unsigned hw = std::max(2u, std::thread::hardware_concurrency());
        j.jobs = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, static_cast<int>(std::min(hw - 1, 4u)));
    });
    return j;
}

// --- layers: what does not move never meets what does not move ---------------------------------
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

// A fingerprint of numbers (FNV-1a): a body is made again when its settings' fingerprint changes.
struct Fingerprint {
    uint64_t h = 14695981039346656037ull;
    Fingerprint& Add(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
        return *this;
    }
    Fingerprint& Add(float v) {
        // Rounded a little: a scale that is the same give or take float noise is the same.
        const int64_t q = static_cast<int64_t>(std::llround(static_cast<double>(v) * 10000.0));
        return Add(&q, sizeof(q));
    }
    Fingerprint& Add(Vec3 v) { return Add(v.x).Add(v.y).Add(v.z); }
    Fingerprint& Add(bool v) { return Add(&v, 1); }
    Fingerprint& Add(int v) { return Add(&v, sizeof(v)); }
    Fingerprint& Add(const std::string& s) { return Add(s.data(), s.size()); }
};

uint32_t Key(JPH::BodyID id) { return id.GetIndexAndSequenceNumber(); }

// What touched and what parted during a step (Jolt calls this from its job threads).
class Contacts final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override {
        Count(a.GetID(), b.GetID(), +1);
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override { Count(pair.GetBody1ID(), pair.GetBody2ID(), -1); }

    // The pairs (by body) that began / ended touching since the last call.
    void Take(std::vector<std::pair<uint32_t, uint32_t>>& began, std::vector<std::pair<uint32_t, uint32_t>>& ended) {
        std::lock_guard lock(mu_);
        began.assign(began_.begin(), began_.end());
        ended.assign(ended_.begin(), ended_.end());
        began_.clear();
        ended_.clear();
    }
    // A body that left the world: what it touched is forgotten (no exit for it).
    void Forget(uint32_t body) {
        std::lock_guard lock(mu_);
        std::erase_if(touching_, [&](const auto& kv) { return kv.first.first == body || kv.first.second == body; });
        std::erase_if(began_, [&](const auto& p) { return p.first == body || p.second == body; });
        std::erase_if(ended_, [&](const auto& p) { return p.first == body || p.second == body; });
    }

private:
    // Contacts come per pair of sub shapes (a mesh's triangles ...): a pair of bodies touches while any do.
    void Count(JPH::BodyID x, JPH::BodyID y, int d) {
        uint32_t a = Key(x), b = Key(y);
        if (a > b) std::swap(a, b);
        std::lock_guard lock(mu_);
        int& n = touching_[{a, b}];
        const int before = n;
        n = std::max(0, n + d);
        if (before == 0 && n > 0) {
            if (!ended_.erase({a, b})) began_.insert({a, b});  // (parted and touched again within a step: nothing)
        } else if (before > 0 && n == 0) {
            if (!began_.erase({a, b})) ended_.insert({a, b});
            touching_.erase({a, b});
        }
    }
    std::mutex mu_;
    std::map<std::pair<uint32_t, uint32_t>, int> touching_;
    std::set<std::pair<uint32_t, uint32_t>> began_, ended_;
};

// Rays skip triggers.
class NotSensors final : public JPH::BodyFilter {
public:
    bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
};

// The model of the entity's メッシュ (3D モデル): the mesh collider's shape. Read by the type's name and
// the field's: 物理 does not need ゲーム制作 × 3D. Empty: none.
AssetRef ModelOf(const game_production::SceneEntity& e) {
    const game_production::Component* mr = e.FindComponent("rm::game_x_3d::MeshRenderer");
    if (!mr || !mr->Known()) return {};
    for (const reflect::FieldDesc& f : mr->value.Type()->fields) {
        if (std::string_view(f.name) == "model") return *static_cast<const AssetRef*>(f.Get(mr->value.Get()));
    }
    return {};
}

}  // namespace

// ------------------------------------------------------------------------------------------------
struct PhysicsWorld::Impl {
    BroadPhaseLayers bpLayers;
    ObjectVsBroadPhase objectVsBp;
    ObjectPairs pairs;
    Contacts contacts;
    JPH::PhysicsSystem system;
    const game_production::AssetAccess* assets = nullptr;
    Vec3 gravityNow{0, 0, 0};  // the gravity the system has (set in Step when it differs)

    struct BodyRec {
        JPH::BodyID id;
        uint64_t settings = 0;
        Motion motion = Motion::Static;
        bool flat = false, trigger = false;
        Vec3 wrotePosition{0, 0, 0};  // the pose last written into (or read from) the Transform
        JPH::Quat wroteTurn = JPH::Quat::sIdentity();
        bool seen = false;
    };
    struct CharRec {
        JPH::Ref<JPH::CharacterVirtual> ch;
        uint64_t settings = 0;
        Character values;
        Vec3 walk{0, 0, 0};    // Move: the horizontal speed wanted
        float jump = 0;        // Jump: asked for the next step (0: none)
        Vec3 wrotePosition{0, 0, 0};
        bool seen = false;
    };
    std::unordered_map<int, BodyRec> bodies;   // by entity
    std::unordered_map<int, CharRec> chars;    // by entity
    std::unordered_map<uint32_t, int> owner;   // body (Key) -> entity (characters' inner bodies too)
    std::unordered_set<uint32_t> triggers;     // bodies that are triggers
    struct MeshEntry {
        JPH::RefConst<JPH::Shape> shape;  // null: cannot be read (told once)
    };
    std::unordered_map<std::string, MeshEntry> meshes;  // the models of mesh colliders, by asset id
    std::unordered_set<int> told;                       // entities told about (once each)

    Impl() {
        JoltOnce();
        system.Init(65536, 0, 65536, 20480, bpLayers, objectVsBp, pairs);
        system.SetContactListener(&contacts);
        system.SetGravity(JPH::Vec3::sZero());  // as `gravityNow` (Step sets the game's)
    }
    ~Impl() {
        JPH::BodyInterface& bi = system.GetBodyInterface();
        for (auto& [e, b] : bodies) {
            bi.RemoveBody(b.id);
            bi.DestroyBody(b.id);
        }
        chars.clear();  // (their inner bodies go with them)
    }

    void TellOnce(int entity, const std::string& text) {
        if (told.insert(entity).second) log::Warn("物理: {}", text);
    }

    // --- shapes -------------------------------------------------------------------------------
    JPH::RefConst<JPH::Shape> MeshShapeOf(const SceneEntity& e) {
        const AssetRef model = ModelOf(e);
        if (model.Empty()) return nullptr;
        auto [it, first] = meshes.try_emplace(model.guid);
        if (!first) return it->second.shape;
        std::string err;
        std::optional<space3d::MeshData> mesh;
        if (assets && assets->file) {
            if (const auto file = assets->file(model); !file.empty()) mesh = space3d::LoadMeshFile(file, &err);
        }
        if (!mesh && assets && assets->bytes) {
            if (const auto bytes = assets->bytes(model)) mesh = space3d::ReadBakedMesh(*bytes, &err);
        }
        if (!mesh || mesh->indices.size() < 3) {
            log::Warn("物理: メッシュのコライダーのモデルを読めません ({}): {}", model.guid, err.empty() ? std::string("モデルがありません") : err);
            return nullptr;
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
            log::Warn("物理: メッシュのコライダーを作れません ({}): {}", model.guid, made.GetError().c_str());
            return nullptr;
        }
        it->second.shape = made.Get();
        return it->second.shape;
    }

    JPH::RefConst<JPH::Shape> ShapeOf(const ShapeDims& d, const SceneEntity& e) {
        JPH::RefConst<JPH::Shape> s;
        switch (d.kind) {
            case ShapeDims::Kind::Box: s = new JPH::BoxShape(J(d.half), std::min({d.half.x, d.half.y, d.half.z, JPH::cDefaultConvexRadius}) * 0.5f); break;
            case ShapeDims::Kind::Sphere: s = new JPH::SphereShape(d.radius); break;
            case ShapeDims::Kind::Capsule:
                s = d.halfHeight > 1e-4f ? JPH::RefConst<JPH::Shape>(new JPH::CapsuleShape(d.halfHeight, d.radius))
                                         : JPH::RefConst<JPH::Shape>(new JPH::SphereShape(d.radius));
                break;
            case ShapeDims::Kind::Mesh: {
                JPH::RefConst<JPH::Shape> mesh = MeshShapeOf(e);
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

    // --- bodies --------------------------------------------------------------------------------
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

    // Makes / keeps / moves the body of `e` at `place`. `created`: counts the bodies made (the broad phase
    // is tidied after many). False: it has no body (no collider; a mesh that cannot be read yet).
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
        Motion motion = rb ? rb->motion : Motion::Static;
        if (dims.kind == ShapeDims::Kind::Mesh && motion != Motion::Static) {
            TellOnce(id, std::format("「{}」のメッシュのコライダーは動かない物だけです (動かない物として扱います)", e.data.name));
            motion = Motion::Static;
        }
        Fingerprint fp;
        fp.Add(flat).Add(static_cast<int>(dims.kind)).Add(dims.half).Add(dims.radius).Add(dims.halfHeight).Add(dims.offset).Add(dims.meshScale);
        fp.Add(trigger).Add(static_cast<int>(motion));
        if (rb) fp.Add(rb->mass).Add(rb->friction).Add(rb->bounciness).Add(rb->gravityScale).Add(rb->lockRotationX).Add(rb->lockRotationY).Add(rb->lockRotationZ);
        if (dims.kind == ShapeDims::Kind::Mesh) fp.Add(ModelOf(e).guid);
        JPH::BodyInterface& bi = system.GetBodyInterface();
        const JPH::RVec3 position = J(pose.position);
        const JPH::Quat turn = QuatOf(pose.turn);
        auto found = bodies.find(id);
        if (found != bodies.end() && found->second.settings != fp.h) {
            Remove(id);
            found = bodies.end();
        }
        if (found == bodies.end()) {
            JPH::RefConst<JPH::Shape> shape = ShapeOf(dims, e);
            if (!shape) return false;
            // A trigger that moves by nothing is kinematic (it sees what does not move too).
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
                s.mMotionQuality = JPH::EMotionQuality::LinearCast;  // a fast thing (a bullet) does not go through a wall
            }
            // On the plane: X, Y and the turn about Z only. In 3D: the turns not locked.
            JPH::EAllowedDOFs dofs = flat ? (JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::RotationZ)
                                          : JPH::EAllowedDOFs::All;
            if (rb && rb->lockRotationX) dofs = dofs & ~JPH::EAllowedDOFs::RotationX;
            if (rb && rb->lockRotationY) dofs = dofs & ~JPH::EAllowedDOFs::RotationY;
            if (rb && rb->lockRotationZ) dofs = dofs & ~JPH::EAllowedDOFs::RotationZ;
            if (type == JPH::EMotionType::Dynamic) s.mAllowedDOFs = dofs;
            const JPH::BodyID body = bi.CreateAndAddBody(s, type == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
            if (body.IsInvalid()) {
                TellOnce(id, "物の数が多すぎて体を作れません (65536 まで)");
                return false;
            }
            BodyRec r;
            r.id = body;
            r.settings = fp.h;
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
        // Where the Transform says now: a script (or the inspector) moved it -> the body goes there.
        // (the Transform keeps what was written give or take the float noise of going through its angles)
        const bool moved = std::fabs(pose.position.x - r.wrotePosition.x) + std::fabs(pose.position.y - r.wrotePosition.y) +
                               std::fabs(pose.position.z - r.wrotePosition.z) > 1e-4f ||
                           std::fabs(turn.Dot(r.wroteTurn)) < 1 - 1e-5f;
        const bool kinematic = bi.GetMotionType(r.id) == JPH::EMotionType::Kinematic;
        if (kinematic && dt > 0) {
            bi.MoveKinematic(r.id, position, turn, dt);  // moves through the step (pushes what is in the way)
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
            s.mInnerBodyShape = capsule;  // a body for the others to meet (bullets, triggers)
            s.mInnerBodyLayer = Layer::kMoving;
            s.mMaxSlopeAngle = JPH::DegreesToRadians(std::clamp(c.maxSlope, 0.f, 89.f));
            s.mMass = std::max(c.mass, 0.1f);
            s.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -d.radius);  // the lower sphere stands
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
            r.ch->SetPosition(J(p));  // a script put it somewhere
        }
    }

    // Where every entity is (parents first), and every body made / kept / gone as the components are.
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
            if (!placed || !t.activeInHierarchy) continue;  // no place / switched off: no body (removed below)
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
        if (created >= 64) system.OptimizeBroadPhase();  // many new at once: the broad phase tidied
        placesCache = std::move(places);
    }
    std::unordered_map<int, EntityPlace> placesCache;  // the last SyncAll's (for writing back)

    // One entity alone (a script asks before the step made its body).
    bool SyncOne(Scene& scene, int id) {
        if (bodies.contains(id) || chars.contains(id)) return true;
        SceneEntity* e = scene.Find(id);
        if (!e) return false;
        // Its parents' places, top first.
        std::vector<const SceneEntity*> chain;
        for (const SceneEntity* x = e; x && chain.size() <= scene.Entities().size(); x = scene.Find(x->data.parent)) chain.push_back(x);
        std::reverse(chain.begin(), chain.end());
        EntityPlace p;
        bool placed = false;
        for (const SceneEntity* x : chain) {
            if (!x->data.active) return false;  // switched off (it or a parent)
            const EntityPlace up = p;
            placed = PlaceUnder(*x, x == chain.front() ? nullptr : &up, p);
        }
        if (!placed) return false;
        placesCache[id] = p;
        SyncCharacter(*e, p);
        return Sync(*e, p, 0, nullptr) || chars.contains(id);
    }

    // --- back into the Transforms ----------------------------------------------------------------
    void WriteBack(Scene& scene) {
        const std::string t3 = reflect::TypeOf<space3d::Transform>().name, t2 = reflect::TypeOf<space2d::Transform2D>().name;
        const JPH::BodyInterface& bi = system.GetBodyInterface();
        for (auto& [id, r] : bodies) {
            if (r.motion != Motion::Dynamic || !bi.IsActive(r.id)) continue;  // asleep: where it was
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

    // --- characters walk ----------------------------------------------------------------------------
    void StepCharacters(float dt, Vec3 gravity) {
        JoltOnce();
        for (auto& [id, r] : chars) {
            JPH::CharacterVirtual& ch = *r.ch;
            const JPH::Vec3 g = J(gravity) * r.values.gravityScale;
            ch.UpdateGroundVelocity();
            const bool ground = ch.GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
            const JPH::Vec3 v = ch.GetLinearVelocity();
            // Jolt's way: on the ground and not moving away from it -> the ground's speed; in the air -> its own fall.
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

    // --- what touched -------------------------------------------------------------------------------
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
            if (!trigger && !begin) return;  // (parting of solid things: no hook)
            const int kind = trigger ? (begin ? RM_HOOK_TRIGGER_ENTER : RM_HOOK_TRIGGER_EXIT) : RM_HOOK_HIT;
            events.push_back({kind, std::min(ea->second, eb->second), std::max(ea->second, eb->second)});
        };
        for (const auto& p : ended) add(p, false);
        for (const auto& p : began) add(p, true);
        // The same order whatever Jolt's threads did: exits first, then by entity.
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
        // A new gravity (the game settings changed it, a script): bodies at rest wake up to feel it.
        w.gravityNow = gravity;
        w.system.SetGravity(J(gravity));
        JPH::BodyInterface& bi = w.system.GetBodyInterface();
        for (const auto& [e, b] : w.bodies) {
            if (b.motion == Motion::Dynamic) bi.ActivateBody(b.id);
        }
    }
    w.StepCharacters(dt, gravity);
    Jolt& j = JoltOnce();
    w.system.Update(dt, 1, j.temp, j.jobs);
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
        c->second.walk = velocity;  // a character: its walk (and the fall is its own)
        return true;
    }
    const auto it = w.bodies.find(entity);
    if (it == w.bodies.end() || it->second.motion == Motion::Static) return false;
    w.system.GetBodyInterface().SetLinearVelocity(it->second.id, J(velocity));
    return true;
}

std::optional<PhysicsWorld::RayHit> PhysicsWorld::Raycast(Vec3 origin, Vec3 direction, float maxDistance) const {
    const Impl& w = *impl_;
    const Vec3 d = space3d::Normalize(direction);
    if (space3d::Length(d) == 0 || !(maxDistance > 0)) return std::nullopt;
    const float reach = std::min(maxDistance, 1e6f);
    const JPH::RRayCast ray(J(origin), J(d) * reach);
    JPH::RayCastResult hit;
    const NotSensors notSensors;
    if (!w.system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, notSensors)) return std::nullopt;
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

PhysicsWorld& WorldOf(Scene& scene) { return scene.PlayState<PhysicsWorld>("rm_engine.physics.world"); }

void RunPhysics(game_production::SystemContext& ctx) { WorldOf(ctx.scene).Step(ctx.scene, ctx.dt, ctx.gravity, ctx.hooks, ctx.assets); }

}  // namespace rm::physics
