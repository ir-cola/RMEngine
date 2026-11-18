#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"

#include "base/Log.h"
#include "core/asset/BakedImage.h"
#include "platform/Image.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_particles/runtime/CollisionShapes.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"
#include "rm_engine/space3d/runtime/TexturePixels.h"
#include "rm_engine/space3d/runtime/View.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticlePlayer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <set>
#include <string>
#include <unordered_map>

namespace rm::game_x_particles {

namespace {

using particles::EffectPlayer;
using particles::ParticleEffect;


class ParticleDraw final : public game_x_3d::WorldDrawer {
public:
    void Prepare(const game_x_3d::WorldDrawFrame& f, std::vector<space3d::ExtraDraw>& out) override {
        animating_ = false;
        if (f.gfx.Generation() != generation_ || f.playing != playing_) {
            running_.clear();  // (new devices; Play started or stopped: everything from the start)
            view_ = {};
            generation_ = f.gfx.Generation();
            playing_ = f.playing;
        }
        ID3D11Device* device = f.gfx.D3D();
        ID3D11DeviceContext* ctx = f.gfx.D3DContext();
        if (!device || !ctx) return;
        const std::string type = reflect::TypeOf<ParticleSystem>().name;
        const double now = NowSeconds();
        for (auto& [id, r] : running_) r.seen = false;
        // What they collide with: the shapes (gathered at the first particle that runs) and the depth of
        // this picture's last frame (taken while it was drawn: ViewTaker below).
        bool gathered = false;
        particles::CollisionWorld collide;
        if (view_.depth) {
            collide.depth = view_.depth.Get();
            collide.samples = view_.samples;
            std::copy(std::begin(view_.viewProj), std::end(view_.viewProj), collide.viewProj);
            std::copy(std::begin(view_.inverse), std::end(view_.inverse), collide.inverse);
            std::copy(std::begin(view_.eye), std::end(view_.eye), collide.eye);
            collide.width = view_.width;
            collide.height = view_.height;
        }
        viewTaken_ = false;
        out.push_back([this](const space3d::ExtraDrawContext& dc) { TakeView(dc); });
        // Each パーティクル of each entity (several on one: several effects, each with its own player).
        struct Found {
            const game_production::SceneEntity* entity;
            size_t nth;
            const ParticleSystem* system;
        };
        std::vector<Found> found;
        for (const auto& t : f.scene.TreeOrder()) {
            for (size_t nth = 0;; ++nth) {
                const game_production::Component* c = t.entity->FindComponent(type, nth);
                if (!c) break;
                if (c->Known()) found.push_back({t.entity, nth, static_cast<const ParticleSystem*>(c->value.Get())});
            }
        }
        for (const Found& one : found) {
            const auto& e = *one.entity;
            const size_t nth = one.nth;
            const auto& ps = *one.system;
            if (f.playing ? !ps.playOnStart : std::find(f.selection.begin(), f.selection.end(), e.data.id) == f.selection.end()) continue;
            const space3d::Mat4* world = f.world ? f.world(e.data.id) : nullptr;
            if (!world) continue;  // no Transform, or switched off
            if (!particles::CanRunParticles(device)) {
                if (!toldNoGpu_) log::Warn("この GPU ではパーティクルを出せません (Direct3D 11 の機能レベル 11_0 が要ります)");
                toldNoGpu_ = true;
                return;
            }
            std::shared_ptr<const ParticleEffect> effect = EffectOf(ps.effect, f, now);
            if (!effect) continue;
            Running& r = running_[RunKey(e.data.id, nth)];
            r.seen = true;
            r.player.Set(effect, [&](const particles::ParticleModule& m, std::string& file) { return modules_.Find(m, file, f.asset, f.name, !f.playing); });
            if (!gathered) {
                gathered = true;
                shapes_.clear();
                if (f.registry) {
                    for (const auto& s : f.registry->Contributions<CollisionShapesInfo>(kCollisionShapesPoint)) {
                        if (s.item->gather) s.item->gather(f.scene, f.world, shapes_);
                    }
                }
                collide.shapes = shapes_;
            }
            r.player.Advance(device, ctx, &world->m[0][0], f.dt * std::max(ps.speed, 0.f), &collide);
            for (const std::string& p : r.player.TakeProblems()) Tell(p);
            out.push_back([this, player = &r.player, gfx = &f.gfx, read = f.asset, often = !f.playing](const space3d::ExtraDrawContext& dc) {
                player->Draw(dc.ctx, ViewOf(dc), [&](const AssetRef& ref) { return images_.Find(ref, *gfx, read, often); });
            });
            animating_ = true;
        }
        std::erase_if(running_, [](const auto& kv) { return !kv.second.seen; });
    }

