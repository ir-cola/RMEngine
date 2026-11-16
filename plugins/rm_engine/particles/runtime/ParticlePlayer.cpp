#include "rm_engine/particles/runtime/ParticlePlayer.h"

#include "rm_engine/particles/runtime/BuiltinModules.h"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <mutex>
#include <utility>

#pragma comment(lib, "d3dcompiler.lib")

namespace rm::particles {

using Microsoft::WRL::ComPtr;

uint32_t AdvanceClock(EmitterClock& c, const ParticleEmitter& e, float dt) {
    if (c.finished) return 0;
    const float duration = std::max(e.duration, 0.01f);
    const float rate = std::max(e.rate, 0.f);
    double out = 0;
    if (!c.burstDone) {
        out += std::max(e.burst, 0);
        c.burstDone = true;
    }
    float left = std::max(dt, 0.f);
    // (A long dt runs through several rounds; each round starts with its burst.)
    for (int rounds = 0; left > 0 && rounds < 1000; ++rounds) {
        const float span = std::min(left, duration - c.age);
        c.owed += rate * span;
        c.age += span;
        left -= span;
        if (c.age < duration) break;
        if (!e.loop) {
            c.finished = true;
            break;
        }
        c.age = 0;
        out += std::max(e.burst, 0);
    }
    const float whole = std::floor(c.owed);
    c.owed -= whole;
    out += whole;
    const double cap = static_cast<double>(std::max(e.capacity, 1));
    return static_cast<uint32_t>(std::min(out, cap));
}

std::shared_ptr<const ModuleSource> BuiltinLookup(const ParticleModule& m, std::string& file) {
    if (m.builtin.empty()) return nullptr;
    file = m.builtin;
    const ModuleSource* src = FindBuiltin(m.builtin);
    // (the built-in ones live for the whole program: a pointer that owns nothing)
    return src ? std::shared_ptr<const ModuleSource>(std::shared_ptr<const ModuleSource>(), src) : nullptr;
}

bool CanRunParticles(ID3D11Device* device) { return device && device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0; }

namespace {

// The shaders of one text on one device (shared by every player that has the same modules).
struct Programs {
    ComPtr<ID3D11ComputeShader> spawn, update;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    std::string error;  // the compiler's words (then nothing above is made)
};

// The states every emitter draws with, once per device.
struct States {
    ComPtr<ID3D11BlendState> add, alpha;
    ComPtr<ID3D11RasterizerState> raster;
};

bool Compile(const std::string& hlsl, const char* entry, const char* target, ComPtr<ID3DBlob>& code, std::string& error) {
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(hlsl.data(), hlsl.size(), "particles", nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (SUCCEEDED(hr)) return true;
    error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                   : std::format("シェーダーを作れません (0x{:08X})", static_cast<uint32_t>(hr));
    while (!error.empty() && (error.back() == '\n' || error.back() == '\r' || error.back() == '\0')) error.pop_back();
    return false;
}

std::shared_ptr<const Programs> ProgramsFor(ID3D11Device* device, const std::string& hlsl) {
    static std::mutex mutex;
    static std::map<std::pair<ID3D11Device*, std::string>, std::weak_ptr<const Programs>> made;
    std::lock_guard lock(mutex);
    auto& slot = made[{device, hlsl}];
    if (auto p = slot.lock()) return p;
    auto p = std::make_shared<Programs>();
    ComPtr<ID3DBlob> spawn, update, vs, ps;
    if (Compile(hlsl, "SpawnMain", "cs_5_0", spawn, p->error) && Compile(hlsl, "UpdateMain", "cs_5_0", update, p->error) &&
        Compile(hlsl, "ParticleVS", "vs_5_0", vs, p->error) && Compile(hlsl, "ParticlePS", "ps_5_0", ps, p->error)) {
        if (FAILED(device->CreateComputeShader(spawn->GetBufferPointer(), spawn->GetBufferSize(), nullptr, &p->spawn)) ||
            FAILED(device->CreateComputeShader(update->GetBufferPointer(), update->GetBufferSize(), nullptr, &p->update)) ||
            FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &p->vs)) ||
            FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &p->ps))) {
            p->error = "シェーダーを GPU に作れません";
        }
    }
    // (Texts nobody plays any more are forgotten now and then.)
    if (made.size() > 64) std::erase_if(made, [](const auto& kv) { return kv.second.expired(); });
    slot = p;
    return p;
}

