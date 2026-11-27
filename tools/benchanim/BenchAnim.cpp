// 骨のアニメーションの速さ (docs/ANIMATION.md 段階 7): 骨のあるキャラクター N 体 (骨 64 本・頂点 4,000 個・クリップ 2 つを
// 混ぜる) を、1 フレームに 1 回 姿勢を作って (animation3d::Player) 描く (space3d::Renderer。太陽とスポット 4 つの影つき)。
// エンジンにもテストにも入れない (GPU が要る): 手で作って走らせる -
//
//   powershell -NoProfile -ExecutionPolicy Bypass -File tools\benchanim\run.ps1 [-Frames 300] [-Count 300] [-Options "threads cs lod"]
//
//   threads: 姿勢を base の作業の列 (WorkQueue) で並べて作る (無し: 1 本のスレッド)
//   cs:      スキニングを Compute Shader で 1 回だけ (無し: 頂点シェーダーで、影の地図を描くたびに)
//   igpu:    Windows の既定の GPU で (無し: 性能の高い GPU。ノートの PC の別の GPU)
//   lod:     遠い物の姿勢を 2 / 4 フレームに 1 回にする (画面の上の大きさで。PoseInterval)
//
// 1 フレームの「姿勢を作る CPU 時間」と「Render の CPU 時間」の平均、GPU の時間 (タイムスタンプのクエリ) の平均を出す。

#include "base/WorkQueue.h"
#include "rm_engine/animation3d/runtime/Player.h"
#include "rm_engine/animation3d/runtime/Thinning.h"
#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::space3d;

namespace {

constexpr int kBones = 64;

// 骨 64 本の木 (骨 i の親は (i - 1) / 2: 枝分かれする)。
Skeleton MakeSkeleton() {
    Skeleton s;
    for (int i = 0; i < kBones; ++i) {
        Bone b;
        b.name = "b" + std::to_string(i);
        b.parent = i == 0 ? -1 : (i - 1) / 2;
        b.rest.t = i == 0 ? Vec3{0, 1, 0} : Vec3{(i % 2 ? 0.08f : -0.08f), 0.12f, 0};
        s.bones.push_back(b);
    }
    return s;
}

// 骨ごとに Y と Z のまわりに揺れるクリップ (2 秒、30 コマ)。
Clip MakeClip(const char* name, float speed) {
    Clip c;
    c.name = name;
    c.duration = 2;
    c.rate = 30;
    c.frames = ClipFrames(2, 30);
    for (int b = 0; b < kBones; ++b) {
        BoneTrack t;
        t.bone = "b" + std::to_string(b);
        for (uint32_t f = 0; f < c.frames; ++f) {
            const float a = std::sin(speed * 3.14159f * static_cast<float>(f) / static_cast<float>(c.frames - 1) + static_cast<float>(b)) * 0.4f;
            t.r.push_back(QuatNormalize(QuatAxisAngle({0, 1, 0}, a) * QuatAxisAngle({0, 0, 1}, a * 0.5f)));
        }
        c.tracks.push_back(std::move(t));
    }
    return c;
}

// 頂点 4,000 個の柱 (骨 4 本ずつに付く)。
MeshData MakeMesh(const Skeleton& skel) {
    MeshData m;
    constexpr int kAround = 20, kUp = 199;
    for (int j = 0; j <= kUp; ++j) {
        for (int i = 0; i < kAround; ++i) {
            const float u = static_cast<float>(i) / kAround * 6.2831853f, y = static_cast<float>(j) / kUp * 2.f;
            m.vertices.push_back({{std::cos(u) * 0.25f, y, std::sin(u) * 0.25f}, {std::cos(u), 0, std::sin(u)}, {0, 0}});
        }
    }
    for (int j = 0; j < kUp; ++j) {
        for (int i = 0; i < kAround; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * kAround + i), b = static_cast<uint32_t>(j * kAround + (i + 1) % kAround);
            m.indices.insert(m.indices.end(), {a, a + kAround, b, b, a + kAround, b + kAround});
        }
    }
    SkinData skin;
    skin.skeleton = skel;
    std::vector<Mat4> model(kBones);
    for (int b = 0; b < kBones; ++b) {
        const Mat4 l = TRSMatrix(skel.bones[static_cast<size_t>(b)].rest);
        model[static_cast<size_t>(b)] = b == 0 ? l : model[static_cast<size_t>((b - 1) / 2)] * l;
        skin.inverseBind.push_back(Inverse(model[static_cast<size_t>(b)]).value_or(Mat4{}));
    }
    for (size_t v = 0; v < m.vertices.size(); ++v) {
        std::vector<std::pair<uint32_t, float>> inf;
        for (uint32_t k = 0; k < 4; ++k) inf.emplace_back(static_cast<uint32_t>((v * 7 + k * 13) % kBones), 1.f + static_cast<float>(k));
        skin.vertices.push_back(PackInfluences(inf, 0));
    }
    std::vector<Vec3> positions;
    for (const MeshVertex& v : m.vertices) positions.push_back(v.position);
    ComputeBoneBounds(skin, positions);
    m.skin = std::move(skin);
    ComputeBounds(m);
    return m;
}