    bool Animating() const override { return animating_; }

private:
    // The camera and the depth of this picture's frame (read by the next frame's collisions).
    struct View {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth;
        uint32_t samples = 1;
        float viewProj[16]{};
        float inverse[16]{};
        float eye[3]{};
        uint32_t width = 0, height = 0;
    };
    void TakeView(const space3d::ExtraDrawContext& dc) {
        if (viewTaken_) return;
        viewTaken_ = true;
        const space3d::Mat4 viewProj = dc.projection * dc.view;
        const std::optional<space3d::Mat4> inverse = space3d::Inverse(viewProj);
        if (!dc.depth || !inverse) {
            view_ = {};
            return;
        }
        view_.depth = dc.depth;
        view_.samples = dc.samples;
        std::memcpy(view_.viewProj, &viewProj.m[0][0], sizeof(view_.viewProj));
        std::memcpy(view_.inverse, &inverse->m[0][0], sizeof(view_.inverse));
        view_.eye[0] = dc.eye.x;
        view_.eye[1] = dc.eye.y;
        view_.eye[2] = dc.eye.z;
        view_.width = dc.width;
        view_.height = dc.height;
    }

    struct Running {
        EffectPlayer player;
        bool seen = false;
    };
    struct Loaded {
        std::shared_ptr<const ParticleEffect> effect;
        std::string bytes;
        double lookedAt = -1e9;
    };

    // The effect an asset holds (read again now and then); none: the sample (NewEffect).
    std::shared_ptr<const ParticleEffect> EffectOf(const AssetRef& ref, const game_x_3d::WorldDrawFrame& f, double now) {
        if (ref.Empty()) {
            static const auto sample = std::make_shared<const ParticleEffect>(particles::NewEffect());
            return sample;
        }
        Loaded& l = effects_[ref.guid];
        // (Not playing - the editor: looked at often, so an edit of the パーティクル tab shows at once.)
        if (now - l.lookedAt < (f.playing ? kLookSeldom : kLookOften)) return l.effect;
        l.lookedAt = now;
        const auto bytes = f.asset ? f.asset(ref) : std::nullopt;
        if (!bytes) {
            if (!l.effect) Tell(std::format("パーティクルのエフェクト ({}) が見つかりません", ref.guid));
            return l.effect;
        }
        if (l.effect && *bytes == l.bytes) return l.effect;
        std::string err;
        auto effect = particles::ReadEffect(*bytes, &err);
        if (!effect) {
            Tell(std::format("パーティクルのエフェクト ({}) を読めません: {}", ref.guid, err));
            return l.effect;
        }
        l.bytes = *bytes;
        l.effect = std::make_shared<const ParticleEffect>(std::move(*effect));
        return l.effect;
    }

    void Tell(const std::string& text) {
        if (told_.insert(text).second) log::Warn("{}", text);
    }