std::shared_ptr<const States> StatesFor(ID3D11Device* device) {
    static std::mutex mutex;
    static std::map<ID3D11Device*, std::weak_ptr<const States>> made;
    std::lock_guard lock(mutex);
    auto& slot = made[device];
    if (auto s = slot.lock()) return s;
    auto s = std::make_shared<States>();
    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_ONE;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
    rt.DestBlendAlpha = D3D11_BLEND_ONE;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&bd, &s->add);
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    device->CreateBlendState(&bd, &s->alpha);
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rd, &s->raster);
    slot = s;
    return s;
}

ComPtr<ID3D11Buffer> ConstantBuffer(ID3D11Device* device, UINT bytes) {
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = (bytes + 15u) & ~15u;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> b;
    device->CreateBuffer(&d, nullptr, &b);
    return b;
}

constexpr float kForgetDeadAfter = 30;  // a finished emitter's particles are surely gone after this (seconds)

}  // namespace

struct EffectPlayer::Emitter {
    const ParticleEmitter* data = nullptr;  // inside effect_
    std::vector<std::shared_ptr<const ModuleSource>> looked;  // every module in use, as looked up (null: none)
    std::vector<std::shared_ptr<const ModuleSource>> sources;  // the ones that run
    std::vector<std::string> files;
    std::string error;  // the compiler's words when its shaders did not compile
    std::vector<ModuleInput> inputs;
    EmitterShader shader;
    std::vector<float> params;
    std::shared_ptr<const Programs> programs;
    std::shared_ptr<const States> states;
    bool broken = false;  // its modules or its shaders cannot run (told once)

    ComPtr<ID3D11Buffer> particles;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11ShaderResourceView> srv;
    uint32_t capacity = 0;  // of the buffer made
    bool cleared = false;
    ComPtr<ID3D11Buffer> emitterCb, paramsCb, viewCb;
    uint32_t paramsCbSlots = 0;

    EmitterClock clock;
    uint32_t head = 0, serial = 0;
    float sinceFinished = 0;
};

EffectPlayer::EffectPlayer() = default;
EffectPlayer::~EffectPlayer() = default;

void EffectPlayer::Set(std::shared_ptr<const ParticleEffect> effect, const ModuleLookup& lookup) {
    // The modules each emitter uses now.
    struct Found {
        const ParticleModule* module;
        std::shared_ptr<const ModuleSource> source;
        std::string file;
    };
    const size_t count = effect ? effect->emitters.size() : 0;
    std::vector<std::vector<Found>> found(count);
    for (size_t i = 0; i < count; ++i) {
        for (const ParticleModule& m : effect->emitters[i].modules) {
            if (!m.enabled) continue;
            Found f{&m, nullptr, {}};
            if (lookup) f.source = lookup(m, f.file);
            found[i].push_back(std::move(f));
        }
    }
    if (effect == effect_ && emitters_.size() == count) {
        bool same = true;
        for (size_t i = 0; same && i < count; ++i) {
            same = emitters_[i]->looked.size() == found[i].size();
            for (size_t j = 0; same && j < found[i].size(); ++j) same = emitters_[i]->looked[j] == found[i][j].source;
        }
        if (same) return;
    }
    effect_ = std::move(effect);
    std::vector<std::unique_ptr<Emitter>> next;
    for (size_t i = 0; i < count; ++i) {
        const ParticleEmitter& data = effect_->emitters[i];
        auto e = std::make_unique<Emitter>();
        e->data = &data;
        for (Found& f : found[i]) {
            const ParticleModule& m = *f.module;
            const std::string& file = f.file;
            e->looked.push_back(f.source);
            const std::shared_ptr<const ModuleSource>& src = f.source;
            if (!src) {
                problems_.push_back(std::format("パーティクル「{}」: モジュール「{}」が見つからないので飛ばします", data.name, m.builtin.empty() ? m.file.guid : m.builtin));
                continue;
            }
            if (!src->errors.empty()) {
                for (const ModuleError& err : src->errors) problems_.push_back(std::format("パーティクル「{}」: モジュール {}: {}", data.name, file, Message(err)));
                continue;
            }
            e->sources.push_back(src);
            e->files.push_back(file);
            e->inputs.push_back({src.get(), file, m.values});
        }
        e->shader = AssembleShader(e->inputs);
        e->params = PackParams(e->inputs);
        for (const ModuleError& err : e->shader.errors) problems_.push_back(std::format("パーティクル「{}」: {}", data.name, Message(err)));
        e->broken = !e->shader.errors.empty();
        // The same modules as before: the particles go on (only the values changed).
        if (i < emitters_.size() && emitters_[i] && emitters_[i]->shader.hlsl == e->shader.hlsl && !e->broken) {
            Emitter& old = *emitters_[i];
            old.data = e->data;
            old.looked = std::move(e->looked);
            old.sources = std::move(e->sources);
            old.files = std::move(e->files);
            old.inputs = std::move(e->inputs);
            old.params = std::move(e->params);
            next.push_back(std::move(emitters_[i]));
            continue;
        }
        next.push_back(std::move(e));
    }
    emitters_ = std::move(next);
}