double Ms(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

}  // namespace

int main(int argc, char** argv) {
    const int frames = argc > 1 && std::atoi(argv[1]) > 0 ? std::atoi(argv[1]) : 300;
    const int count = argc > 2 && std::atoi(argv[2]) > 0 ? std::atoi(argv[2]) : 300;
    bool threads = false, cs = false, lod = false, igpu = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        threads = threads || a == "threads";
        cs = cs || a == "cs";
        lod = lod || a == "lod";
        igpu = igpu || a == "igpu";
    }
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    // GPU: 既定は性能の高いもの (ノートの PC の別の GPU)。オプション igpu: Windows の既定 (内蔵のことが多い)。
    Microsoft::WRL::ComPtr<IDXGIAdapter1> fast;
    if (!igpu) {
        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&fast));
    }
    if (FAILED(D3D11CreateDevice(fast.Get(), fast ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                                 D3D11_SDK_VERSION, &device, nullptr, &ctx))) {
        std::printf("no Direct3D 11.0 device\n");
        return 1;
    }
    {
        // (どの GPU で測ったか: ノートの PC は内蔵の GPU が選ばれることがある)
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC ad{};
        if (SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&ad))) std::printf("gpu: %ls\n", ad.Description);
    }
    Renderer r;
    std::string error;
    if (!r.Init(device.Get(), &error)) {
        std::printf("Init failed: %s\n", error.c_str());
        return 1;
    }
    r.SetComputeSkinning(cs);
    const Skeleton skel = MakeSkeleton();
    const MeshData mesh = MakeMesh(skel);
    const auto gpu = r.Upload(mesh, &error);
    if (!gpu) {
        std::printf("Upload failed: %s\n", error.c_str());
        return 1;
    }
    std::vector<Clip> clips{MakeClip("walk", 2), MakeClip("run", 3)};

    // キャラクター: 格子に並べる (奥ほど遠い)。混ぜる重みは体ごとに違う。
    struct Body {
        animation3d::Player player;
        std::vector<BoneMatrix> bones;
        Mat4 world;
        int interval = 1;
    };
    std::vector<std::unique_ptr<Body>> bodies;
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    for (int i = 0; i < count; ++i) {
        auto b = std::make_unique<Body>();
        b->player.Bind(skel, clips);
        b->player.Play(0);
        const float w = static_cast<float>(i % 10) / 10.f;
        const animation3d::Player::Motion mix[] = {{0, 1 - w}, {1, w}};
        b->player.SetBlend(mix);
        b->player.SetTime(static_cast<float>(i % 17) * 0.1f);
        Transform t;
        t.position = {static_cast<float>(i % side) * 2.f - static_cast<float>(side), 0, static_cast<float>(i / side) * 2.f * (lod ? 4.f : 1.f)};
        b->world = LocalMatrix(t);
        bodies.push_back(std::move(b));
    }
    const Vec3 eye{0, 6, -12};
    const Mat4 view = LookAt(eye, {0, 0, 20}, {0, 1, 0});
    const Mat4 proj = Perspective(60, 16.f / 9.f, 0.1f, 500);
    // 間引き: 画面の上の高さの割合 (体の高さ 2 m を距離で割った、ざっとしたもの) から、何フレームに 1 回か。
    if (lod) {
        for (auto& b : bodies) {
            const Vec3 p{b->world.m[3][0], b->world.m[3][1], b->world.m[3][2]};
            const float d = std::max(0.1f, Length(p - eye));
            b->interval = animation3d::PoseInterval(2.f / (d * 2.f * std::tan(30.f * 3.14159f / 180.f)), true);
        }
    }

    std::vector<Light> lights(5);
    lights[0].type = LightType::Directional;
    lights[0].direction = {-0.45f, -1.f, -0.3f};
    for (int i = 1; i < 5; ++i) {
        lights[static_cast<size_t>(i)].type = LightType::Spot;
        lights[static_cast<size_t>(i)].position = {static_cast<float>(i) * 8.f - 20.f, 8, 10};
        lights[static_cast<size_t>(i)].direction = {0, -1, 0.2f};
        lights[static_cast<size_t>(i)].range = 20;
        lights[static_cast<size_t>(i)].spotAngle = 60;
    }
    std::vector<MeshDraw> draws(bodies.size());
    for (size_t i = 0; i < bodies.size(); ++i) {
        draws[i].mesh = gpu.get();
        draws[i].world = bodies[i]->world;
        draws[i].moving = true;
    }

    // GPU の時間: 1 フレームごとのタイムスタンプ (前のフレームのものを、次のフレームで読む)。
    D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    Microsoft::WRL::ComPtr<ID3D11Query> disjoint, t0, t1;
    device->CreateQuery(&qd, &disjoint);
    qd.Query = D3D11_QUERY_TIMESTAMP;
    device->CreateQuery(&qd, &t0);
    device->CreateQuery(&qd, &t1);

    WorkQueue& queue = SharedWorkQueue();
    double poseMs = 0, renderMs = 0, gpuMs = 0;
    int gpuSamples = 0;
    const float dt = 1.f / 60.f;
    for (int frame = 0; frame < frames; ++frame) {
        const auto a = std::chrono::steady_clock::now();
        auto step = [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                Body& b = *bodies[i];
                b.player.Tick(dt);
                if ((static_cast<size_t>(frame) + i) % static_cast<size_t>(b.interval) != 0) continue;  // (間引き: 前の骨の行列のまま)
                b.player.EvaluatePose(skel);
                b.player.FinishPose(*mesh.skin, b.bones);
            }
        };
        if (threads) {
            queue.ParallelFor(bodies.size(), step);
        } else {
            step(0, bodies.size());
        }
        const auto b = std::chrono::steady_clock::now();
        for (size_t i = 0; i < bodies.size(); ++i) draws[i].bones = bodies[i]->bones;
        RenderFrame f;
        f.width = 1280;
        f.height = 720;
        f.view = view;
        f.projection = proj;
        f.meshes = draws;
        f.lights = lights;
        f.shadows = true;
        ctx->Begin(disjoint.Get());
        ctx->End(t0.Get());
        const auto c = std::chrono::steady_clock::now();
        r.Render(ctx.Get(), f);
        const auto d = std::chrono::steady_clock::now();
        ctx->End(t1.Get());
        ctx->End(disjoint.Get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        while (ctx->GetData(disjoint.Get(), &dj, sizeof(dj), 0) == S_FALSE) {
        }
        UINT64 s0 = 0, s1 = 0;
        while (ctx->GetData(t0.Get(), &s0, sizeof(s0), 0) == S_FALSE) {
        }
        while (ctx->GetData(t1.Get(), &s1, sizeof(s1), 0) == S_FALSE) {
        }
        if (frame >= 10) {
            poseMs += Ms(b - a);
            renderMs += Ms(d - c);
            if (!dj.Disjoint && dj.Frequency > 0) {
                gpuMs += static_cast<double>(s1 - s0) * 1000.0 / static_cast<double>(dj.Frequency);
                ++gpuSamples;
            }
        }
    }
    const double n = std::max(1, frames - 10);
    std::printf("characters %d  bones %d  vertices %zu  options:%s%s%s  (threads %u)\n", count, kBones, mesh.vertices.size(), threads ? " threads" : "",
                cs ? " cs" : "", lod ? " lod" : "", queue.Threads());
    std::printf("pose   %.3f ms / frame (CPU)\n", poseMs / n);
    std::printf("render %.3f ms / frame (CPU)\n", renderMs / n);
    std::printf("gpu    %.3f ms / frame\n", gpuSamples > 0 ? gpuMs / gpuSamples : 0.0);
    const Renderer::Stats& st = r.LastStats();
    std::printf("draw calls %zu  skinned draws %zu  maps drawn %zu  compute-skinned %zu  deform dispatches %zu\n", st.drawCalls, st.skinnedDraws, st.mapsDrawn,
                st.computeSkinned, st.deformDispatches);
    // スキニングの回数: Compute Shader なら 1 フレームに 1 回。頂点シェーダーなら絵と、描き直した影の地図ごとに (地図に入る物だけ)。
    if (st.computeSkinned > 0) {
        std::printf("skinning: once per frame (compute shader)\n");
    } else {
        std::printf("skinning: in the picture + up to %zu shadow maps (vertex shader)\n", st.mapsDrawn);
    }
    return 0;
}
