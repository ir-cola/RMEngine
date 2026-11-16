// Tests for the particle plugin (plugins/rm_engine/particles): reading a module (.rmfx), putting an
// emitter's shaders together from its modules, and that what is put together compiles (D3DCompile:
// no GPU needed).

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/particles/runtime/BuiltinModules.h"
#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticlePlayer.h"
#include "rm_engine/particles/runtime/ParticleShader.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

using namespace rm;
using namespace rm::particles;

namespace {

// Compiles one entry of the text; the compiler's words on failure.
std::string Compile(const std::string& hlsl, const char* entry, const char* target) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(hlsl.data(), hlsl.size(), "particles", nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (SUCCEEDED(hr)) return {};
    return errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string("failed");
}

std::vector<ModuleInput> Inputs(const ParticleEmitter& e) {
    std::vector<ModuleInput> in;
    for (const ParticleModule& m : e.modules) in.push_back({FindBuiltin(m.builtin), m.builtin, m.values});
    return in;
}

}  // namespace

RM_TEST(Particles_ModuleSource_ReadsParamsAttrsAndFunctions) {
    const ModuleSource m = ParseModule(
        "module \"風で曲げる\"\r\n"
        "param float strength = 1.5 [0, 10]; // 強さ\r\n"
        "param float3 direction = float3(1, 0, -2); // 向き\n"
        "param color tint; // 色\n"
        "param float2 range = 3;\n"
        "param bool on = true;\n"
        "attr float heat;\n"
        "void Update(inout Particle p, float dt, inout Random rnd) {\n"
        "    p.velocity += direction * strength * dt;\n"
        "}\n");
    CHECK(m.errors.empty());
    CHECK(m.title == "風で曲げる");
    CHECK(m.params.size() == 5 && m.attrs.size() == 1);
    const ModuleParam* s = FindParam(m, "strength");
    CHECK(s && s->type == ParamType::Float && s->value[0] == 1.5f && s->hasRange && s->min == 0 && s->max == 10 && s->label == "強さ" && s->line == 2);
    const ModuleParam* d = FindParam(m, "direction");
    CHECK(d && d->type == ParamType::Float3 && d->value[0] == 1 && d->value[1] == 0 && d->value[2] == -2);
    const ModuleParam* t = FindParam(m, "tint");
    CHECK(t && t->type == ParamType::Color && t->value[3] == 1);  // a colour without a value: white
    const ModuleParam* r = FindParam(m, "range");
    CHECK(r && r->value[0] == 3 && r->value[1] == 3);             // one value for all
    CHECK(FindParam(m, "on") && FindParam(m, "on")->value[0] == 1);
    CHECK(m.attrs[0].name == "heat" && m.attrs[0].type == "float" && m.attrs[0].line == 7);
    CHECK(!m.hasSpawn && m.hasUpdate);
    // The engine's lines are empty in the code: the compiler's line numbers are the file's.
    size_t lines = 0;
    for (const char c : m.code) lines += c == '\n';
    CHECK(lines == 10 && m.code.find("param") == std::string::npos && m.code.find("p.velocity") != std::string::npos);
}

RM_TEST(Particles_ModuleSource_SaysWhatIsWrong) {
    const ModuleSource m = ParseModule(
        "param vec3 a = 1;\n"
        "param float 2b = 1;\n"
        "param float c = x;\n"
        "param float d = 1\n"
        "param float e = 1; param float e = 2;\n"
        "attr float e;\n");
    std::vector<ModuleErrorKind> kinds;
    for (const ModuleError& e : m.errors) kinds.push_back(e.kind);
    CHECK(kinds.size() == 6);
    CHECK(kinds.size() == 6 && kinds[0] == ModuleErrorKind::UnknownType && m.errors[0].line == 1);
    CHECK(kinds.size() == 6 && kinds[1] == ModuleErrorKind::BadName && kinds[2] == ModuleErrorKind::BadValue && kinds[3] == ModuleErrorKind::BadLine);
    CHECK(kinds.size() == 6 && kinds[4] == ModuleErrorKind::BadValue);  // ("1; param float e = 2" is not a value)
    CHECK(kinds.size() == 6 && kinds[5] == ModuleErrorKind::NoFunction && m.errors[5].line == 0);
    CHECK(!Message(m.errors[0]).empty());
}

