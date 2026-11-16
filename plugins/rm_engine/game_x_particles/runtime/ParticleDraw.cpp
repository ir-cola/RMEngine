#include "rm_engine/game_x_particles/runtime/ParticleDraw.h"

#include "base/Log.h"
#include "core/reflect/Reflect.h"
#include "rm_engine/game_x_particles/runtime/ParticleSystem.h"
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
            generation_ = f.gfx.Generation();
            playing_ = f.playing;
        }
        ID3D11Device* device = f.gfx.D3D();
        ID3D11DeviceContext* ctx = f.gfx.D3DContext();
        if (!device || !ctx) return;
        const std::string type = reflect::TypeOf<ParticleSystem>().name;
        const double now = NowSeconds();
        for (auto& [id, r] : running_) r.seen = false;
        for (const auto& t : f.scene.TreeOrder()) {
            const auto& e = *t.entity;
            const game_production::Component* c = e.FindComponent(type);
            if (!c || !c->Known()) continue;
            const auto& ps = *static_cast<const ParticleSystem*>(c->value.Get());
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
            Running& r = running_[e.data.id];
            r.seen = true;
            r.player.Set(effect, [&](const particles::ParticleModule& m, std::string& file) { return modules_.Find(m, file, f.asset, f.name, !f.playing); });
            r.player.Advance(device, ctx, &world->m[0][0], f.dt * std::max(ps.speed, 0.f));
            for (const std::string& p : r.player.TakeProblems()) Tell(p);
            out.push_back([player = &r.player](const space3d::ExtraDrawContext& dc) { player->Draw(dc.ctx, ViewOf(dc)); });
            animating_ = true;
        }
        std::erase_if(running_, [](const auto& kv) { return !kv.second.seen; });
    }

    bool Animating() const override { return animating_; }

private:
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

    std::unordered_map<int, Running> running_;  // by entity
    std::unordered_map<std::string, Loaded> effects_;  // by asset id
    ModuleFiles modules_;
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

particles::ViewConstants ViewOf(const space3d::ExtraDrawContext& c) {
    particles::ViewConstants v{};
    const space3d::Mat4 viewProj = c.projection * c.view;
    std::memcpy(v.viewProj, &viewProj.m[0][0], sizeof(v.viewProj));
    // The camera's right and up in the world: the rows of the view's turn (m[column][row]).
    for (int i = 0; i < 3; ++i) {
        v.right[i] = c.view.m[i][0];
        v.up[i] = c.view.m[i][1];
    }
    return v;
}

}  // namespace rm::game_x_particles
