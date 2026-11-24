// パーティクルのプラグイン (plugins/rm_engine/particles) のテスト: モジュール (.rmfx) を読む、
// エミッターのシェーダーをモジュールから組み立てる、組み立てたものがコンパイルできる
// (D3DCompile: GPU は要らない)。

#include "Test.h"

#include "core/reflect/Data.h"
#include "rm_engine/game_production/editor/GameExport.h"
#include "rm_engine/particles_x_physics/runtime/Register.h"
#include "rm_engine/weather_x_physics/runtime/Register.h"
#include "rm_engine/physics/runtime/Components.h"
#include "rm_engine/space3d/runtime/Transform.h"
#include "rm_engine/particles/runtime/BuiltinModules.h"
#include "rm_engine/particles/runtime/ModuleSource.h"
#include "rm_engine/particles/runtime/ParticleEffect.h"
#include "rm_engine/particles/runtime/ParticlePlayer.h"
#include "rm_engine/particles/runtime/Presets.h"
#include "rm_engine/particles/runtime/ParticleShader.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

using namespace rm;
using namespace rm::particles;

namespace {

// テキストのエントリーを 1 つコンパイルする。失敗したらコンパイラーの言葉を返す。
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
    CHECK(t && t->type == ParamType::Color && t->value[3] == 1);  // 値の無い色: 白
    const ModuleParam* r = FindParam(m, "range");
    CHECK(r && r->value[0] == 3 && r->value[1] == 3);             // 全部に同じ値
    CHECK(FindParam(m, "on") && FindParam(m, "on")->value[0] == 1);
    CHECK(m.attrs[0].name == "heat" && m.attrs[0].type == "float" && m.attrs[0].line == 7);
    CHECK(!m.hasSpawn && m.hasUpdate);
    // エンジンの行はコードの中では空: コンパイラーの行番号はファイルのものと同じ。
    size_t lines = 0;
    for (const char c : m.code) lines += c == '\n';
    CHECK(lines == 10 && m.code.find("param") == std::string::npos && m.code.find("p.velocity") != std::string::npos);
}

// コメントの中だけにある関数は無いものとする (新しいモジュールの "// void Spawn(...) { }")。
RM_TEST(Particles_ModuleSource_CommentsAreNotFunctions) {
    const ModuleSource m = ParseModule(
        "module \"x\"\n"
        "// void Spawn(inout Particle p, inout Random rnd) { }\n"
        "/* void Update(inout Particle p, float dt, inout Random rnd) { } */\n"
        "void Update(inout Particle p, float dt, inout Random rnd) { p.age += 0; }\n");
    CHECK(!m.hasSpawn && m.hasUpdate && m.errors.empty());
    CHECK(!ParseModule("// void Update(inout Particle p, float dt, inout Random rnd) {}\n").hasUpdate);
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
    CHECK(kinds.size() == 6 && kinds[4] == ModuleErrorKind::BadValue);  // ("1; param float e = 2" は値ではない)
    CHECK(kinds.size() == 6 && kinds[5] == ModuleErrorKind::NoFunction && m.errors[5].line == 0);
    CHECK(!Message(m.errors[0]).empty());
}

RM_TEST(Particles_Builtins_ReadWithoutErrors) {
    CHECK(BuiltinModules().size() >= 8);
    for (const BuiltinModule& b : BuiltinModules()) {
        const ModuleSource* m = FindBuiltin(b.id);
        CHECK(m && m->errors.empty() && !m->title.empty());
        if (!m) continue;
        for (const ModuleParam& p : m->params) CHECK(!p.label.empty());  // どの欄にも言葉がある
    }
    CHECK(FindBuiltin("no_such_module") == nullptr);
}