void EffectPlayer::Restart() {
    time_ = 0;
    for (auto& e : emitters_) {
        e->clock = {};
        e->head = e->serial = 0;
        e->cleared = false;
        e->sinceFinished = 0;
    }
}

void EffectPlayer::Advance(ID3D11Device* device, ID3D11DeviceContext* ctx, const float world[16], float dt) {
    if (!device || !ctx) return;
    time_ += dt;
    for (auto& ptr : emitters_) {
        Emitter& e = *ptr;
        if (!e.data->enabled || e.broken) continue;
        if (!e.programs) {
            e.programs = ProgramsFor(device, e.shader.hlsl);
            if (!e.programs->error.empty()) {
                problems_.push_back(std::format("パーティクル「{}」のシェーダーを作れません:\n{}", e.data->name, e.programs->error));
                e.error = e.programs->error;
                e.broken = true;
                continue;
            }
            e.states = StatesFor(device);
        }
        const uint32_t capacity = static_cast<uint32_t>(std::clamp(e.data->capacity, 1, 1000000));
        if (!e.particles || e.capacity != capacity) {
            e.particles.Reset();
            e.uav.Reset();
            e.srv.Reset();
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = capacity * e.shader.stride;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = e.shader.stride;
            if (FAILED(device->CreateBuffer(&bd, nullptr, &e.particles)) || FAILED(device->CreateUnorderedAccessView(e.particles.Get(), nullptr, &e.uav)) ||
                FAILED(device->CreateShaderResourceView(e.particles.Get(), nullptr, &e.srv))) {
                problems_.push_back(std::format("パーティクル「{}」: GPU に粒 {} 個ぶんの場所を作れません", e.data->name, capacity));
                e.broken = true;
                continue;
            }
            e.capacity = capacity;
            e.cleared = false;
            e.head = 0;
        }
        if (!e.emitterCb) e.emitterCb = ConstantBuffer(device, sizeof(EmitterConstants));
        if (!e.viewCb) e.viewCb = ConstantBuffer(device, sizeof(ViewConstants));
        if (!e.paramsCb || e.paramsCbSlots != e.shader.paramSlots) {
            e.paramsCb = ConstantBuffer(device, e.shader.paramSlots * 16u);
            e.paramsCbSlots = e.shader.paramSlots;
        }
        if (!e.emitterCb || !e.viewCb || !e.paramsCb) continue;
        if (!e.cleared) {
            const UINT zero[4] = {};
            ctx->ClearUnorderedAccessViewUint(e.uav.Get(), zero);
            e.cleared = true;
        }
        const uint32_t born = AdvanceClock(e.clock, *e.data, dt);
        if (e.clock.finished) e.sinceFinished += dt;

        EmitterConstants ec{};
        std::copy(world, world + 16, ec.world);
        ec.position[0] = world[12];
        ec.position[1] = world[13];
        ec.position[2] = world[14];
        ec.time = time_;
        ec.dt = dt;
        ec.spawnStart = e.head;
        ec.spawnCount = born;
        ec.capacity = e.capacity;
        ec.spawnSerial = e.serial;
        ec.age = e.clock.age;
        ec.shape = static_cast<uint32_t>(e.data->shape);
        ctx->UpdateSubresource(e.emitterCb.Get(), 0, nullptr, &ec, 0, 0);
        std::vector<float> params(static_cast<size_t>(e.shader.paramSlots) * 4, 0.f);
        std::copy_n(e.params.begin(), std::min(e.params.size(), params.size()), params.begin());
        ctx->UpdateSubresource(e.paramsCb.Get(), 0, nullptr, params.data(), 0, 0);

        ID3D11Buffer* cbs[2] = {e.emitterCb.Get(), e.paramsCb.Get()};
        ctx->CSSetConstantBuffers(0, 2, cbs);
        ID3D11UnorderedAccessView* uav = e.uav.Get();
        ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        if (born > 0) {
            ctx->CSSetShader(e.programs->spawn.Get(), nullptr, 0);
            ctx->Dispatch((born + kThreads - 1) / kThreads, 1, 1);
        }
        ctx->CSSetShader(e.programs->update.Get(), nullptr, 0);
        ctx->Dispatch((e.capacity + kThreads - 1) / kThreads, 1, 1);
        ID3D11UnorderedAccessView* noUav = nullptr;
        ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
        ctx->CSSetShader(nullptr, nullptr, 0);
        e.head = (e.head + born) % e.capacity;
        e.serial += born;
    }
}

