// How long the 3D renderer takes on the CPU for one frame (space3d::Renderer::Render): 1000 boxes
// (casting shadows) and 8 lights (the sun, point lights and spots, with shadows), seen by a moving
// camera. Not part of the engine or its tests (it needs a GPU): built and run by hand -
//
//   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench3d\run.ps1 [-Frames 300] [-Case boxes|mesh]
//
//   boxes: 1000 boxes (Box: the renderer's cube)
//   mesh:  1000 models of the same mesh (MeshDraw: a ball of 480 triangles, one material, uploaded once)
//
// Prints the average and the median of the CPU time spent in Render (the GPU is waited for between
// frames, outside the measured time).

#include "rm_engine/space3d/runtime/Mesh.h"
#include "rm_engine/space3d/runtime/Renderer3D.h"
#include "rm_engine/space3d/runtime/View.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace rm;
using namespace rm::space3d;

int main(int argc, char** argv) {
    const int frames = argc > 1 && std::atoi(argv[1]) > 0 ? std::atoi(argv[1]) : 300;
    const bool meshCase = argc > 2 && std::string(argv[2]) == "mesh";
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    // Anywhere after the others: "fl10" = a device of feature level 10.0 only (the least the engine
    // needs); "debug" = with Direct3D's debug layer (its messages go to the debugger's output).
    bool fl10 = false, debug = false;
    for (int i = 1; i < argc; ++i) {
        fl10 = fl10 || std::string(argv[i]) == "fl10";
        debug = debug || std::string(argv[i]) == "debug";
    }
    const D3D_FEATURE_LEVEL all[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const D3D_FEATURE_LEVEL only10[] = {D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | (debug ? D3D11_CREATE_DEVICE_DEBUG : 0),
                                 fl10 ? only10 : all, fl10 ? 1 : 3, D3D11_SDK_VERSION, &device, &got, &ctx))) {
        std::printf("no Direct3D 11 device%s\n", debug ? " (with the debug layer: is it installed?)" : "");
        return 1;
    }
    if (fl10 || debug) std::printf("feature level %x%s\n", static_cast<unsigned>(got), debug ? ", debug layer" : "");
    Microsoft::WRL::ComPtr<ID3D11InfoQueue> info;
    if (debug) device.As(&info);
    Renderer r;
    std::string error;
    if (!r.Init(device.Get(), &error)) {
        std::printf("Init failed: %s\n", error.c_str());
        return 1;
    }

    // 1000 boxes on a 40 x 25 grid (2 apart), some stacked.
    std::vector<Box> boxes;
    for (int i = 0; i < 1000; ++i) {
        Transform t;
        t.position = {static_cast<float>(i % 40) * 2.f - 40.f, 0.5f + static_cast<float>(i % 3), static_cast<float>(i / 40) * 2.f - 25.f};
        t.rotation = {0, static_cast<float>(i * 13 % 90), 0};
        boxes.push_back({LocalMatrix(t), {0.7f, 0.7f, 0.75f, 1}, true});
    }
    // The same 1000 places as models of one mesh: a ball (16 x 15 quads), one material.
    MeshData ball;
    constexpr int kAround = 16, kUp = 15;
    for (int j = 0; j <= kUp; ++j) {
        for (int i = 0; i <= kAround; ++i) {
            const float u = static_cast<float>(i) / kAround * 6.2831853f, v = static_cast<float>(j) / kUp * 3.1415926f;
            const Vec3 n{std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u)};
            ball.vertices.push_back({n * 0.5f, n, {static_cast<float>(i) / kAround, static_cast<float>(j) / kUp}});
        }
    }
    for (int j = 0; j < kUp; ++j) {
        for (int i = 0; i < kAround; ++i) {
            const uint32_t a = static_cast<uint32_t>(j * (kAround + 1) + i), b = a + 1, c = a + kAround + 1, d = c + 1;
            ball.indices.insert(ball.indices.end(), {a, c, b, b, c, d});
        }
    }
    ComputeBounds(ball);
    const auto ballGpu = r.Upload(ball, &error);
    if (!ballGpu) {
        std::printf("Upload failed: %s\n", error.c_str());
        return 1;
    }
    std::vector<DrawMaterial> ballMaterials(1);
    ballMaterials[0].color = {0.8f, 0.5f, 0.3f, 1};
    ballMaterials[0].roughness = 0.4f;
    std::vector<MeshDraw> meshes;
    for (const Box& b : boxes) {
        MeshDraw m;
        m.mesh = ballGpu.get();
        m.world = b.world;
        m.materials = ballMaterials;
        meshes.push_back(m);
    }
    const std::span<const Box> drawBoxes = meshCase ? std::span<const Box>() : std::span<const Box>(boxes);
    const std::span<const MeshDraw> drawMeshes = meshCase ? std::span<const MeshDraw>(meshes) : std::span<const MeshDraw>();

    // 8 lights: the sun, 1 point light and 6 spots, all casting shadows.
    std::vector<Light> lights(8);
    lights[0].type = LightType::Directional;
    lights[0].direction = {-0.45f, -1.f, -0.3f};
    lights[1].type = LightType::Point;
    lights[1].position = {0, 4, 0};
    lights[1].range = 15;
    for (int i = 2; i < 8; ++i) {
        lights[static_cast<size_t>(i)].type = LightType::Spot;
        lights[static_cast<size_t>(i)].position = {static_cast<float>(i) * 6.f - 25.f, 6, static_cast<float>(i % 2) * 10.f - 5.f};
        lights[static_cast<size_t>(i)].direction = {0, -1, 0.2f};
        lights[static_cast<size_t>(i)].range = 12;
        lights[static_cast<size_t>(i)].spotAngle = 50;
    }

    // Bench3D.exe image <file.ppm>: one fixed frame (the sun, 1 point light and 2 spots: every one gets
    // its shadow maps whatever the order) saved, to compare the picture of two versions.
    // Bench3D.exe image <file.ppm> [mesh]: the models instead of the boxes.
    if (argc > 2 && std::string(argv[1]) == "image") {
        lights.resize(4);
        RenderFrame f;
        f.width = 640;
        f.height = 360;
        f.clear = {0.75f, 0.75f, 0.78f, 1};  // light: the shadows on the floor show
        f.view = LookAt({0, 18, 30}, {0, 0, 0}, {0, 1, 0});
        f.projection = Perspective(60, 16.f / 9.f, 0.1f, 500);
        // [moving]: every other box moving (the shadow maps keep a layer of the rest); one is moved
        // and put back, so the last frame lays the kept layer back and draws only the moving ones -
        // the picture must be the same as without.
        const bool moving = argc > 3 && std::string(argv[3]) == "moving";
        std::vector<Box> still = boxes;
        if (moving) {
            for (size_t i = 1; i < still.size(); i += 2) still[i].moving = true;
        }
        if (argc > 3 && std::string(argv[3]) == "mesh") {
            f.meshes = meshes;
        } else {
            f.boxes = still;
        }
        f.lights = lights;
        f.shadows = true;
        if (moving) {
            r.Render(ctx.Get(), f);
            std::vector<Box> moved = still;
            moved[1].world.m[3][0] += 0.5f;
            f.boxes = moved;
            r.Render(ctx.Get(), f);
            f.boxes = still;
            const Renderer::Stats before = r.LastStats();
            r.Render(ctx.Get(), f);
            std::printf("moving: layers drawn %zu (frame 2), maps drawn %zu kept %zu layers drawn %zu (frame 3)\n", before.layersDrawn,
                        r.LastStats().mapsDrawn, r.LastStats().mapsKept, r.LastStats().layersDrawn);
        }
        r.Render(ctx.Get(), f);
        ID3D11Texture2D* picture = r.Render(ctx.Get(), f);  // twice: the second keeps the shadow maps it can
        if (!picture) return 1;
        D3D11_TEXTURE2D_DESC td{};
        picture->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING;
        td.BindFlags = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        device->CreateTexture2D(&td, nullptr, &staging);
        ctx->CopyResource(staging.Get(), picture);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return 1;
        FILE* out = std::fopen(argv[2], "wb");
        std::fprintf(out, "P6\n%u %u\n255\n", td.Width, td.Height);
        for (UINT y = 0; y < td.Height; ++y) {
            const auto* row = static_cast<const uint8_t*>(map.pData) + y * map.RowPitch;
            for (UINT x = 0; x < td.Width; ++x) {
                const uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]};
                std::fwrite(rgb, 1, 3, out);
            }
        }
        std::fclose(out);
        ctx->Unmap(staging.Get(), 0);
        std::printf("saved %s\n", argv[2]);
        if (info) {
            // What the debug layer said (warnings and errors), if anything.
            size_t told = 0;
            for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
                SIZE_T size = 0;
                info->GetMessage(i, nullptr, &size);
                std::vector<char> buffer(size);
                auto* m = reinterpret_cast<D3D11_MESSAGE*>(buffer.data());
                if (FAILED(info->GetMessage(i, m, &size)) || m->Severity > D3D11_MESSAGE_SEVERITY_WARNING) continue;
                if (++told <= 10) std::printf("debug layer: %.*s\n", static_cast<int>(m->DescriptionByteLength), m->pDescription);
            }
            std::printf("debug layer: %zu warnings / errors\n", told);
        }
        return 0;
    }

    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    Microsoft::WRL::ComPtr<ID3D11Query> done;
    device->CreateQuery(&qd, &done);
    std::vector<double> ms;
    for (int frame = 0; frame < frames + 20; ++frame) {
        // The camera turns slowly around the middle (the cascades follow it; the lights and boxes stay).
        const float a = static_cast<float>(frame) * 0.01f;
        RenderFrame f;
        f.width = 1280;
        f.height = 720;
        f.view = LookAt({std::sin(a) * 30.f, 18, std::cos(a) * 30.f}, {0, 0, 0}, {0, 1, 0});
        f.projection = Perspective(60, 16.f / 9.f, 0.1f, 500);
        f.boxes = drawBoxes;
        f.meshes = drawMeshes;
        f.lights = lights;
        f.shadows = true;
        const auto start = std::chrono::steady_clock::now();
        r.Render(ctx.Get(), f);
        const auto end = std::chrono::steady_clock::now();
        // The GPU finishes the frame before the next one (outside the time).
        ctx->End(done.Get());
        while (ctx->GetData(done.Get(), nullptr, 0, 0) == S_FALSE) {
        }
        if (frame >= 20) ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    double sum = 0;
    for (const double v : ms) sum += v;
    std::sort(ms.begin(), ms.end());
    std::printf("frames %zu  average %.3f ms  median %.3f ms  (%s, 8 lights, shadows)\n", ms.size(), sum / static_cast<double>(ms.size()),
                ms[ms.size() / 2], meshCase ? "1000 models of the same mesh" : "1000 boxes");
    const Renderer::Stats& st = r.LastStats();
    std::printf("last frame: %zu drawn of %zu, %zu draws, shadow maps drawn %zu kept %zu\n", st.drawn, st.things, st.drawCalls, st.mapsDrawn,
                st.mapsKept);
    return 0;
}
