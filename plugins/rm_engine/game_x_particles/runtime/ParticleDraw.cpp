#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"

#include "base/Log.h"
#include "core/asset/BakedImage.h"
#include "platform/Image.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_particles/runtime/CollisionShapes.h"
#include "rm_engine/game_x_particles/runtime/ParticleStats.h"
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
            running_.clear();  // (新しいデバイス、Play の開始か停止: すべて始めから)
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
        // ぶつかる相手: 形 (最初に動くパーティクルで集める) と、この絵の最後のフレームの
        // 深度 (描いている間に取る: 下の ViewTaker)。
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
        // 各エンティティの各 パーティクル (1 つに複数あれば複数のエフェクトで、それぞれ専用のプレイヤー)。
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
            // Play 中: パーティクルを出しているものと、スクリプトが止めて残りが消えていくもの。
            const auto there = running_.find(RunKey(e.data.id, nth));
            if (f.playing ? !Emitting(ps) && (there == running_.end() || there->second.player.Finished())
                          : std::find(f.selection.begin(), f.selection.end(), e.data.id) == f.selection.end()) {
                continue;
            }
            const space3d::Mat4* world = f.world ? f.world(e.data.id) : nullptr;
            if (!world) continue;  // Transform がない、またはオフ
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
            if (f.playing) {
                // 前のフレームからスクリプトが頼んだこと (それぞれ 1 回)。
                r.player.SetEmitting(Emitting(ps));
                if (ps.restarts != r.restarts) r.player.Restart();
                if (ps.emitted != r.emitted) r.player.Emit(ps.emitted - r.emitted);
                r.restarts = ps.restarts;
                r.emitted = ps.emitted;
            }
            const float speed = f.playing && ps.paused ? 0.f : std::max(ps.speed, 0.f);
            r.player.Advance(device, ctx, &world->m[0][0], f.dt * speed, &collide);
            for (const std::string& p : r.player.TakeProblems()) Tell(p);
            out.push_back([this, player = &r.player, gfx = &f.gfx, read = f.asset, often = !f.playing](const space3d::ExtraDrawContext& dc) {
                player->Draw(dc.ctx, ViewOf(dc), [&](const AssetRef& ref) { return images_.Find(ref, *gfx, read, often); });
            });
            animating_ = true;
        }
        std::erase_if(running_, [](const auto& kv) { return !kv.second.seen; });
        // この絵が再生しているもの。デバッグパネル用。
        PictureParticles stats;
        stats.picture = f.picture ? f.picture : "";
        stats.playing = f.playing;
        stats.systems = found.size();
        stats.effects = running_.size();
        stats.aliveKnown = true;
        for (const auto& [key, r] : running_) {
            const EffectPlayer::Census c = r.player.Count();
            stats.emitters += c.emitters;
            stats.capacity += c.capacity;
            stats.alive += c.alive;
            stats.aliveKnown = stats.aliveKnown && c.aliveKnown;
        }
        ReportParticles(this, std::move(stats));
    }

    bool Animating() const override { return animating_; }

private:
    // この絵のフレームのカメラと深度 (次のフレームの衝突が読む)。
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
        uint32_t restarts = 0, emitted = 0;  // スクリプトの頼みをやり終えた数 (ParticleSystem の数)
    };
    struct Loaded {
        std::shared_ptr<const ParticleEffect> effect;
        std::string bytes;
        double lookedAt = -1e9;
    };

    // アセットが持つエフェクト (ときどき読み直す)。なければ見本 (NewEffect)。
    std::shared_ptr<const ParticleEffect> EffectOf(const AssetRef& ref, const game_x_3d::WorldDrawFrame& f, double now) {
        if (ref.Empty()) {
            static const auto sample = std::make_shared<const ParticleEffect>(particles::NewEffect());
            return sample;
        }
        Loaded& l = effects_[ref.guid];
        // (再生中でない - エディター: よく見直すので、パーティクル タブでの編集がすぐ出る。)
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
    std::unordered_map<uint64_t, Running> running_;  // エンティティと、その何番目の パーティクル か (RunKey) ごと
    std::unordered_map<std::string, Loaded> effects_;  // アセット id ごと
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
        e = {};  // (なくなったデバイスで作ったもの)
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
    const auto baked = asset::ReadBakedImage(*bytes, &err);  // (ゲームのもの: ベイク済み。エディターのもの: ファイル)
    if (!baked) return tell(err), nullptr;
    const auto* data = reinterpret_cast<const uint8_t*>(baked->picture.data());
    const auto image = platform::DecodeImage(gfx.Wic(), std::span(data, baked->picture.size()), platform::PixelOrder::Rgba, {1024, 1024}, &err);
    if (!image) return tell(err), nullptr;
    // ミップマップつき (3D 空間の絵が持つ、小さくしたコピー。光として平均する): 大きな絵を小さな
    // パーティクルや遠いパーティクルに貼ってもちらつかない (2026-11-18: 以前は 1 段しかなかった)。
    // テクセルは絵のまま (UNORM: パーティクルのシェーダーはそのまま受け取る)。
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
    // ワールドでのカメラの右と上: ビューの回転の行 (m[列][行])。
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