RM_TEST(Particles_Builtins_ReadWithoutErrors) {
    CHECK(BuiltinModules().size() >= 8);
    for (const BuiltinModule& b : BuiltinModules()) {
        const ModuleSource* m = FindBuiltin(b.id);
        CHECK(m && m->errors.empty() && !m->title.empty());
        if (!m) continue;
        for (const ModuleParam& p : m->params) CHECK(!p.label.empty());  // every field has its words
    }
    CHECK(FindBuiltin("no_such_module") == nullptr);
}

RM_TEST(Particles_Shader_PutTogetherAndCompiles) {
    // Every built-in module at once, then the new effect's emitter: all three kinds of shader compile.
    ParticleEmitter all;
    for (const BuiltinModule& b : BuiltinModules()) {
        ParticleModule m;
        m.builtin = std::string(b.id);
        all.modules.push_back(m);
    }
    for (const ParticleEmitter& e : {all, NewEffect().emitters[0]}) {
        const std::vector<ModuleInput> in = Inputs(e);
        const EmitterShader s = AssembleShader(in);
        CHECK(s.errors.empty() && s.stride == 88);
        const std::string spawn = Compile(s.hlsl, "SpawnMain", "cs_5_0");
        const std::string update = Compile(s.hlsl, "UpdateMain", "cs_5_0");
        const std::string vs = Compile(s.hlsl, "ParticleVS", "vs_5_0");
        const std::string ps = Compile(s.hlsl, "ParticlePS", "ps_5_0");
        CHECK(spawn.empty() && update.empty() && vs.empty() && ps.empty());
        for (const std::string* m : {&spawn, &update, &vs, &ps}) {
            if (!m->empty()) std::printf("%s\n", m->c_str());
        }
    }
}

RM_TEST(Particles_Shader_ParamsAttrsAndErrorsByFile) {
    const ModuleSource a = ParseModule("param float speed = 2;\nparam color tint = (1, 0, 0, 1);\nattr float heat;\nvoid Update(inout Particle p, float dt, inout Random rnd) { p.heat += speed * dt; }\n");
    const ModuleSource b = ParseModule("param float speed = 5;\nattr float heat;\nattr float3 wind;\nvoid Spawn(inout Particle p, inout Random rnd) { p.wind = speed; }\n");
    const std::vector<ParticleValue> values{{"speed", 9, 0, 0, 0}, {"gone", 1, 1, 1, 1}};
    const std::vector<ModuleInput> in{{&a, "A.rmfx", values}, {&b, "B.rmfx", {}}};
    const EmitterShader s = AssembleShader(in);
    CHECK(s.errors.empty());
    CHECK(s.stride == 88 + 4 + 12);  // heat once (both have it), wind
    CHECK(s.paramSlots == 3);
    // Two params named speed: one per module's namespace.
    CHECK(s.hlsl.find("M0::speed = P[0].x") != std::string::npos && s.hlsl.find("M1::speed = P[2].x") != std::string::npos);
    CHECK(Compile(s.hlsl, "UpdateMain", "cs_5_0").empty() && Compile(s.hlsl, "SpawnMain", "cs_5_0").empty());
    // The values: given (9), the default (red; 5); a value of a param no longer there is not used.
    const std::vector<float> p = PackParams(in);
    CHECK(p.size() == 12 && p[0] == 9 && p[4] == 1 && p[5] == 0 && p[7] == 1 && p[8] == 5);
    // A mistake in a module's code: the compiler names its file and its line.
    const ModuleSource bad = ParseModule("param float x = 1;\n\nvoid Update(inout Particle p, float dt, inout Random rnd) {\n    p.nothing = x;\n}\n");
    const std::vector<ModuleInput> badIn{{&bad, "Bad.rmfx", {}}};
    const std::string err = Compile(AssembleShader(badIn).hlsl, "UpdateMain", "cs_5_0");
    CHECK(err.find("Bad.rmfx(4") != std::string::npos);
    // The same attr with two types: said, with the line.
    const ModuleSource c = ParseModule("attr float3 heat;\nvoid Update(inout Particle p, float dt, inout Random rnd) {}\n");
    const std::vector<ModuleInput> clash{{&a, "A.rmfx", {}}, {&c, "C.rmfx", {}}};
    const EmitterShader cs = AssembleShader(clash);
    CHECK(cs.errors.size() == 1 && cs.errors[0].kind == ModuleErrorKind::SameName && cs.errors[0].line == 1);
}