void EffectPlayer::Draw(ID3D11DeviceContext* ctx, const ViewConstants& view) {
    if (!ctx) return;
    for (auto& ptr : emitters_) {
        Emitter& e = *ptr;
        if (!e.data->enabled || e.broken || !e.programs || !e.srv || !e.cleared || !e.states) continue;
        ctx->UpdateSubresource(e.viewCb.Get(), 0, nullptr, &view, 0, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(e.programs->vs.Get(), nullptr, 0);
        ctx->PSSetShader(e.programs->ps.Get(), nullptr, 0);
        ID3D11ShaderResourceView* srv = e.srv.Get();
        ctx->VSSetShaderResources(0, 1, &srv);
        ID3D11Buffer* viewCb = e.viewCb.Get();
        ID3D11Buffer* emitterCb = e.emitterCb.Get();
        ctx->VSSetConstantBuffers(2, 1, &viewCb);
        ctx->PSSetConstantBuffers(0, 1, &emitterCb);
        const float factor[4] = {};
        ctx->OMSetBlendState(e.data->blend == ParticleBlend::Add ? e.states->add.Get() : e.states->alpha.Get(), factor, 0xFFFFFFFF);
        ctx->RSSetState(e.states->raster.Get());
        ctx->DrawInstanced(6, e.capacity, 0, 0);
        ID3D11ShaderResourceView* noSrv = nullptr;
        ID3D11Buffer* noCb = nullptr;
        ctx->VSSetShaderResources(0, 1, &noSrv);
        ctx->VSSetConstantBuffers(2, 1, &noCb);
    }
}

std::string EffectPlayer::Error(size_t emitter) const { return emitter < emitters_.size() ? emitters_[emitter]->error : std::string(); }

bool EffectPlayer::Finished() const {
    for (const auto& e : emitters_) {
        if (e->data->enabled && !e->broken && !(e->clock.finished && e->sinceFinished > kForgetDeadAfter)) return false;
    }
    return true;
}

std::vector<std::string> EffectPlayer::TakeProblems() { return std::exchange(problems_, {}); }

}  // namespace rm::particles