    static uint64_t RunKey(int entity, size_t nth) { return (static_cast<uint64_t>(static_cast<uint32_t>(entity)) << 32) | static_cast<uint32_t>(nth); }
    std::unordered_map<uint64_t, Running> running_;  // by entity and which of its パーティクル (RunKey)
    std::unordered_map<std::string, Loaded> effects_;  // by asset id
    ModuleFiles modules_;
    ParticleImages images_;
    std::vector<particles::CollisionShape> shapes_;
    View view_;
    bool viewTaken_ = false;
    std::set<std::string> told_;
    uint64_t generation_ = 0;
    bool playing_ = false;
    bool animating_ = false;
    bool toldNoGpu_ = false;
};

}  // namespace

std::unique_ptr<game_x_3d::WorldDrawer> CreateParticleDraw() { return std::make_unique<ParticleDraw>(); }

double NowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::shared_ptr<const particles::ModuleSource> ModuleFiles::Find(const particles::ParticleModule& m, std::string& file, const Read& read, const Name& name,
                                                                 bool often) {
    if (!m.builtin.empty()) return particles::BuiltinLookup(m, file);
    if (m.file.Empty()) return nullptr;
    Entry& e = files_[m.file.guid];
    const double now = NowSeconds();
    if (now - e.lookedAt >= (often ? kLookOften : kLookSeldom)) {
        e.lookedAt = now;
        if (e.name.empty()) e.name = name ? name(m.file) : std::string();
        if (e.name.empty()) e.name = m.file.guid;
        if (const auto bytes = read ? read(m.file) : std::nullopt; bytes && (!e.source || *bytes != e.bytes)) {
            e.bytes = *bytes;
            e.source = std::make_shared<const particles::ModuleSource>(particles::ParseModule(*bytes));
        }
    }
    file = e.name;
    return e.source;
}

ID3D11ShaderResourceView* ParticleImages::Find(const AssetRef& ref, platform::Gfx& gfx, const ModuleFiles::Read& read, bool often) {
    if (ref.Empty()) return nullptr;
    Entry& e = images_[ref.guid];
    if (e.generation != gfx.Generation()) {
        e = {};  // (made on devices that are gone)
        e.generation = gfx.Generation();
    }
    const double now = NowSeconds();
    if (now - e.lookedAt < (often ? kLookOften : kLookSeldom)) return e.view.Get();
    e.lookedAt = now;
    const auto bytes = read ? read(ref) : std::nullopt;
    if (!bytes || (e.view && *bytes == e.bytes)) return e.view.Get();
    e.bytes = *bytes;
    e.view.Reset();
    auto tell = [&](const std::string& why) {
        if (told_.insert(ref.guid + why).second) log::Warn("パーティクルの絵 ({}) を読めません: {}", ref.guid, why);
    };
    std::string err;
    const auto baked = asset::ReadBakedImage(*bytes, &err);  // (the game's: baked; the editor's: the file)
    if (!baked) return tell(err), nullptr;
    const auto* data = reinterpret_cast<const uint8_t*>(baked->picture.data());
    const auto image = platform::DecodeImage(gfx.Wic(), std::span(data, baked->picture.size()), platform::PixelOrder::Rgba, {1024, 1024}, &err);
    if (!image) return tell(err), nullptr;
    // With its mipmaps (the smaller copies 3D 空間's pictures have, averaged as light): a big picture on
    // small or far particles does not shimmer (2026-11-18: it had only the one level). The texels stay
    // as the picture has them (UNORM: the particle shader takes them as they are).
    const space3d::TexturePixels pixels = space3d::MakeTexturePixels(*image, true);
    if (pixels.levels.empty() || pixels.format != space3d::TextureFormat::Rgba8) return tell("ミップマップを作れません"), nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = image->width;
    td.Height = image->height;
    td.MipLevels = static_cast<UINT>(pixels.levels.size());
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init;
    for (const platform::Image& level : pixels.levels) init.push_back({level.pixels.data(), level.width * 4, 0});
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (!gfx.D3D() || FAILED(gfx.D3D()->CreateTexture2D(&td, init.data(), &texture)) ||
        FAILED(gfx.D3D()->CreateShaderResourceView(texture.Get(), nullptr, &e.view))) {
        tell("GPU に上げられません");
        return nullptr;
    }
    return e.view.Get();
}

particles::ViewConstants ViewOf(const space3d::ExtraDrawContext& c) {
    particles::ViewConstants v{};
    const space3d::Mat4 viewProj = c.projection * c.view;
    std::memcpy(v.viewProj, &viewProj.m[0][0], sizeof(v.viewProj));
    // The camera's right and up in the world: the rows of the view's turn (m[column][row]).
    for (int i = 0; i < 3; ++i) {
        v.right[i] = c.view.m[i][0];
        v.up[i] = c.view.m[i][1];
    }
    v.eye[0] = c.eye.x;
    v.eye[1] = c.eye.y;
    v.eye[2] = c.eye.z;
    return v;
}

}  // namespace rm::game_x_particles