RM_TEST(Particles_Effect_SavesAndLoads) {
    ParticleEffect e = NewEffect();
    e.emitters[0].modules[1].values.push_back({"life", 2, 3, 0, 0});
    const json::Value saved = reflect::Save(e);
    ParticleEffect back;
    reflect::Load(back, saved);
    CHECK(back.emitters.size() == 1 && back.emitters[0].modules.size() == 5);
    CHECK(back.emitters[0].modules[1].builtin == "initial" && back.emitters[0].modules[1].values.size() == 1);
    CHECK(back.emitters[0].modules[1].values[0].name == "life" && back.emitters[0].modules[1].values[0].y == 3);
    CHECK(back.emitters[0].blend == ParticleBlend::Add && back.emitters[0].rate == 40);
}

RM_TEST(Particles_Clock_RateBurstDurationLoop) {
    ParticleEmitter e;
    e.rate = 10;
    e.burst = 5;
    e.duration = 1;
    e.loop = false;
    e.capacity = 100;
    EmitterClock c;
    CHECK(AdvanceClock(c, e, 0) == 5);       // the burst at once
    CHECK(AdvanceClock(c, e, 0.25f) == 2);   // 2.5 owed: 2 now ...
    CHECK(AdvanceClock(c, e, 0.25f) == 3);   // ... and the half later
    CHECK(AdvanceClock(c, e, 1.0f) == 5 && c.finished);  // to the end of the round, then nothing
    CHECK(AdvanceClock(c, e, 1.0f) == 0);
    // Looping: each round starts with its burst again; a long frame runs through rounds.
    e.loop = true;
    EmitterClock l;
    CHECK(AdvanceClock(l, e, 0) == 5);
    CHECK(AdvanceClock(l, e, 2.5f) == 25 + 10 && !l.finished && l.age > 0.49f && l.age < 0.51f);
    // Never more than the capacity at once.
    e.capacity = 7;
    EmitterClock m;
    CHECK(AdvanceClock(m, e, 10) == 7);
}

RM_TEST(Particles_EffectFile_KeepsWhatItDoesNotKnow) {
    const std::string text = R"({"kind": "rm_engine.particle_effect", "format": 1, "later": 3,
        "effect": {"emitters": [{"name": "火", "rate": 12, "glow": true, "modules": [{"builtin": "initial"}]}]}})";
    std::string err;
    reflect::Unknowns u;
    const auto e = ReadEffect(text, &err, &u);
    CHECK(e && e->emitters.size() == 1 && e->emitters[0].name == "火" && e->emitters[0].rate == 12);
    const std::string again = WriteEffect(*e, &u);
    CHECK(again.find("\"later\"") != std::string::npos && again.find("\"glow\"") != std::string::npos);
    const auto back = ReadEffect(again, &err);
    CHECK(back && back->emitters[0].modules.size() == 1 && back->emitters[0].modules[0].builtin == "initial");
    CHECK(!ReadEffect(R"({"kind": "other"})", &err) && !err.empty());
    CHECK(!ReadEffect("{", &err));
}
