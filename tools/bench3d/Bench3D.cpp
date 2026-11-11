// How long the 3D renderer takes on the CPU for one frame (space3d::Renderer::Render): 1000 boxes
// (casting shadows) and 8 lights (the sun, point lights and spots, with shadows), seen by a moving
// camera. Not part of the engine or its tests (it needs a GPU): built and run by hand -
//
//   powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench3d\run.ps1
//
// Prints the average and the median of the CPU time spent in Render (the GPU is waited for between
// frames, outside the measured time).

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
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 3, D3D11_SDK_VERSION,
                                 &device, nullptr, &ctx))) {
        std::printf("no Direct3D 11 device\n");
        return 1;
    }
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
    if (argc > 2 && std::string(argv[1]) == "image") {
        lights.resize(4);
        RenderFrame f;
        f.width = 640;
        f.height = 360;
        f.clear = {0.75f, 0.75f, 0.78f, 1};  // light: the shadows on the floor show
        f.view = LookAt({0, 18, 30}, {0, 0, 0}, {0, 1, 0});
        f.projection = Perspective(60, 16.f / 9.f, 0.1f, 500);
        f.boxes = boxes;
        f.lights = lights;
        f.shadows = true;
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
        f.boxes = boxes;
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
    std::printf("frames %zu  average %.3f ms  median %.3f ms  (1000 boxes, 8 lights, shadows)\n", ms.size(), sum / static_cast<double>(ms.size()),
                ms[ms.size() / 2]);
    return 0;
}