RM_TEST(Particles_Shader_PutTogetherAndCompiles) {
    // 組み込みのモジュールを全部まとめて、その後で新しいエフェクトのエミッター: 3 種類のシェーダーが全部コンパイルできる。
    ParticleEmitter all;
    for (const BuiltinModule& b : BuiltinModules()) {
        ParticleModule m;
        m.builtin = std::string(b.id);
        all.modules.push_back(m);
    }
    for (const ParticleEmitter& e : {all, NewEffect().emitters[0]}) {
        const std::vector<ModuleInput> in = Inputs(e);
        const EmitterShader s = AssembleShader(in);
        CHECK(s.errors.empty() && s.stride == 96);
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
    CHECK(s.stride == 96 + 4 + 12);  // heat は 1 回 (両方が持つ)、wind
    CHECK(s.paramSlots == 3);
    // speed という名前の param が 2 つ: モジュールごとの名前空間で 1 つずつ。
    CHECK(s.hlsl.find("M0::speed = P[0].x") != std::string::npos && s.hlsl.find("M1::speed = P[2].x") != std::string::npos);
    CHECK(Compile(s.hlsl, "UpdateMain", "cs_5_0").empty() && Compile(s.hlsl, "SpawnMain", "cs_5_0").empty());
    // 値: 渡したもの (9)、既定 (赤; 5)。もう無い param の値は使わない。
    const std::vector<float> p = PackParams(in);
    CHECK(p.size() == 12 && p[0] == 9 && p[4] == 1 && p[5] == 0 && p[7] == 1 && p[8] == 5);
    // モジュールのコードの間違い: コンパイラーがそのファイルと行を示す。
    const ModuleSource bad = ParseModule("param float x = 1;\n\nvoid Update(inout Particle p, float dt, inout Random rnd) {\n    p.nothing = x;\n}\n");
    const std::vector<ModuleInput> badIn{{&bad, "Bad.rmfx", {}}};
    const std::string err = Compile(AssembleShader(badIn).hlsl, "UpdateMain", "cs_5_0");
    CHECK(err.find("Bad.rmfx(4") != std::string::npos);
    // 同じ attr が 2 つの型で: 行を添えて知らせる。
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
    CHECK(AdvanceClock(c, e, 0) == 5);       // バーストは一度に
    CHECK(AdvanceClock(c, e, 0.25f) == 2);   // 2.5 個の借り: 今 2 個 ...
    CHECK(AdvanceClock(c, e, 0.25f) == 3);   // ... 半分は後で
    CHECK(AdvanceClock(c, e, 1.0f) == 5 && c.finished);  // ラウンドの終わりまで。その後は何も出ない
    CHECK(AdvanceClock(c, e, 1.0f) == 0);
    // ループ: ラウンドごとにまたバーストから始まる。長いフレームは複数のラウンドを通り抜ける。
    e.loop = true;
    EmitterClock l;
    CHECK(AdvanceClock(l, e, 0) == 5);
    CHECK(AdvanceClock(l, e, 2.5f) == 25 + 10 && !l.finished && l.age > 0.49f && l.age < 0.51f);
    // 同時に容量より多くはならない。
    e.capacity = 7;
    EmitterClock m;
    CHECK(AdvanceClock(m, e, 10) == 7);
    // 長さ をもう再生した時間より短くした (再生中の編集): ラウンドは今終わる。
    // 数は負にならない (以前は約 40 億個のパーティクルとして読まれた)。
    e.capacity = 100;
    e.loop = false;
    EmitterClock s;
    AdvanceClock(s, e, 0);
    AdvanceClock(s, e, 0.8f);
    e.duration = 0.2f;
    CHECK(AdvanceClock(s, e, 0.1f) == 0 && s.finished);
    e.loop = true;
    e.duration = 1;
    EmitterClock t;
    AdvanceClock(t, e, 0);
    AdvanceClock(t, e, 0.8f);
    e.duration = 0.2f;
    const uint32_t looped = AdvanceClock(t, e, 0.1f);
    CHECK(looped >= 5 && looped <= 7 && !t.finished);  // (次のラウンド: そのバースト、その後にレートのフレーム)
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

RM_TEST(Particles_Export_TakesTheModulesAnEffectNames) {
    // シーンがエフェクトを指し、エフェクトがファイルのモジュールを指す: 両方ともゲームに入る。
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "rm_particles_export_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Assets", ec);
    const std::string effectId(32, 'a'), moduleId(32, 'b');
    ParticleEffect e = NewEffect();
    ParticleModule m;
    m.file = AssetRef{moduleId};
    e.emitters[0].modules.push_back(m);
    std::ofstream(root / "Assets" / "fire.rmparticle", std::ios::binary) << WriteEffect(e);
    std::ofstream(root / "Assets" / "Wind.rmfx", std::ios::binary) << "void Update(inout Particle p, float dt, inout Random rnd) {}\n";
    game_production::GameExportInput in;
    in.projectRoot = root;
    in.scenes.push_back({"Assets/Scenes/Main.rmscene", "{\"entities\": [{\"components\": [{\"effect\": \"" + effectId + "\"}]}]}"});
    in.assetFile = [&](const AssetRef& ref) -> fs::path {
        if (ref.guid == effectId) return root / "Assets" / "fire.rmparticle";
        if (ref.guid == moduleId) return root / "Assets" / "Wind.rmfx";
        return {};
    };
    const auto assets = game_production::CollectAssets(in);
    CHECK(assets.size() == 2);
    CHECK(assets.size() == 2 && assets[0].rel == "Assets/fire.rmparticle" && assets[1].rel == "Assets/Wind.rmfx");
    fs::remove_all(root, ec);
}

RM_TEST(Particles_Physics_CollidersBecomeShapes) {
    // 回転して拡大した箱、球、トリガー (除く)、メッシュ (除く): 世界の中の形。
    game_production::Scene scene;
    scene.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<physics::Collider>()});
    auto add = [&](const char* name, Vec3 pos, Vec3 scale, physics::Shape shape, bool trigger) {
        const int id = scene.Create(name);
        scene.AddComponent(id, reflect::TypeOf<space3d::Transform>());
        scene.AddComponent(id, reflect::TypeOf<physics::Collider>());
        auto* e = scene.Find(id);
        auto* t = static_cast<space3d::Transform*>(e->FindComponent(reflect::TypeOf<space3d::Transform>().name)->value.Get());
        t->position = pos;
        t->scale = scale;
        auto* c = static_cast<physics::Collider*>(e->FindComponent(reflect::TypeOf<physics::Collider>().name)->value.Get());
        c->shape = shape;
        c->trigger = trigger;
        return id;
    };
    const int box = add("箱", {1, 2, 3}, {2, 1, 1}, physics::Shape::Box, false);
    add("球", {0, 0, 0}, {3, 3, 3}, physics::Shape::Sphere, false);
    add("トリガー", {0, 0, 0}, {1, 1, 1}, physics::Shape::Box, true);
    add("メッシュ", {0, 0, 0}, {1, 1, 1}, physics::Shape::Mesh, false);
    std::map<int, space3d::Mat4> worlds;
    for (const auto& e : scene.Entities()) {
        const auto* t = static_cast<const space3d::Transform*>(e.FindComponent(reflect::TypeOf<space3d::Transform>().name)->value.Get());
        worlds[e.data.id] = space3d::WorldMatrix(space3d::Mat4{}, *t);
    }
    std::vector<CollisionShape> shapes;
    particles_x_physics::GatherShapes(scene, [&](int id) -> const space3d::Mat4* { return id == box || worlds.count(id) ? &worlds[id] : nullptr; }, shapes);
    CHECK(shapes.size() == 2);
    if (shapes.size() != 2) return;
    CHECK(shapes[0].type == CollisionType::Box && shapes[0].center[0] == 1 && shapes[0].center[1] == 2 && shapes[0].center[2] == 3);
    CHECK(shapes[0].sizeX == 1 && shapes[0].sizeY == 0.5f && shapes[0].sizeZ == 0.5f);  // 1 x 拡大の半分
    CHECK(shapes[0].axisX[0] == 1 && shapes[0].axisY[1] == 1 && shapes[0].axisZ[2] == 1);  // 軸は単位の長さのまま
    CHECK(shapes[1].type == CollisionType::Sphere && shapes[1].sizeX == 1.5f);  // 0.5 x 一番大きい拡大
    // 切ってある (場所が無い): 何もない。
    std::vector<CollisionShape> none;
    particles_x_physics::GatherShapes(scene, [](int) -> const space3d::Mat4* { return nullptr; }, none);
    CHECK(none.empty());
}

RM_TEST(Particles_Shader_CollisionTrailsAndStretchCompile) {
    // 軌跡のある 当たる モジュール: カーネルと 2 種類の描画がコンパイルできる。
    ParticleEmitter e = NewEffect().emitters[0];
    ParticleModule shapes, depth;
    shapes.builtin = "collide_shapes";
    depth.builtin = "collide_depth";
    e.modules.push_back(shapes);
    e.modules.push_back(depth);
    const std::vector<ModuleInput> in = Inputs(e);
    const EmitterShader s = AssembleShader(in);
    CHECK(s.errors.empty());
    for (const auto& [entry, target] : {std::pair{"SpawnMain", "cs_5_0"}, std::pair{"UpdateMain", "cs_5_0"}, std::pair{"ParticleVS", "vs_5_0"},
                                        std::pair{"ParticlePS", "ps_5_0"}, std::pair{"TrailVS", "vs_5_0"}, std::pair{"TrailPS", "ps_5_0"}}) {
        const std::string err = Compile(s.hlsl, entry, target);
        CHECK(err.empty());
        if (!err.empty()) std::printf("%s: %s\n", entry, err.c_str());
    }
}

RM_TEST(Particles_Presets_UseModulesThatExistAndCompile) {
    // プリセットはすべて: 組み込みモジュールだけ、どの値もモジュールの param の名前、
    // エミッターのシェーダーがコンパイルできる (6 エントリー全部)。保存して、そのまま読める。
    CHECK(EffectPresets().size() >= 8 && EffectPresets()[0].id == "glow");
    for (const EffectPreset& p : EffectPresets()) {
        const ParticleEffect e = p.make();
        CHECK(!e.emitters.empty() && !p.title.empty());
        for (const ParticleEmitter& em : e.emitters) {
            for (const ParticleModule& m : em.modules) {
                const ModuleSource* src = FindBuiltin(m.builtin);
                CHECK(src != nullptr);
                if (!src) continue;
                for (const ParticleValue& v : m.values) {
                    CHECK(FindParam(*src, v.name) != nullptr);
                    if (!FindParam(*src, v.name)) std::printf("%s: %s has no %s\n", std::string(p.id).c_str(), m.builtin.c_str(), v.name.c_str());
                }
            }
            const EmitterShader s = AssembleShader(Inputs(em));
            CHECK(s.errors.empty());
            for (const auto& [entry, target] : {std::pair{"SpawnMain", "cs_5_0"}, std::pair{"UpdateMain", "cs_5_0"}, std::pair{"ParticleVS", "vs_5_0"},
                                                std::pair{"ParticlePS", "ps_5_0"}, std::pair{"TrailVS", "vs_5_0"}, std::pair{"TrailPS", "ps_5_0"}}) {
                const std::string err = Compile(s.hlsl, entry, target);
                CHECK(err.empty());
                if (!err.empty()) std::printf("%s %s: %s\n", std::string(p.id).c_str(), entry, err.c_str());
            }
        }
        std::string err;
        const auto back = ReadEffect(WriteEffect(e), &err);
        CHECK(back && back->emitters.size() == e.emitters.size());
    }
}

RM_TEST(Weather_MeshesBlockRainAndSnow) {
    // 雨と雪が当たるもの: 見えているメッシュ (コライダーではない)。天候 × 物理 は動くもの
    // (動かない ではない体・キャラクター) を言い、動くものの子のメッシュも動く (雪に足跡)。
    game_production::Scene scene;
    scene.SetComponentTypes({&reflect::TypeOf<space3d::Transform>(), &reflect::TypeOf<physics::Collider>(),
                             &reflect::TypeOf<physics::Character>(), &reflect::TypeOf<physics::Rigidbody>()});
    const int box = scene.Create("箱");
    scene.AddComponent(box, reflect::TypeOf<physics::Collider>());  // (コライダーがあっても、動かない)
    const int walker = scene.Create("歩く人");
    scene.AddComponent(walker, reflect::TypeOf<physics::Character>());
    const int look = scene.Create("見た目", walker);
    const int ghost = scene.Create("見えない壁");
    scene.AddComponent(ghost, reflect::TypeOf<physics::Collider>());
    std::vector<int> movers;
    weather_x_physics::GatherMovers(scene, movers);
    CHECK(movers == std::vector<int>{walker});
    const char meshes[2] = {};
    const auto* boxMesh = reinterpret_cast<const space3d::GpuMesh*>(&meshes[0]);
    const auto* lookMesh = reinterpret_cast<const space3d::GpuMesh*>(&meshes[1]);
    std::map<int, space3d::Mat4> placed;
    for (const int id : {box, walker, look, ghost}) placed[id] = space3d::Mat4{};
    placed[box].m[3][1] = 2;
    auto blocks = [&] {
        std::vector<space3d::MapShape> shapes;
        weather::WeatherBlocksOf(
            scene, movers, [&](int id) -> const space3d::Mat4* { return placed.count(id) ? &placed[id] : nullptr; },
            [&](int id) -> const space3d::GpuMesh* { return id == box ? boxMesh : id == look ? lookMesh : nullptr; }, shapes);
        return shapes;
    };
    // 見えない壁 (コライダーだけ) と、メッシュの無い歩く人自身は通り抜ける。
    auto shapes = blocks();
    CHECK(shapes.size() == 2);
    if (shapes.size() == 2) {
        CHECK(shapes[0].mesh == boxMesh && !shapes[0].moving && shapes[0].world.m[3][1] == 2);
        CHECK(shapes[1].mesh == lookMesh && shapes[1].moving);
    }
    // 切ってある (場所が無い) ものも通り抜ける。
    placed.erase(box);
    CHECK(blocks().size() == 1);
}
